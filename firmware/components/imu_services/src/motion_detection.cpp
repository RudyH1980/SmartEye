/**
 * @file motion_detection.cpp
 * @brief Motion Detection Service (Shake, Free-fall)
 */

#include <cmath>
#include <utility>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "imu_constants.hpp"
#include "imu_services.hpp"

constexpr static const char *TAG = "MotionDetection";

namespace ImuServices {

using namespace ImuServices;

static TaskHandle_t sMotionTask = nullptr;
static MotionCallback sMotionCallback = nullptr;
static qmi8658_dev_t *sImuDevice = nullptr;

bool detectShake(qmi8658_dev_t *imu, float thresholdRads) {
    if (imu == nullptr) {
        return false;
    }

    float gyroX;
    float gyroY;
    float gyroZ;
    if (qmi8658_read_gyro(imu, &gyroX, &gyroY, &gyroZ) != ESP_OK) {
        return false;
    }

    // Check if any axis exceeds threshold
    return (
        std::fabs(gyroX) > thresholdRads || std::fabs(gyroY) > thresholdRads ||
        std::fabs(gyroZ) > thresholdRads
    );
}

bool detectFreeFall(qmi8658_dev_t *imu, float thresholdMps2) {
    if (imu == nullptr) {
        return false;
    }

    float accelX;
    float accelY;
    float accelZ;
    if (qmi8658_read_accel(imu, &accelX, &accelY, &accelZ) != ESP_OK) {
        return false;
    }

    // Calculate acceleration magnitude
    float magnitude = std::sqrt(accelX * accelX + accelY * accelY + accelZ * accelZ);

    // Free fall: magnitude close to 0 (gravity ~9.8 m/s²)
    return (magnitude < thresholdMps2);
}

static void motionDetectionTask(void *pvParameters) {
    uint32_t pollIntervalMs = *(uint32_t *)pvParameters;
    free(pvParameters);

    ESP_LOGI(TAG, "Motion detection task started (poll interval: %lu ms)", pollIntervalMs);

    while (true) {
        // Check shake
        if (detectShake(sImuDevice, MotionDetectionConfig::SHAKE_THRESHOLD_RADS)) {
            if (sMotionCallback) {
                float gyroX;
                float gyroY;
                float gyroZ;
                qmi8658_read_gyro(sImuDevice, &gyroX, &gyroY, &gyroZ);
                float maxGyro = fmax(std::fabs(gyroX), fmax(std::fabs(gyroY), std::fabs(gyroZ)));
                sMotionCallback({MotionType::SHAKE, maxGyro});
            }
        }

        // Check free fall
        if (detectFreeFall(sImuDevice, MotionDetectionConfig::FREEFALL_THRESHOLD_MPS2)) {
            if (sMotionCallback) {
                sMotionCallback({MotionType::FREE_FALL, 0.0});
            }
        }

        vTaskDelay(pdMS_TO_TICKS(pollIntervalMs));
    }
}

void setMotionCallback(MotionCallback callback) {
    sMotionCallback = std::move(callback);
}

void startMotionDetection(qmi8658_dev_t *imu, uint32_t pollIntervalMs) {
    if (sMotionTask != nullptr) {
        ESP_LOGW(TAG, "Motion detection already running");
        return;
    }

    sImuDevice = imu;

    auto *intervalParam = (uint32_t *)malloc(sizeof(uint32_t));
    *intervalParam = pollIntervalMs;

    BaseType_t ret = xTaskCreate(
        motionDetectionTask,
        "MotionDetect",
        MotionDetectionConfig::TASK_STACK_SIZE,
        intervalParam,
        MotionDetectionConfig::TASK_PRIORITY,
        &sMotionTask
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create motion detection task");
        free(intervalParam);
        sMotionTask = nullptr;
    }
}

void stopMotionDetection() {
    if (sMotionTask != nullptr) {
        vTaskDelete(sMotionTask);
        sMotionTask = nullptr;
        ESP_LOGI(TAG, "Motion detection stopped");
    }
}

}  // namespace ImuServices
