/**
 * @file imu_services.hpp
 * @brief IMU (QMI8658) Service Layer
 * @details High-level services built on top of QMI8658 IMU:
 *   - Screen Orientation Detection
 *   - Motion Detection (shake, free-fall, tap)
 *   - Gaming Control (tilt angles)
 *   - Pedometer (step counting)
 */

#pragma once

#include <cmath>
#include <functional>

// qmi8658.h redefines M_PI with a different token (float literal) than
// picolibc's <cmath> (double); undef first to avoid a hard -Werror.
#ifdef M_PI
#undef M_PI
#endif

extern "C" {
#include "qmi8658.h"
}

namespace ImuServices {

// ============================================================================
// SCREEN ORIENTATION SERVICE
// ============================================================================

/**
 * @brief Screen orientation enum
 */
enum class ScreenOrientation {
    PORTRAIT,           // 0°   - Normal vertical
    LANDSCAPE_RIGHT,    // 90°  - Rotated right
    PORTRAIT_INVERTED,  // 180° - Upside down
    LANDSCAPE_LEFT      // 270° - Rotated left
};

/**
 * @brief Get current screen orientation based on accelerometer
 * @param imu QMI8658 device handle
 * @return Current screen orientation
 */
ScreenOrientation getScreenOrientation(qmi8658_dev_t *imu);

/**
 * @brief Orientation change callback type
 */
using OrientationCallback = std::function<void(ScreenOrientation)>;

/**
 * @brief Enable auto-rotation with callback
 * @param imu QMI8658 device handle
 * @param callback Function to call when orientation changes
 * @param pollIntervalMs Polling interval in milliseconds (default: 100ms)
 */
void enableAutoRotation(
    qmi8658_dev_t *imu,
    OrientationCallback callback,
    uint32_t pollIntervalMs = 100
);

/**
 * @brief Disable auto-rotation
 */
void disableAutoRotation();

// ============================================================================
// MOTION DETECTION SERVICE
// ============================================================================

/**
 * @brief Motion event types
 */
enum class MotionType {
    SHAKE,      // Device is being shaken
    FREE_FALL,  // Device is in free fall
    TAP         // Device was tapped
};

/**
 * @brief Motion event structure
 */
struct MotionEvent {
    MotionType type;
    float intensity;  // Intensity of the motion (context-dependent)
};

/**
 * @brief Detect shake motion
 * @param imu QMI8658 device handle
 * @param thresholdRads Threshold in rad/s (default: 3.5 rad/s ≈ 200°/s)
 * @return true if shake detected
 */
bool detectShake(qmi8658_dev_t *imu, float thresholdRads = 3.5);

/**
 * @brief Detect free fall
 * @param imu QMI8658 device handle
 * @param thresholdMps2 Threshold in m/s² (default: 2.9 m/s² ≈ 0.3g)
 * @return true if free fall detected
 */
bool detectFreeFall(qmi8658_dev_t *imu, float thresholdMps2 = 2.9);

/**
 * @brief Motion event callback type
 */
using MotionCallback = std::function<void(const MotionEvent &)>;

/**
 * @brief Set motion event callback
 * @param callback Function to call when motion detected
 */
void setMotionCallback(MotionCallback callback);

/**
 * @brief Start motion detection task
 * @param imu QMI8658 device handle
 * @param pollIntervalMs Polling interval in milliseconds (default: 50ms)
 */
void startMotionDetection(qmi8658_dev_t *imu, uint32_t pollIntervalMs = 50);

/**
 * @brief Stop motion detection task
 */
void stopMotionDetection();

// ============================================================================
// GAMING CONTROL SERVICE
// ============================================================================

/**
 * @brief Tilt angles structure (in degrees)
 */
struct TiltAngles {
    float pitch;  // Forward/backward tilt (-90 to +90°)
    float roll;   // Left/right tilt (-180 to +180°)
};

/**
 * @brief Where gravity points within the plane of the screen
 * @details Turning the board like a dial rotates gravity through the screen
 *          plane, so the angle tracks the turn directly. Lay the board flat
 *          and there is almost no gravity left in that plane, which is what
 *          `strength` reports: below roughly 0.3 the angle is mostly noise
 *          and should be ignored. The board does not need to stand upright,
 *          a modest lean is enough.
 */
struct ScreenRotation {
    float angleDeg;  // 0-360, gravity direction in the screen plane
    float strength;  // 0-1, share of gravity lying in that plane
};

/**
 * @brief Read the in-plane gravity angle
 * @param imu IMU handle
 * @return Angle and how trustworthy it is
 */
ScreenRotation getScreenRotation(qmi8658_dev_t *imu);

/**
 * @brief Get tilt angles from accelerometer
 * @param imu QMI8658 device handle
 * @return Tilt angles in degrees
 */
TiltAngles getTiltAngles(qmi8658_dev_t *imu);

/**
 * @brief Get normalized tilt X (left/right)
 * @param imu QMI8658 device handle
 * @param maxTiltDeg Maximum tilt angle to normalize (default: 45°)
 * @return Normalized value: -1.0 (left) to +1.0 (right)
 */
float getTiltX(qmi8658_dev_t *imu, float maxTiltDeg = 45.0);

/**
 * @brief Get normalized tilt Y (forward/backward)
 * @param imu QMI8658 device handle
 * @param maxTiltDeg Maximum tilt angle to normalize (default: 45°)
 * @return Normalized value: -1.0 (backward) to +1.0 (forward)
 */
float getTiltY(qmi8658_dev_t *imu, float maxTiltDeg = 45.0);

// ============================================================================
// PEDOMETER SERVICE
// ============================================================================

/**
 * @brief Start pedometer (step counting)
 * @param imu QMI8658 device handle
 * @param pollIntervalMs Polling interval in milliseconds (default: 50ms)
 */
void startPedometer(qmi8658_dev_t *imu, uint32_t pollIntervalMs = 50);

/**
 * @brief Stop pedometer
 */
void stopPedometer();

/**
 * @brief Get current step count
 * @return Total steps counted since start
 */
uint32_t getStepCount();

/**
 * @brief Reset step count to zero
 */
void resetStepCount();

/**
 * @brief Get estimated distance walked
 * @param stepLengthM Average step length in meters (default: 0.75m)
 * @return Distance in meters
 */
float getDistance(float stepLengthM = 0.75);

}  // namespace ImuServices
