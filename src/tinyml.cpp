#include "tinyml.h"

namespace
{
    constexpr int kTensorArenaSize = 8 * 1024;
    constexpr float kSensorValueUnset = 0.0f;
    constexpr TickType_t kInferenceDelay = pdMS_TO_TICKS(5000);
    static uint8_t g_tensorArena[kTensorArenaSize];

    TinyMLState classifyTinyMLState(float score)
    {
        if (score >= 0.80f)
            return TINYML_ANOMALY;
        if (score >= 0.55f)
            return TINYML_WARNING;
        return TINYML_NORMAL;
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

        input->data.f[0] = temperature;
        input->data.f[1] = humidity;
        if (interpreter.Invoke() != kTfLiteOk)
        {
            errorReporter->Report("Invoke failed");
            vTaskDelay(kInferenceDelay);
            continue;
        }

        const float result = output->data.f[0];
        const TinyMLState newState = classifyTinyMLState(result);
        if (ctx != NULL && ctx->stateMutex != NULL && xSemaphoreTake(ctx->stateMutex, portMAX_DELAY) == pdTRUE)
        {
            ctx->tinymlScore = result;
            ctx->tinymlReady = true;
            ctx->tinymlState = newState;
            ctx->tempLevel = toTempLevel(newState);
            xSemaphoreGive(ctx->stateMutex);
        }

        if (ctx != NULL && ctx->ledTempSemaphore != NULL)
            xSemaphoreGive(ctx->ledTempSemaphore);

        Serial.print("[TinyML] Temp: ");
        Serial.print(temperature, 1);
        Serial.print(" C | Humi: ");
        Serial.print(humidity, 1);
        Serial.print(" % | Score: ");
        Serial.print(result, 6);
        Serial.print(" | State: ");
        Serial.println(tinyMLStateToString(newState));

        vTaskDelay(kInferenceDelay);
    }
}
