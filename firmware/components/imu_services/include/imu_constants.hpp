/**
 * @file imu_constants.hpp
 * @brief IMU sensor thresholds and task configuration constants
 */

#pragma once

#include <cstdint>

namespace ImuServices {

/**
 * @brief Pedometer (step counter) configuration
 * @details Tuned for typical human walking/running gait
 */
namespace PedometerConfig {
/**
 * @brief Acceleration threshold for step detection (m/s²)
 * @details 11.0 m/s² ≈ 1.12g
 *          Typical peak acceleration during walking: 1.0-1.5g
 *          This threshold filters out non-step movements
 */
constexpr float STEP_THRESHOLD_MPS2 = 11.0F;

/**
 * @brief Minimum time between consecutive steps (milliseconds)
 * @details 300ms = 2 steps/second maximum (120 BPM running cadence)
 *          Prevents double-counting from accelerometer noise
 */
constexpr uint32_t MIN_STEP_INTERVAL_MS = 300;

/// Task stack size (bytes)
constexpr size_t TASK_STACK_SIZE = 3072;

/// Task priority
constexpr uint8_t TASK_PRIORITY = 4;
}  // namespace PedometerConfig

/**
 * @brief Motion detection thresholds
 */
namespace MotionDetectionConfig {
/**
 * @brief Shake detection threshold (rad/s)
 * @details 3.5 rad/s ≈ 200 degrees/second
 *          Typical phone shake: 150-300 deg/s
 */
constexpr float SHAKE_THRESHOLD_RADS = 3.5F;

/**
 * @brief Free fall detection threshold (m/s²)
 * @details 2.9 m/s² ≈ 0.3g
 *          True free fall: < 0.5g (< 4.9 m/s²)
 *          This threshold accounts for sensor noise
 */
constexpr float FREEFALL_THRESHOLD_MPS2 = 2.9F;

/// Task stack size (bytes)
constexpr size_t TASK_STACK_SIZE = 3072;

/// Task priority
constexpr uint8_t TASK_PRIORITY = 4;
}  // namespace MotionDetectionConfig

/**
 * @brief Screen orientation detection
 */
namespace OrientationConfig {
/**
 * @brief Orientation angle thresholds (degrees)
 * @details Divides 360° into 4 quadrants:
 *          - Portrait: -45° to +45°
 *          - Landscape Right: 45° to 135°
 *          - Portrait Inverted: 135° to 225° (or -135° to -45°)
 *          - Landscape Left: 225° to 315° (or -135° to -225°)
 */
constexpr float ANGLE_45_DEG = 45.0F;
constexpr float ANGLE_135_DEG = 135.0F;

/// Auto-rotation task stack size (bytes)
constexpr size_t TASK_STACK_SIZE = 3072;

/// Auto-rotation task priority
constexpr uint8_t TASK_PRIORITY = 3;
}  // namespace OrientationConfig

/**
 * @brief Gaming control mapping
 */
namespace GamingControlConfig {
/**
 * @brief Accelerometer range normalization
 * @details These values define the clamping range for tilt-based controls.
 *          Accelerometer output is clamped and normalized to this range.
 */
namespace AccelRange {
/// Minimum expected acceleration value (arbitrary units)
constexpr int MIN = -100;

/// Maximum expected acceleration value (arbitrary units)
constexpr int MAX = 100;

/// Range span
constexpr int SPAN = MAX - MIN;
}  // namespace AccelRange
}  // namespace GamingControlConfig

}  // namespace ImuServices
