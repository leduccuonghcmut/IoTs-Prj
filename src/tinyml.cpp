#include "tinyml.h"

#include <math.h>

namespace
{
    constexpr int kTensorArenaSize = 8 * 1024;
    constexpr size_t kRollingWindowSize = 5;
    constexpr float kSensorValueUnset = 0.0f;
    constexpr TickType_t kInferenceDelay = pdMS_TO_TICKS(5000);

    constexpr float kScalerTempMin = 15.0000f;
    constexpr float kScalerTempMax = 58.3800f;
    constexpr float kScalerHumidityMin = 5.6600f;
    constexpr float kScalerHumidityMax = 95.0000f;
    constexpr float kScalerDeltaTempMin = -13.8400f;
    constexpr float kScalerDeltaTempMax = 13.7600f;
    constexpr float kScalerDeltaHumidityMin = -34.5200f;
    constexpr float kScalerDeltaHumidityMax = 33.8100f;
    constexpr float kScalerRollingMeanTempMin = 15.3700f;
    constexpr float kScalerRollingMeanTempMax = 57.7500f;
    constexpr float kScalerRollingStdTempMin = 0.0000f;
    constexpr float kScalerRollingStdTempMax = 17.0900f;

    static uint8_t g_tensorArena[kTensorArenaSize];
    static float g_tempWindow[kRollingWindowSize] = {0.0f};
    static size_t g_windowCount = 0;
    static size_t g_windowIndex = 0;
    static bool g_hasPreviousSample = false;
    static float g_previousTemperature = 0.0f;
    static float g_previousHumidity = 0.0f;

    float normalizeMinMax(float value, float minValue, float maxValue)
    {
        if (maxValue <= minValue)
            return 0.0f;
        return (value - minValue) / (maxValue - minValue);
    }

    TinyMLState classifyTinyMLStateByLabel(int label)
    {
        switch (label)
        {
        case 1:
            return TINYML_WARNING;
        case 2:
            return TINYML_ANOMALY;
        case 0:
        default:
            return TINYML_NORMAL;
        }
    }

    int argMax(const float *values, int count)
    {
        if (values == nullptr || count <= 0)
            return 0;

        int bestIndex = 0;
        float bestValue = values[0];
        for (int index = 1; index < count; ++index)
        {
            if (values[index] > bestValue)
            {
                bestValue = values[index];
                bestIndex = index;
            }
        }
        return bestIndex;
    }

    const char *tinyMLStateToString(TinyMLState state)
    {
        switch (state)
        {
        case TINYML_NORMAL:
            return "NORMAL";
        case TINYML_WARNING:
            return "WARNING";
        case TINYML_ANOMALY:
            return "ANOMALY";
        case TINYML_IDLE:
        default:
            return "IDLE";
        }
    }

    TempLevel toTempLevel(TinyMLState state)
    {
        switch (state)
        {
        case TINYML_ANOMALY:
            return TEMP_CRITICAL;
        case TINYML_WARNING:
            return TEMP_WARNING;
        case TINYML_NORMAL:
        default:
            return TEMP_NORMAL;
        }
    }

