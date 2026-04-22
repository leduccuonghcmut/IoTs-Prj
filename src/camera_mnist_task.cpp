#include "camera_mnist_task.h"

#include <ArduinoHttpClient.h>
#include <WiFi.h>
#include <new>

#include "TinyML_MNIST.h"
#include "global.h"

#include <TensorFlowLite_ESP32.h>
#include "esp_heap_caps.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace
{
    constexpr int kCameraFrameSize = 96;
    constexpr int kMnistImageSize = 28;
    constexpr int kDigitCanvasSize = 20;
    constexpr size_t kFrameBytes = kCameraFrameSize * kCameraFrameSize;
    constexpr int kMaxPixels = kCameraFrameSize * kCameraFrameSize;
    // The updated CNN-based MNIST model needs substantially more runtime memory
    // than the previous dense model.
    constexpr int kTensorArenaSize = 96 * 1024;
    constexpr TickType_t kCameraPollDelay = pdMS_TO_TICKS(2000);
    constexpr TickType_t kRetryDelay = pdMS_TO_TICKS(3000);

    static uint8_t *g_frameBuffer = nullptr;
    static uint8_t *g_binaryMask = nullptr;
    static uint8_t *g_selectedMask = nullptr;
    static uint16_t *g_componentQueue = nullptr;
    static uint8_t *g_inputCanvas = nullptr;
    static uint8_t *g_tensorArena = nullptr;
    static tflite::MicroMutableOpResolver<7> g_resolver;

    struct DigitBBox
    {
        int minX;
        int minY;
        int maxX;
        int maxY;
        int count;
    };

    struct PreparedDigitStats
    {
        int width;
        int height;
        int foregroundCount;
        float fillRatio;
        float centerOffset;
        bool valid;
    };

    struct ComponentStats
    {
        int minX;
        int minY;
        int maxX;
        int maxY;
        int count;
        float centerOffset;
        float score;
        bool valid;
    };

    void setMnistStatus(AppContext *ctx, bool ready, int digit, float confidence, const String &status)
    {
        if (ctx == NULL || ctx->stateMutex == NULL)
            return;

        if (xSemaphoreTake(ctx->stateMutex, portMAX_DELAY) == pdTRUE)
        {
            ctx->mnistReady = ready;
            ctx->mnistDigit = digit;
            ctx->mnistConfidence = confidence;
            ctx->mnistStatus = status;
            xSemaphoreGive(ctx->stateMutex);
        }
    }

    void *allocateMnistBuffer(size_t bytes)
    {
        void *buffer = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (buffer != nullptr)
            return buffer;

        return heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
    }

    bool ensureMnistBuffers(AppContext *ctx)
    {
        if (g_frameBuffer != nullptr &&
            g_binaryMask != nullptr &&
            g_selectedMask != nullptr &&
            g_componentQueue != nullptr &&
            g_inputCanvas != nullptr &&
            g_tensorArena != nullptr)
        {
            return true;
        }

        if (g_frameBuffer == nullptr)
            g_frameBuffer = static_cast<uint8_t *>(allocateMnistBuffer(kFrameBytes));
        if (g_binaryMask == nullptr)
            g_binaryMask = g_frameBuffer;
        if (g_selectedMask == nullptr)
            g_selectedMask = static_cast<uint8_t *>(allocateMnistBuffer(kMaxPixels));
        if (g_componentQueue == nullptr)
            g_componentQueue = static_cast<uint16_t *>(allocateMnistBuffer(kMaxPixels * sizeof(uint16_t)));
        if (g_inputCanvas == nullptr)
            g_inputCanvas = static_cast<uint8_t *>(allocateMnistBuffer(kMnistImageSize * kMnistImageSize));
        if (g_tensorArena == nullptr)
            g_tensorArena = static_cast<uint8_t *>(allocateMnistBuffer(kTensorArenaSize));

        const bool ready = g_frameBuffer != nullptr &&
                           g_binaryMask != nullptr &&
                           g_selectedMask != nullptr &&
                           g_componentQueue != nullptr &&
                           g_inputCanvas != nullptr &&
                           g_tensorArena != nullptr;

        if (!ready)
        {
            String status = "Khong du bo nho cho MNIST runtime";
            status += " (heap=";
            status += String(ESP.getFreeHeap());
            status += ", psram=";
            status += String(ESP.getFreePsram());
            status += ")";
            status += ".";
            setMnistStatus(ctx, false, -1, 0.0f, status);
        }

        return ready;
    }

    void logMnistLine(AppContext *ctx, const String &host, int digit, float confidence)
    {
        if (ctx != NULL && ctx->serialMutex != NULL && xSemaphoreTake(ctx->serialMutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            Serial.printf("[MNIST] Host: %s | Digit: %d | Confidence: %.4f\n",
                          host.c_str(),
                          digit,
                          confidence);
            xSemaphoreGive(ctx->serialMutex);
            return;
        }

        Serial.printf("[MNIST] Host: %s | Digit: %d | Confidence: %.4f\n",
                      host.c_str(),
                      digit,
                      confidence);
    }

    String readCameraHost(AppContext *ctx)
    {
        if (ctx == NULL || ctx->configMutex == NULL)
            return "";

        String host;
        if (xSemaphoreTake(ctx->configMutex, portMAX_DELAY) == pdTRUE)
        {
            host = ctx->cameraHost;
            xSemaphoreGive(ctx->configMutex);
        }
        return host;
    }

    bool splitHostAndPort(const String &hostWithPort, String &host, uint16_t &port)
    {
        host = hostWithPort;
        port = 80;

        const int colonIndex = hostWithPort.lastIndexOf(':');
        if (colonIndex <= 0 || colonIndex == hostWithPort.length() - 1)
            return !host.isEmpty();

        const String hostPart = hostWithPort.substring(0, colonIndex);
        const String portPart = hostWithPort.substring(colonIndex + 1);
        const int parsedPort = portPart.toInt();
        if (hostPart.isEmpty() || parsedPort <= 0 || parsedPort > 65535)
            return false;

        host = hostPart;
        port = static_cast<uint16_t>(parsedPort);
        return true;
    }

    bool fetchGrayFrame(const String &host)
    {
        String serverHost;
        uint16_t serverPort = 80;
        if (!splitHostAndPort(host, serverHost, serverPort))
            return false;

        WiFiClient wifiClient;
        wifiClient.setTimeout(3500);
        HttpClient http(wifiClient, serverHost, serverPort);

        const int statusCode = http.get("/snapshot_gray");
        if (statusCode != 0)
            return false;

        const int responseCode = http.responseStatusCode();
        if (responseCode != 200)
        {
            http.stop();
            return false;
        }

        if (http.skipResponseHeaders() < 0)
        {
            http.stop();
            return false;
        }

        const long contentLength = http.contentLength();
        if (contentLength != static_cast<long>(kFrameBytes))
        {
            http.stop();
            return false;
        }

        size_t offset = 0;
        while (http.connected() && offset < kFrameBytes)
        {
            const int available = http.available();
            if (available <= 0)
            {
                delay(5);
                continue;
            }

            const size_t toRead = static_cast<size_t>(available);
            const size_t maxReadable = kFrameBytes - offset;
            const size_t chunk = toRead < maxReadable ? toRead : maxReadable;
            const int bytesRead = http.read(g_frameBuffer + offset, chunk);
            if (bytesRead <= 0)
                break;
            offset += static_cast<size_t>(bytesRead);
        }

        http.stop();
        return offset == kFrameBytes;
    }

    float computeMeanRegion(int x0, int y0, int x1, int y1)
    {
        long sum = 0;
        int count = 0;
        for (int y = y0; y < y1; ++y)
        {
            for (int x = x0; x < x1; ++x)
            {
                sum += g_frameBuffer[y * kCameraFrameSize + x];
                ++count;
            }
        }
        return count > 0 ? static_cast<float>(sum) / static_cast<float>(count) : 0.0f;
    }

    float computeBorderMean()
    {
        long sum = 0;
        int count = 0;
        for (int y = 0; y < kCameraFrameSize; ++y)
        {
            for (int x = 0; x < kCameraFrameSize; ++x)
            {
                if (x < 8 || y < 8 || x >= kCameraFrameSize - 8 || y >= kCameraFrameSize - 8)
                {
                    sum += g_frameBuffer[y * kCameraFrameSize + x];
                    ++count;
                }
            }
        }
        return count > 0 ? static_cast<float>(sum) / static_cast<float>(count) : 0.0f;
    }

    DigitBBox locateDigit(bool darkDigitOnBrightBg, uint8_t threshold)
    {
        DigitBBox box{ kCameraFrameSize, kCameraFrameSize, -1, -1, 0 };
        for (int y = 0; y < kCameraFrameSize; ++y)
        {
            for (int x = 0; x < kCameraFrameSize; ++x)
            {
                const uint8_t value = g_frameBuffer[y * kCameraFrameSize + x];
                const bool foreground = darkDigitOnBrightBg ? (value <= threshold) : (value >= threshold);
                if (!foreground)
                    continue;

                if (x < box.minX)
                    box.minX = x;
                if (y < box.minY)
                    box.minY = y;
                if (x > box.maxX)
                    box.maxX = x;
                if (y > box.maxY)
                    box.maxY = y;
                ++box.count;
            }
        }
        return box;
    }

    void clearCanvas()
    {
        if (g_inputCanvas == nullptr)
            return;

        for (int i = 0; i < kMnistImageSize * kMnistImageSize; ++i)
            g_inputCanvas[i] = 0;
    }

    void buildBinaryMask(bool darkDigitOnBrightBg, uint8_t threshold)
    {
        for (int y = 0; y < kCameraFrameSize; ++y)
        {
            for (int x = 0; x < kCameraFrameSize; ++x)
            {
                const uint8_t value = g_frameBuffer[y * kCameraFrameSize + x];
                const bool foreground = darkDigitOnBrightBg ? (value <= threshold) : (value >= threshold);
                g_binaryMask[y * kCameraFrameSize + x] = foreground ? 1 : 0;
                g_selectedMask[y * kCameraFrameSize + x] = 0;
            }
        }
    }

    ComponentStats findBestComponent()
    {
        ComponentStats best{ 0, 0, 0, 0, 0, 0.0f, -1000000.0f, false };
        const float frameCenter = (kCameraFrameSize - 1) * 0.5f;

        for (int y = 0; y < kCameraFrameSize; ++y)
        {
            for (int x = 0; x < kCameraFrameSize; ++x)
            {
                const int startIndex = y * kCameraFrameSize + x;
                if (g_binaryMask[startIndex] == 0)
                    continue;

                int queueHead = 0;
                int queueTail = 0;
                g_componentQueue[queueTail++] = static_cast<uint16_t>(startIndex);
                g_binaryMask[startIndex] = 0;

                ComponentStats current{ x, y, x, y, 0, 0.0f, 0.0f, true };
                long sumX = 0;
                long sumY = 0;

                while (queueHead < queueTail)
                {
                    const int currentIndex = g_componentQueue[queueHead++];
                    const int currentY = currentIndex / kCameraFrameSize;
                    const int currentX = currentIndex % kCameraFrameSize;

                    ++current.count;
                    sumX += currentX;
                    sumY += currentY;

                    if (currentX < current.minX)
                        current.minX = currentX;
                    if (currentY < current.minY)
                        current.minY = currentY;
                    if (currentX > current.maxX)
                        current.maxX = currentX;
                    if (currentY > current.maxY)
                        current.maxY = currentY;

                    if (currentX > 0)
                    {
                        const int leftIndex = currentIndex - 1;
                        if (g_binaryMask[leftIndex] != 0)
                        {
                            g_binaryMask[leftIndex] = 0;
                            g_componentQueue[queueTail++] = static_cast<uint16_t>(leftIndex);
                        }
                    }
                    if (currentX + 1 < kCameraFrameSize)
                    {
                        const int rightIndex = currentIndex + 1;
                        if (g_binaryMask[rightIndex] != 0)
                        {
                            g_binaryMask[rightIndex] = 0;
                            g_componentQueue[queueTail++] = static_cast<uint16_t>(rightIndex);
                        }
                    }
                    if (currentY > 0)
                    {
                        const int upIndex = currentIndex - kCameraFrameSize;
                        if (g_binaryMask[upIndex] != 0)
                        {
                            g_binaryMask[upIndex] = 0;
                            g_componentQueue[queueTail++] = static_cast<uint16_t>(upIndex);
                        }
                    }
                    if (currentY + 1 < kCameraFrameSize)
                    {
                        const int downIndex = currentIndex + kCameraFrameSize;
                        if (g_binaryMask[downIndex] != 0)
                        {
                            g_binaryMask[downIndex] = 0;
                            g_componentQueue[queueTail++] = static_cast<uint16_t>(downIndex);
                        }
                    }
                }

                if (current.count <= 0)
                    continue;

                const float centerX = static_cast<float>(sumX) / static_cast<float>(current.count);
                const float centerY = static_cast<float>(sumY) / static_cast<float>(current.count);
                const float offsetX = fabsf(centerX - frameCenter);
                const float offsetY = fabsf(centerY - frameCenter);
                current.centerOffset = sqrtf(offsetX * offsetX + offsetY * offsetY);
                current.score = static_cast<float>(current.count) - current.centerOffset * 6.5f;

                if (current.score > best.score)
                {
                    best = current;
                    for (int i = 0; i < queueTail; ++i)
                    {
                        g_selectedMask[g_componentQueue[i]] = 1;
                    }
                }
            }
        }

        return best;
    }

    bool prepareMnistCanvas(PreparedDigitStats &stats)
    {
        stats = { 0, 0, 0, 0.0f, 0.0f, false };
        const float borderMean = computeBorderMean();
        const float centerMean = computeMeanRegion(24, 24, 72, 72);
        const bool darkDigitOnBrightBg = centerMean < borderMean;
        int threshold = 0;

        if (darkDigitOnBrightBg)
            threshold = static_cast<int>(borderMean) - 28;
        else
            threshold = static_cast<int>(borderMean) + 28;

        if (threshold < 10)
            threshold = 10;
        if (threshold > 245)
            threshold = 245;

        buildBinaryMask(darkDigitOnBrightBg, static_cast<uint8_t>(threshold));
        const ComponentStats component = findBestComponent();
        if (!component.valid || component.count < 48 || component.maxX <= component.minX || component.maxY <= component.minY)
        {
            clearCanvas();
            return false;
        }

        const int width = component.maxX - component.minX + 1;
        const int height = component.maxY - component.minY + 1;
        const float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
        const float fillRatio = static_cast<float>(component.count) / static_cast<float>(width * height);
        const float centerOffset = component.centerOffset;

        stats.width = width;
        stats.height = height;
        stats.foregroundCount = component.count;
        stats.fillRatio = fillRatio;
        stats.centerOffset = centerOffset;

        const bool sizeOk = width >= 10 && height >= 10 && width <= 84 && height <= 84;
        const bool aspectOk = aspectRatio >= 0.18f && aspectRatio <= 1.85f;
        const bool densityOk = fillRatio >= 0.05f && fillRatio <= 0.60f;
        const bool centeredEnough = centerOffset <= 22.0f;
        if (!(sizeOk && aspectOk && densityOk && centeredEnough))
        {
            clearCanvas();
            return false;
        }

        clearCanvas();
        const float scale = static_cast<float>(kDigitCanvasSize) / static_cast<float>(width > height ? width : height);
        const int scaledWidth = static_cast<int>(width * scale + 0.5f);
        const int scaledHeight = static_cast<int>(height * scale + 0.5f);
        const int offsetX = (kMnistImageSize - scaledWidth) / 2;
        const int offsetY = (kMnistImageSize - scaledHeight) / 2;

        for (int dstY = 0; dstY < scaledHeight; ++dstY)
        {
            const int srcY = component.minY + static_cast<int>((static_cast<float>(dstY) / scale));
            for (int dstX = 0; dstX < scaledWidth; ++dstX)
            {
                const int srcX = component.minX + static_cast<int>((static_cast<float>(dstX) / scale));
                const int srcIndex = srcY * kCameraFrameSize + srcX;
                if (g_selectedMask[srcIndex] == 0)
                    continue;

                const int canvasX = offsetX + dstX;
                const int canvasY = offsetY + dstY;
                if (canvasX >= 0 && canvasX < kMnistImageSize && canvasY >= 0 && canvasY < kMnistImageSize)
                {
                    g_inputCanvas[canvasY * kMnistImageSize + canvasX] = 255;
                }
            }
        }

        stats.valid = true;
        return true;
    }

    bool loadInputTensor(TfLiteTensor *input)
    {
        if (input == nullptr)
            return false;

        const size_t pixelCount = static_cast<size_t>(kMnistImageSize * kMnistImageSize);

        if (input->type == kTfLiteFloat32)
        {
            if (input->bytes < pixelCount * sizeof(float))
                return false;
            for (int i = 0; i < kMnistImageSize * kMnistImageSize; ++i)
                input->data.f[i] = g_inputCanvas[i] > 0 ? 1.0f : 0.0f;
            return true;
        }

        if (input->type == kTfLiteUInt8)
        {
            if (input->bytes < pixelCount)
                return false;
            for (int i = 0; i < kMnistImageSize * kMnistImageSize; ++i)
                input->data.uint8[i] = g_inputCanvas[i];
            return true;
        }

        if (input->type == kTfLiteInt8)
        {
            if (input->bytes < pixelCount)
                return false;

            const float scale = input->params.scale;
            const int zeroPoint = input->params.zero_point;
            if (scale <= 0.0f)
                return false;

            for (int i = 0; i < kMnistImageSize * kMnistImageSize; ++i)
            {
                const float normalized = g_inputCanvas[i] > 0 ? 1.0f : 0.0f;
                int quantized = static_cast<int>(roundf(normalized / scale)) + zeroPoint;
                if (quantized < -128)
                    quantized = -128;
                if (quantized > 127)
                    quantized = 127;
                input->data.int8[i] = static_cast<int8_t>(quantized);
            }
            return true;
        }

        return false;
    }

    bool readOutputTensor(TfLiteTensor *output, int &digit, float &confidence, float &margin)
    {
        if (output == nullptr)
            return false;

        digit = -1;
        confidence = 0.0f;
        float secondBest = -1000000.0f;
        for (int i = 0; i < 10; ++i)
        {
            float value = 0.0f;
            if (output->type == kTfLiteFloat32)
                value = output->data.f[i];
            else if (output->type == kTfLiteUInt8)
                value = static_cast<float>(output->data.uint8[i]) / 255.0f;
            else if (output->type == kTfLiteInt8)
                value = (static_cast<int>(output->data.int8[i]) - output->params.zero_point) * output->params.scale;
            else
                return false;

            if (value > confidence)
            {
                secondBest = confidence;
                confidence = value;
                digit = i;
            }
            else if (value > secondBest)
            {
                secondBest = value;
            }
        }
        margin = confidence - secondBest;
        return digit >= 0;
    }
}

void camera_mnist_task(void *pvParameters)
{
    AppContext *ctx = static_cast<AppContext *>(pvParameters);

    static bool resolverReady = false;
    if (!resolverReady)
    {
        g_resolver.AddConv2D();
        g_resolver.AddMul();
        g_resolver.AddAdd();
        g_resolver.AddMaxPool2D();
        g_resolver.AddReshape();
        g_resolver.AddFullyConnected();
        g_resolver.AddSoftmax();
        resolverReady = true;
    }

    const tflite::Model *mnistModel = tflite::GetModel(model);

    if (mnistModel->version() != TFLITE_SCHEMA_VERSION)
    {
        setMnistStatus(ctx, false, -1, 0.0f, "MNIST schema mismatch.");
        vTaskDelete(NULL);
        return;
    }

    setMnistStatus(ctx, false, -1, 0.0f, "Waiting for camera host.");

    tflite::MicroErrorReporter microErrorReporter;
    tflite::ErrorReporter *errorReporter = &microErrorReporter;
    tflite::MicroInterpreter *interpreter = nullptr;
    TfLiteTensor *input = nullptr;
    TfLiteTensor *output = nullptr;
    bool interpreterReady = false;

    while (1)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            setMnistStatus(ctx, false, -1, 0.0f, "WiFi not connected. ESP32 cannot fetch camera.");
            vTaskDelay(kRetryDelay);
            continue;
        }

        const String host = readCameraHost(ctx);
        if (host.isEmpty())
        {
            setMnistStatus(ctx, false, -1, 0.0f, "Set camera host from dashboard first.");
            vTaskDelay(kRetryDelay);
            continue;
        }

        if (!interpreterReady)
        {
            if (!ensureMnistBuffers(ctx))
            {
                vTaskDelay(kRetryDelay);
                continue;
            }

            interpreter = new tflite::MicroInterpreter(mnistModel, g_resolver, g_tensorArena, kTensorArenaSize, errorReporter);
            if (interpreter == nullptr)
            {
                setMnistStatus(ctx, false, -1, 0.0f, "MNIST interpreter creation failed.");
                vTaskDelay(kRetryDelay);
                continue;
            }

            if (interpreter->AllocateTensors() != kTfLiteOk)
            {
                setMnistStatus(ctx, false, -1, 0.0f, "MNIST AllocateTensors failed.");
                delete interpreter;
                interpreter = nullptr;
                vTaskDelay(kRetryDelay);
                continue;
            }

            input = interpreter->input(0);
            output = interpreter->output(0);
            if (input == nullptr || output == nullptr)
            {
                setMnistStatus(ctx, false, -1, 0.0f, "MNIST tensor binding failed.");
                delete interpreter;
                interpreter = nullptr;
                vTaskDelay(kRetryDelay);
                continue;
            }

            interpreterReady = true;
        }

        if (!fetchGrayFrame(host))
        {
            setMnistStatus(ctx, false, -1, 0.0f, "Cannot fetch /snapshot_gray from camera server.");
            vTaskDelay(kRetryDelay);
            continue;
        }

        PreparedDigitStats stats;
        if (!prepareMnistCanvas(stats))
        {
            setMnistStatus(ctx, false, -1, 0.0f, "Khung hinh chua giong mot chu so ro rang. Hay dua so vao giua khung, nen sach va net dam.");
            vTaskDelay(kCameraPollDelay);
            continue;
        }

        if (!loadInputTensor(input))
        {
            setMnistStatus(ctx, false, -1, 0.0f, "Unsupported MNIST input tensor type.");
            vTaskDelete(NULL);
            return;
        }

        if (interpreter->Invoke() != kTfLiteOk)
        {
            setMnistStatus(ctx, false, -1, 0.0f, "MNIST inference failed.");
            vTaskDelay(kRetryDelay);
            continue;
        }

        int predictedDigit = -1;
        float confidence = 0.0f;
        float margin = 0.0f;
        if (!readOutputTensor(output, predictedDigit, confidence, margin))
        {
            setMnistStatus(ctx, false, -1, 0.0f, "Unsupported MNIST output tensor type.");
            vTaskDelete(NULL);
            return;
        }

        const bool confidenceOk = confidence >= 0.97f;
        const bool marginOk = margin >= 0.18f;
        if (!(confidenceOk && marginOk))
        {
            String rejectStatus = "Model chua du chac chan";
            rejectStatus += " (conf=";
            rejectStatus += String(confidence, 3);
            rejectStatus += ", margin=";
            rejectStatus += String(margin, 3);
            rejectStatus += "). Thu dung nen tron, chi mot chu so va can giua khung.";
            setMnistStatus(ctx, false, -1, confidence, rejectStatus);
            vTaskDelay(kCameraPollDelay);
            continue;
        }

        String status = "Digit ";
        status += String(predictedDigit);
        status += " detected from ";
        status += host;
        status += ".";
        setMnistStatus(ctx, true, predictedDigit, confidence, status);

        logMnistLine(ctx, host, predictedDigit, confidence);

        vTaskDelay(kCameraPollDelay);
    }
}
