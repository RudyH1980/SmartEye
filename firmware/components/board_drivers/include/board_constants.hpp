/**
 * @file board_constants.hpp
 * @brief Hardware timing and configuration constants for board initialization
 */

#pragma once

#include <cstdint>

namespace BoardDrivers {

/**
 * @brief Power management timing constants
 */
namespace PowerTiming {
/// SD card power stabilization delay after enabling (milliseconds)
constexpr uint32_t SD_POWER_STABILIZATION_MS = 300;
}  // namespace PowerTiming

/**
 * @brief GT911 touch controller timing constants
 */
namespace GT911Timing {
/// Initial reset pulse duration (milliseconds)
constexpr uint32_t RESET_DELAY_MS = 150;

/// Pulse width for address selection (milliseconds)
constexpr uint32_t PULSE_DELAY_MS = 50;

/// Boot time after reset release (milliseconds)
constexpr uint32_t BOOT_DELAY_MS = 100;
}  // namespace GT911Timing

/**
 * @brief LCD backlight configuration constants
 */
namespace BacklightConfig {
/// LEDC timer resolution (13-bit = 8192 levels for smooth control)
constexpr uint32_t TIMER_RESOLUTION_BITS = 13;

/// Maximum duty cycle value (2^13 - 1)
constexpr uint32_t MAX_DUTY_CYCLE = (1U << TIMER_RESOLUTION_BITS) - 1;

/// Backlight off state
constexpr uint32_t DUTY_OFF = 0;
}  // namespace BacklightConfig

/**
 * @brief TCA9554 IO expander constants
 */
namespace TCA9554Config {
/// Mask for all 8 pins
constexpr uint8_t ALL_PINS_MASK = 0xFF;

/// Buzzer pin (P7)
constexpr uint8_t BUZZER_PIN = 7;
constexpr uint8_t BUZZER_PIN_MASK = (1 << BUZZER_PIN);

/// LCD TE and RST pins (P0, P1)
constexpr uint8_t LCD_TE_PIN = 0;
constexpr uint8_t LCD_RST_PIN = 1;
constexpr uint8_t LCD_CONTROL_MASK = (1 << LCD_TE_PIN) | (1 << LCD_RST_PIN);
}  // namespace TCA9554Config

/**
 * @brief QMI8658 IMU constants
 */
namespace QMI8658Config {
/// Expected WHO_AM_I register value
constexpr uint8_t EXPECTED_WHO_AM_I = 0x05;
}  // namespace QMI8658Config

}  // namespace BoardDrivers
