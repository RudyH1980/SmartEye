/**
 * @file gaming_control.cpp
 * @brief Gaming Control Service (Tilt detection)
 */

#include <cmath>

#include <cmath>
#include "esp_log.h"
#include "imu_services.hpp"

constexpr static const char *TAG = "GamingControl";

namespace ImuServices {

ScreenRotation getScreenRotation(qmi8658_dev_t *imu) {
    ScreenRotation rotation = {0.0F, 0.0F};

    if (imu == nullptr) {
        return rotation;
    }

    float accelX = NAN;
    float accelY = NAN;
    float accelZ = NAN;
    if (qmi8658_read_accel(imu, &accelX, &accelY, &accelZ) != ESP_OK) {
        return rotation;
    }

    // Only the component of gravity lying in the screen plane says anything
    // about how far the board has been turned; the axis through the screen
    // (Z) just tells us how much it is leaning.
    const float inPlane = std::sqrt((accelX * accelX) + (accelY * accelY));
    constexpr float ONE_G_MPS2 = 9.807F;

    rotation.strength = inPlane / ONE_G_MPS2;
    if (rotation.strength > 1.0F) {
        rotation.strength = 1.0F;
    }

    float degrees = std::atan2(accelY, accelX) * 180.0F / static_cast<float>(M_PI);
    if (degrees < 0.0F) {
        degrees += 360.0F;
    }
    rotation.angleDeg = degrees;

    return rotation;
}

TiltAngles getTiltAngles(qmi8658_dev_t *imu) {
    TiltAngles angles = {0.0, 0.0};

    if (imu == nullptr) {
        return angles;
    }

    float accelX = NAN;
    float accelY = NAN;
    float accelZ = NAN;
    if (qmi8658_read_accel(imu, &accelX, &accelY, &accelZ) != ESP_OK) {
        return angles;
    }

    // Calculate pitch and roll in degrees
    angles.pitch = atan2(-accelX, sqrt(accelY * accelY + accelZ * accelZ)) * 180.0 / M_PI;
    angles.roll = atan2(accelY, accelZ) * 180.0 / M_PI;

    return angles;
}

float getTiltX(qmi8658_dev_t *imu, float maxTiltDeg) {
    TiltAngles angles = getTiltAngles(imu);

    // Roll: -180 to +180°
    // Normalize to -1.0 (left) to +1.0 (right)
    float normalized = angles.roll / maxTiltDeg;

    // Clamp to [-1.0, 1.0]
    if (normalized < -1.0) {
        normalized = -1.0;
    }
    if (normalized > 1.0) {
        normalized = 1.0;
    }

    return normalized;
}

float getTiltY(qmi8658_dev_t *imu, float maxTiltDeg) {
    TiltAngles angles = getTiltAngles(imu);

    // Pitch: -90 to +90°
    // Normalize to -1.0 (backward) to +1.0 (forward)
    float normalized = angles.pitch / maxTiltDeg;

    // Clamp to [-1.0, 1.0]
    if (normalized < -1.0) {
        normalized = -1.0;
    }
    if (normalized > 1.0) {
        normalized = 1.0;
    }

    return normalized;
}

}  // namespace ImuServices
