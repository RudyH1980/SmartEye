/**
 * @file screen_orientation.cpp
 * @brief Screen Orientation Detection Service
 */

#include <cmath>
#include <utility>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "imu_constants.hpp"
#include "imu_services.hpp"

constexpr static const char *TAG = "ScreenOrientation";

namespace ImuServices {

using namespace ImuServices;

// Static variables for auto-rotation task
static TaskHandle_t sAutoRotationTask = nullptr;
static OrientationCallback sOrientationCallback = nullptr;
static qmi8658_dev_t *sImuDevice = nullptr;
static ScreenOrientation sLastOrientation = ScreenOrientation::PORTRAIT;

/**
 * @brief Get screen orientation from accelerometer data
 */
ScreenOrientation getScreenOrientation(qmi8658_dev_t *imu) {
    if (imu == nullptr) {
        return ScreenOrientation::PORTRAIT;
    }

    float accelX;
    float accelY;
    float accelZ;
    esp_err_t ret = qmi8658_read_accel(imu, &accelX, &accelY, &accelZ);

    if (ret != ESP_OK) {
        return sLastOrientation;  // Return last known orientation on error
    }

    // Calculate roll angle (in degrees)
    // roll = atan2(accelY, accelZ) * 180/PI
    float roll = std::atan2(accelY, accelZ) * 180.0 / M_PI;

    // Determine orientation based on pitch and roll
    // Portrait: pitch ≈ 0°, roll ≈ 0°
    // Landscape Right: pitch ≈ 0°, roll ≈ 90°
    // Portrait Inverted: pitch ≈ 0°, roll ≈ ±180°
    // Landscape Left: pitch ≈ 0°, roll ≈ -90°

    ScreenOrientation orientation;

    if (std::fabs(roll) < OrientationConfig::ANGLE_45_DEG) {
        // Roll close to 0° → Portrait
        orientation = ScreenOrientation::PORTRAIT;
    } else if (roll >= OrientationConfig::ANGLE_45_DEG && roll < OrientationConfig::ANGLE_135_DEG) {
        // Roll 45° to 135° → Landscape Right
        orientation = ScreenOrientation::LANDSCAPE_RIGHT;
    } else if (std::fabs(roll) >= OrientationConfig::ANGLE_135_DEG) {
        // Roll ±135° to ±180° → Portrait Inverted
        orientation = ScreenOrientation::PORTRAIT_INVERTED;
    } else {
        // Roll -135° to -45° → Landscape Left
        orientation = ScreenOrientation::LANDSCAPE_LEFT;
    }

    return orientation;
}

/**
 * @brief Auto-rotation task (FreeRTOS task)
 */
static void autoRotationTask(void *pvParameters) {
    uint32_t pollIntervalMs = *(uint32_t *)pvParameters;
    free(pvParameters);  // Free allocated interval parameter

    ESP_LOGI(TAG, "Auto-rotation task started (poll interval: %lu ms)", pollIntervalMs);

    while (true) {
        ScreenOrientation currentOrientation = getScreenOrientation(sImuDevice);

        // Notify callback if orientation changed
        if (currentOrientation != sLastOrientation) {
            sLastOrientation = currentOrientation;

            if (sOrientationCallback) {
                sOrientationCallback(currentOrientation);
            }

            const char *orientationName = "Unknown";
            switch (currentOrientation) {
                case ScreenOrientation::PORTRAIT:
                    orientationName = "Portrait";
                    break;
                case ScreenOrientation::LANDSCAPE_RIGHT:
                    orientationName = "Landscape Right";
                    break;
                case ScreenOrientation::PORTRAIT_INVERTED:
                    orientationName = "Portrait Inverted";
                    break;
                case ScreenOrientation::LANDSCAPE_LEFT:
                    orientationName = "Landscape Left";
                    break;
            }
            ESP_LOGI(TAG, "Orientation changed: %s", orientationName);
        }

        vTaskDelay(pdMS_TO_TICKS(pollIntervalMs));
    }
}

void enableAutoRotation(qmi8658_dev_t *imu, OrientationCallback callback, uint32_t pollIntervalMs) {
    if (sAutoRotationTask != nullptr) {
        ESP_LOGW(TAG, "Auto-rotation already enabled. Disabling first.");
        disableAutoRotation();
    }

    sImuDevice = imu;
    sOrientationCallback = std::move(callback);
    sLastOrientation = getScreenOrientation(imu);  // Initialize with current orientation

    // Allocate interval parameter (will be freed in task)
    auto *intervalParam = (uint32_t *)malloc(sizeof(uint32_t));
    *intervalParam = pollIntervalMs;

    // Create FreeRTOS task
    BaseType_t ret = xTaskCreate(
        autoRotationTask,
        "AutoRotation",
        OrientationConfig::TASK_STACK_SIZE,  // Stack size
        intervalParam,
        OrientationConfig::TASK_PRIORITY,  // Priority
        &sAutoRotationTask
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create auto-rotation task");
        free(intervalParam);
        sAutoRotationTask = nullptr;
    }
}

void disableAutoRotation() {
    if (sAutoRotationTask != nullptr) {
        vTaskDelete(sAutoRotationTask);
        sAutoRotationTask = nullptr;
        sOrientationCallback = nullptr;
        sImuDevice = nullptr;
        ESP_LOGI(TAG, "Auto-rotation disabled");
    }
}

}  // namespace ImuServices
