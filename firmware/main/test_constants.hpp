/**
 * @file test_constants.hpp
 * @brief Test demo configuration constants
 */

#pragma once

#include <cstdint>

namespace tests {

/**
 * @brief Touch drawing test configuration
 */
namespace TouchDrawingConfig {
/// Drawing dot size (pixels)
constexpr int DOT_SIZE = 6;

/// Touch polling interval when touch is active (milliseconds)
constexpr uint32_t ACTIVE_POLL_MS = 2;

/// Touch polling interval when idle (milliseconds)
constexpr uint32_t IDLE_POLL_MS = 10;

/// Delay after touch read error (milliseconds)
constexpr uint32_t ERROR_DELAY_MS = 5;

/// White color for drawing
constexpr uint16_t COLOR_WHITE = 0xFFFF;
}  // namespace TouchDrawingConfig

/**
 * @brief IMU visualization configuration
 */
namespace IMUVisualizationConfig {
/// IMU data update interval (milliseconds)
constexpr uint32_t UPDATE_INTERVAL_MS = 100;

/// Render frame interval (milliseconds) - 20 FPS
constexpr uint32_t RENDER_INTERVAL_MS = 50;

/**
 * @brief Canvas buffer configuration
 * @details 200x200 pixel RGB565 buffer = 200 * 200 * 2 bytes = 80KB
 */
constexpr int CANVAS_WIDTH = 200;
constexpr int CANVAS_HEIGHT = 200;
constexpr size_t CANVAS_BUFFER_SIZE = CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(uint16_t);

/**
 * @brief Accelerometer smoothing
 * @details Exponential moving average factor (0.0 - 1.0)
 *          Lower = smoother, Higher = more responsive
 */
constexpr float ACCEL_SMOOTH_FACTOR = 0.15F;

/**
 * @brief 3D cube projection parameters
 */
namespace Projection3D {
/// Cube size scaling factor
constexpr float CUBE_SCALE = 50.0F;

/// Perspective projection depth (larger = less perspective)
constexpr float PERSPECTIVE_DEPTH = 200.0F;
}  // namespace Projection3D
}  // namespace IMUVisualizationConfig

/**
 * @brief RTC clock display configuration
 */
namespace RTCClockConfig {
/// Uptime calculation conversion factor
constexpr uint32_t MS_PER_SECOND = 1000;
}  // namespace RTCClockConfig

/**
 * @brief SD card test configuration
 */
namespace SDCardTestConfig {
/// Delay after format complete (milliseconds)
constexpr uint32_t FORMAT_COMPLETE_DISPLAY_MS = 500;

/// Format operation task stack size (bytes)
constexpr size_t FORMAT_TASK_STACK_SIZE = 8192;

/// Format task priority
constexpr uint8_t FORMAT_TASK_PRIORITY = 5;
}  // namespace SDCardTestConfig

/**
 * @brief Power management test configuration
 */
namespace PowerManagementTestConfig {
/// Countdown display update task stack size (bytes)
constexpr size_t COUNTDOWN_TASK_STACK_SIZE = 4096;

/// Countdown task priority
constexpr uint8_t COUNTDOWN_TASK_PRIORITY = 3;

/// Countdown update interval (milliseconds)
constexpr uint32_t COUNTDOWN_UPDATE_MS = 1000;

/// LVGL lock timeout (milliseconds)
constexpr uint32_t LVGL_LOCK_TIMEOUT_MS = 100;
}  // namespace PowerManagementTestConfig

}  // namespace tests
