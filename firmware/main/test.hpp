/**
 * @file test.hpp
 * @brief Test functions for ESP32-S3 Touch LCD 2.8B
 * @details Contains various test and demo functions for LCD and touch functionality
 */

#pragma once

#include "board_drivers.hpp"

namespace tests {

/**
 * @brief Fill LCD screen with a solid color
 * @param hw Hardware handles
 * @param color RGB565 color value (e.g., 0xFFFF for white, 0x0000 for black)
 */
void fillScreen(const BoardDrivers::HardwareHandles &hw, uint16_t color);

/**
 * @brief Set backlight brightness
 * @param hw Hardware handles
 * @param brightness Brightness percentage (0-100)
 */
void setBacklight(const BoardDrivers::HardwareHandles &hw, uint8_t brightness);

/**
 * @brief Draw a dot on the LCD at specified position
 * @param hw Hardware handles
 * @param x X coordinate
 * @param y Y coordinate
 * @param size Dot size (radius)
 * @param color RGB565 color
 */
void drawDot(
    const BoardDrivers::HardwareHandles &hw,
    int16_t x,
    int16_t y,
    int size,
    uint16_t color
);

/**
 * @brief Draw a line between two points using dots
 * @param hw Hardware handles
 * @param x0 Start X
 * @param y0 Start Y
 * @param x1 End X
 * @param y1 End Y
 * @param size Dot size
 * @param color RGB565 color
 */
void drawLine(
    const BoardDrivers::HardwareHandles &hw,
    int16_t x0,
    int16_t y0,
    int16_t x1,
    int16_t y1,
    int size,
    uint16_t color
);

/**
 * @brief Test: Draw 4 colored squares at corners to verify coordinate mapping
 * @param hw Hardware handles
 */
void testCornerSquares(const BoardDrivers::HardwareHandles &hw);

/**
 * @brief Demo: Touch drawing application
 * @param hw Hardware handles
 */
[[noreturn]] void demoTouchDrawing(const BoardDrivers::HardwareHandles &hw);

/**
 * @brief Demo: IMU sensor data display (console only)
 * @param hw Hardware handles
 */
[[noreturn]] void demoIMU(const BoardDrivers::HardwareHandles &hw);

/**
 * @brief Demo: IMU sensor data with LVGL visualization
 * @param hw Hardware handles
 */
[[noreturn]] void demoIMU_GUI(const BoardDrivers::HardwareHandles &hw);

/**
 * @brief Demo: Real-time clock display with LVGL
 * @param hw Hardware handles
 */
[[noreturn]] void demoRTCClock(const BoardDrivers::HardwareHandles &hw);

/**
 * @brief Demo: SD Card info, file browser, and format with LVGL
 * @param hw Hardware handles
 */
[[noreturn]] void demoSDCard(const BoardDrivers::HardwareHandles &hw);

/**
 * @brief Test: Power Management with SD Card integration
 * @param hw Hardware handles
 * @details Tests power modes, auto-sleep, wake-up, and SD Card pre-sleep/post-wakeup callbacks
 */
[[noreturn]] void testPowerManagement(const BoardDrivers::HardwareHandles &hw);

}  // namespace tests
