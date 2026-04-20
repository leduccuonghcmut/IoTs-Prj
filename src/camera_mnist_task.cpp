#include "camera_mnist_task.h"

#include <ArduinoHttpClient.h>
#include <WiFi.h>

#include "TinyML_MNIST.h"
#include "global.h"

#include <TensorFlowLite_ESP32.h>
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace
{
    constexpr int kCameraFrameSize = 96;
    constexpr int kMnistImageSize = 28;
    constexpr int kDigitCanvasSize = 20;
    constexpr size_t kFrameBytes = kCameraFrameSize * kCameraFrameSize;
    constexpr int kTensorArenaSize = 60 * 1024;
    constexpr TickType_t kCameraPollDelay = pdMS_TO_TICKS(2000);
    constexpr TickType_t kRetryDelay = pdMS_TO_TICKS(3000);

    static uint8_t g_frameBuffer[kFrameBytes];
    static float g_inputCanvas[kMnistImageSize * kMnistImageSize];
    static uint8_t g_tensorArena[kTensorArenaSize];
    static tflite::AllOpsResolver g_resolver;

    struct DigitBBox
    {
        int minX;
        int minY;
        int maxX;
        int maxY;
        int count;
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

    bool prepareMnistCanvas()
    {
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

        const DigitBBox box = locateDigit(darkDigitOnBrightBg, static_cast<uint8_t>(threshold));
        if (box.count < 40 || box.maxX <= box.minX || box.maxY <= box.minY)
        {
            for (float &value : g_inputCanvas)
                value = 0.0f;
            return false;
        }

        const int width = box.maxX - box.minX + 1;
        const int height = box.maxY - box.minY + 1;
        const float scaleX = static_cast<float>(width) / static_cast<float>(kDigitCanvasSize);
        const float scaleY = static_cast<float>(height) / static_cast<float>(kDigitCanvasSize);

        for (float &value : g_inputCanvas)
            value = 0.0f;

        for (int dstY = 0; dstY < kDigitCanvasSize; ++dstY)
        {
            const int srcY = box.minY + static_cast<int>(dstY * scaleY);
            for (int dstX = 0; dstX < kDigitCanvasSize; ++dstX)
            {
                const int srcX = box.minX + static_cast<int>(dstX * scaleX);
                const uint8_t pixel = g_frameBuffer[srcY * kCameraFrameSize + srcX];
                const bool foreground = darkDigitOnBrightBg ? (pixel <= threshold) : (pixel >= threshold);
                const int canvasX = dstX + 4;
                const int canvasY = dstY + 4;
                g_inputCanvas[canvasY * kMnistImageSize + canvasX] = foreground ? 1.0f : 0.0f;
            }
        }

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
                input->data.f[i] = g_inputCanvas[i];
            return true;
        }

        if (input->type == kTfLiteUInt8)
        {
            if (input->bytes < pixelCount)
                return false;
            for (int i = 0; i < kMnistImageSize * kMnistImageSize; ++i)
                input->data.uint8[i] = static_cast<uint8_t>(g_inputCanvas[i] * 255.0f);
            return true;
        }

        return false;
    }

    bool readOutputTensor(TfLiteTensor *output, int &digit, float &confidence)
    {
        if (output == nullptr)
            return false;

        digit = -1;
        confidence = 0.0f;
        for (int i = 0; i < 10; ++i)
        {
            float value = 0.0f;
            if (output->type == kTfLiteFloat32)
                value = output->data.f[i];
            else if (output->type == kTfLiteUInt8)
                value = static_cast<float>(output->data.uint8[i]) / 255.0f;
            else
                return false;

            if (value > confidence)
            {
                confidence = value;
                digit = i;
            }
        }
        return digit >= 0;
    }
}

void camera_mnist_task(void *pvParameters)
{
    AppContext *ctx = static_cast<AppContext *>(pvParameters);

    tflite::MicroErrorReporter microErrorReporter;
    tflite::ErrorReporter *errorReporter = &microErrorReporter;
    const tflite::Model *mnistModel = tflite::GetModel(model);
    tflite::MicroInterpreter interpreter(mnistModel, g_resolver, g_tensorArena, kTensorArenaSize, errorReporter);

    if (mnistModel->version() != TFLITE_SCHEMA_VERSION)
    {
        setMnistStatus(ctx, false, -1, 0.0f, "MNIST schema mismatch.");
        vTaskDelete(NULL);
        return;
    }

    if (interpreter.AllocateTensors() != kTfLiteOk)
    {
        setMnistStatus(ctx, false, -1, 0.0f, "MNIST AllocateTensors failed.");
        vTaskDelete(NULL);
        return;
    }

    TfLiteTensor *input = interpreter.input(0);
    TfLiteTensor *output = interpreter.output(0);
    if (input == nullptr || output == nullptr)
    {
        setMnistStatus(ctx, false, -1, 0.0f, "MNIST tensor binding failed.");
        vTaskDelete(NULL);
        return;
    }

    setMnistStatus(ctx, false, -1, 0.0f, "Waiting for camera host.");

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

        if (!fetchGrayFrame(host))
        {
            setMnistStatus(ctx, false, -1, 0.0f, "Cannot fetch /snapshot_gray from camera server.");
            vTaskDelay(kRetryDelay);
            continue;
        }

        if (!prepareMnistCanvas())
        {
            setMnistStatus(ctx, false, -1, 0.0f, "No clear digit found in the current frame.");
            vTaskDelay(kCameraPollDelay);
            continue;
        }

        if (!loadInputTensor(input))
        {
            setMnistStatus(ctx, false, -1, 0.0f, "Unsupported MNIST input tensor type.");
            vTaskDelete(NULL);
            return;
        }

        if (interpreter.Invoke() != kTfLiteOk)
        {
            setMnistStatus(ctx, false, -1, 0.0f, "MNIST inference failed.");
            vTaskDelay(kRetryDelay);
            continue;
        }

        int predictedDigit = -1;
        float confidence = 0.0f;
        if (!readOutputTensor(output, predictedDigit, confidence))
        {
            setMnistStatus(ctx, false, -1, 0.0f, "Unsupported MNIST output tensor type.");
            vTaskDelete(NULL);
            return;
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