    void logTinyMlLine(AppContext *ctx, float temperature, float humidity, float score, TinyMLState state)
    {
        if (ctx != NULL && ctx->serialMutex != NULL && xSemaphoreTake(ctx->serialMutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            Serial.printf("[TinyML] Temp: %.1f C | Humi: %.1f %% | Score: %.6f | State: %s\n",
                          temperature,
                          humidity,
                          score,
                          tinyMLStateToString(state));
            xSemaphoreGive(ctx->serialMutex);
            return;
        }

        Serial.printf("[TinyML] Temp: %.1f C | Humi: %.1f %% | Score: %.6f | State: %s\n",
                      temperature,
                      humidity,
                      score,
                      tinyMLStateToString(state));
    }

    void updateRollingWindow(float temperature)
    {
        g_tempWindow[g_windowIndex] = temperature;
        g_windowIndex = (g_windowIndex + 1) % kRollingWindowSize;
        if (g_windowCount < kRollingWindowSize)
            ++g_windowCount;
    }

    void computeRollingStats(float &meanTemperature, float &stdTemperature)
    {
        if (g_windowCount == 0)
        {
            meanTemperature = 0.0f;
            stdTemperature = 0.0f;
            return;
        }

        float sum = 0.0f;
        for (size_t index = 0; index < g_windowCount; ++index)
            sum += g_tempWindow[index];

        meanTemperature = sum / static_cast<float>(g_windowCount);

        float variance = 0.0f;
        for (size_t index = 0; index < g_windowCount; ++index)
        {
            const float diff = g_tempWindow[index] - meanTemperature;
            variance += diff * diff;
        }

        variance /= static_cast<float>(g_windowCount);
        stdTemperature = sqrtf(variance < 0.0f ? 0.0f : variance);
    }

    void buildModelInput(float temperature, float humidity, float *inputData, size_t inputCount)
    {
        if (inputData == nullptr || inputCount == 0)
            return;

        if (inputCount == 2)
        {
            inputData[0] = temperature;
            inputData[1] = humidity;
            return;
        }

        const float deltaTemperature = g_hasPreviousSample ? (temperature - g_previousTemperature) : 0.0f;
        const float deltaHumidity = g_hasPreviousSample ? (humidity - g_previousHumidity) : 0.0f;

        updateRollingWindow(temperature);

        float rollingMeanTemperature = temperature;
        float rollingStdTemperature = 0.0f;
        computeRollingStats(rollingMeanTemperature, rollingStdTemperature);

        inputData[0] = normalizeMinMax(temperature, kScalerTempMin, kScalerTempMax);
        inputData[1] = normalizeMinMax(humidity, kScalerHumidityMin, kScalerHumidityMax);
        inputData[2] = normalizeMinMax(deltaTemperature, kScalerDeltaTempMin, kScalerDeltaTempMax);
        inputData[3] = normalizeMinMax(deltaHumidity, kScalerDeltaHumidityMin, kScalerDeltaHumidityMax);
        inputData[4] = normalizeMinMax(rollingMeanTemperature, kScalerRollingMeanTempMin, kScalerRollingMeanTempMax);
        inputData[5] = normalizeMinMax(rollingStdTemperature, kScalerRollingStdTempMin, kScalerRollingStdTempMax);

        g_previousTemperature = temperature;
        g_previousHumidity = humidity;
        g_hasPreviousSample = true;
    }

    TinyMLState classifyFromOutput(const TfLiteTensor *output, float &score)
    {
        score = 0.0f;
        if (output == nullptr || output->data.f == nullptr)
            return TINYML_IDLE;

        const int outputCount = output->bytes / static_cast<int>(sizeof(float));
        if (outputCount <= 0)
            return TINYML_IDLE;

        if (outputCount == 1)
        {
            score = output->data.f[0];
            if (score >= 0.80f)
                return TINYML_ANOMALY;
            if (score >= 0.55f)
                return TINYML_WARNING;
            return TINYML_NORMAL;
        }

        const int label = argMax(output->data.f, outputCount);
        score = output->data.f[label];
        return classifyTinyMLStateByLabel(label);
    }

}

void tiny_ml_task(void *pvParameters)
{
    AppContext *ctx = static_cast<AppContext *>(pvParameters);
    tflite::MicroErrorReporter microErrorReporter;
    tflite::ErrorReporter *errorReporter = &microErrorReporter;
    const tflite::Model *model = tflite::GetModel(dht_anomaly_model_tflite);
    tflite::AllOpsResolver resolver;
    tflite::MicroInterpreter interpreter(model, resolver, g_tensorArena, kTensorArenaSize, errorReporter);

    if (model->version() != TFLITE_SCHEMA_VERSION)
    {
        errorReporter->Report("Model schema mismatch");
        vTaskDelete(NULL);
        return;
    }

    if (interpreter.AllocateTensors() != kTfLiteOk)
    {
        errorReporter->Report("AllocateTensors() failed");
        vTaskDelete(NULL);
        return;
    }

    TfLiteTensor *input = interpreter.input(0);
    TfLiteTensor *output = interpreter.output(0);
    if (input == nullptr || output == nullptr)
    {
        errorReporter->Report("Input or output tensor is null");
        vTaskDelete(NULL);
        return;
    }

    const size_t inputCount = static_cast<size_t>(input->bytes / sizeof(float));

    while (1)
    {
        float temperature = 0.0f;
        float humidity = 0.0f;
        if (ctx != NULL && ctx->stateMutex != NULL && xSemaphoreTake(ctx->stateMutex, portMAX_DELAY) == pdTRUE)
        {
            temperature = ctx->temperature;
            humidity = ctx->humidity;
            xSemaphoreGive(ctx->stateMutex);
        }

        if (temperature == kSensorValueUnset && humidity == kSensorValueUnset)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        float inputData[6] = {0.0f};
        buildModelInput(temperature, humidity, inputData, inputCount);
        for (size_t index = 0; index < inputCount && index < 6; ++index)
            input->data.f[index] = inputData[index];

        if (interpreter.Invoke() != kTfLiteOk)
        {
            errorReporter->Report("Invoke failed");
            vTaskDelay(kInferenceDelay);
            continue;
        }

        float result = 0.0f;
        const TinyMLState newState = classifyFromOutput(output, result);
        float probNormal = 0.0f;
        float probThreshold = 0.0f;
        float probSpike = 0.0f;

        if (output != nullptr && output->data.f != nullptr)
        {
            const int outputCount = output->bytes / static_cast<int>(sizeof(float));
            if (outputCount >= 3)
            {
                probNormal = output->data.f[0];
                probThreshold = output->data.f[1];
                probSpike = output->data.f[2];
            }
            else if (outputCount == 1)
            {
                probSpike = output->data.f[0];
                probNormal = 1.0f - probSpike;
                probThreshold = 0.0f;
            }
        }

        if (ctx != NULL && ctx->stateMutex != NULL && xSemaphoreTake(ctx->stateMutex, portMAX_DELAY) == pdTRUE)
        {
            ctx->tinymlScore = result;
            ctx->tinymlProbNormal = probNormal;
            ctx->tinymlProbThreshold = probThreshold;
            ctx->tinymlProbSpike = probSpike;
            ctx->tinymlReady = true;
            ctx->tinymlState = newState;
            ctx->tempLevel = toTempLevel(newState);
            xSemaphoreGive(ctx->stateMutex);
        }

        if (ctx != NULL && ctx->ledTempSemaphore != NULL)
            xSemaphoreGive(ctx->ledTempSemaphore);

        logTinyMlLine(ctx, temperature, humidity, result, newState);

        vTaskDelay(kInferenceDelay);
    }
}
