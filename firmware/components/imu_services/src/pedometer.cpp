/**
 * @file pedometer.cpp
 * @brief Pedometer Service (Step counting)
 */

#include <cmath>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "imu_constants.hpp"
#include "imu_services.hpp"

constexpr static const char *TAG = "Pedometer";

namespace ImuServices {

using namespace ImuServices;

static TaskHandle_t sPedometerTask = nullptr;
static qmi8658_dev_t *sImuDevice = nullptr;
static uint32_t sStepCount = 0;
static float sLastAccelMagnitude = 0.0;
static uint32_t sLastStepTime = 0;

static void pedometerTask(void *pvParameters) {
    uint32_t pollIntervalMs = *(uint32_t *)pvParameters;
    free(pvParameters);

    ESP_LOGI(TAG, "Pedometer task started (poll interval: %lu ms)", pollIntervalMs);

    while (true) {
        float accelX;
        float accelY;
        float accelZ;
        if (qmi8658_read_accel(sImuDevice, &accelX, &accelY, &accelZ) == ESP_OK) {
            // Calculate acceleration magnitude
            float magnitude = std::sqrt(accelX * accelX + accelY * accelY + accelZ * accelZ);

            // Peak detection algorithm:
            // Step detected when magnitude crosses threshold upward
            if (sLastAccelMagnitude < PedometerConfig::STEP_THRESHOLD_MPS2 &&
                magnitude >= PedometerConfig::STEP_THRESHOLD_MPS2) {
                uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

                // Debounce: ensure minimum interval between steps
                if (now - sLastStepTime >= PedometerConfig::MIN_STEP_INTERVAL_MS) {
                    sStepCount++;
                    sLastStepTime = now;
                    ESP_LOGI(TAG, "Step detected! Total: %lu", sStepCount);
                }
            }

            sLastAccelMagnitude = magnitude;
        }

        vTaskDelay(pdMS_TO_TICKS(pollIntervalMs));
    }
}

void startPedometer(qmi8658_dev_t *imu, uint32_t pollIntervalMs) {
    if (sPedometerTask != nullptr) {
        ESP_LOGW(TAG, "Pedometer already running");
        return;
    }

    sImuDevice = imu;
    sStepCount = 0;
    sLastAccelMagnitude = 0.0;
    sLastStepTime = 0;

    auto *intervalParam = (uint32_t *)malloc(sizeof(uint32_t));
    *intervalParam = pollIntervalMs;

    BaseType_t
        ret = xTaskCreate(pedometerTask, "Pedometer", 3072, intervalParam, 5, &sPedometerTask);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create pedometer task");
        free(intervalParam);
        sPedometerTask = nullptr;
    }
}

void stopPedometer() {
    if (sPedometerTask != nullptr) {
        vTaskDelete(sPedometerTask);
        sPedometerTask = nullptr;
        ESP_LOGI(TAG, "Pedometer stopped. Total steps: %lu", sStepCount);
    }
}

uint32_t getStepCount() {
    return sStepCount;
}

void resetStepCount() {
    sStepCount = 0;
    ESP_LOGI(TAG, "Step count reset to 0");
}

float getDistance(float stepLengthM) {
    return sStepCount * stepLengthM;
}

}  // namespace ImuServices
