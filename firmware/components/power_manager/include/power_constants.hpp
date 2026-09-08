/**
 * @file power_constants.hpp
 * @brief Power management timing and threshold constants
 */

#pragma once

#include <cstdint>

/**
 * @brief Time conversion constants
 */
namespace TimeConversion {
/// Microseconds per second
constexpr int64_t US_PER_SECOND = 1000000LL;

/// Milliseconds per second
constexpr uint32_t MS_PER_SECOND = 1000;
}  // namespace TimeConversion

/**
 * @brief Backlight level constants (percentage)
 */
namespace BacklightLevels {
/// Full brightness for active mode
constexpr uint8_t ACTIVE_MODE_PCT = 100;

/// Reduced brightness for low-power mode
constexpr uint8_t LOW_POWER_MODE_PCT = 20;

/// Backlight off
constexpr uint8_t OFF_PCT = 0;
}  // namespace BacklightLevels

/**
 * @brief Auto-sleep task configuration
 */
namespace AutoSleepConfig {
/// Task stack size (bytes)
constexpr size_t TASK_STACK_SIZE = 4096;

/// Task priority
constexpr uint8_t TASK_PRIORITY = 5;

/// Check interval (milliseconds)
constexpr uint32_t CHECK_INTERVAL_MS = 1000;

/// Warning threshold - start logging countdown (seconds)
constexpr uint32_t WARNING_THRESHOLD_SEC = 10;

/// Warning interval - log every N seconds (seconds)
constexpr uint32_t WARNING_INTERVAL_SEC = 5;
}  // namespace AutoSleepConfig

/**
 * @brief Battery monitoring constants (LiPo battery)
 */
namespace BatteryConfig {
/// ADC measurement range at 12dB attenuation (millivolts)
constexpr uint16_t ADC_MAX_MV = 3100;

/// 12-bit ADC resolution (0-4095)
constexpr uint16_t ADC_12BIT_MAX = 4096;

/// Voltage divider ratio (battery voltage / ADC voltage)
constexpr uint8_t VOLTAGE_DIVIDER_RATIO = 2;

/// USB power detection threshold (millivolts)
constexpr uint16_t USB_POWER_THRESHOLD_MV = 4500;

/**
 * @brief LiPo battery voltage-to-percentage curve thresholds
 * @details Based on typical 3.7V LiPo discharge curve:
 *          - 4.2V = 100% (fully charged)
 *          - 3.7V = 50%  (nominal voltage)
 *          - 3.4V = 10%  (low battery warning)
 *          - 3.0V = 0%   (cutoff voltage)
 */
namespace VoltageThresholds {
constexpr uint16_t FULL_MV = 4200;     ///< 100% charge
constexpr uint16_t NOMINAL_MV = 3700;  ///< 50% charge
constexpr uint16_t LOW_MV = 3400;      ///< 10% charge (warning)
constexpr uint16_t EMPTY_MV = 3000;    ///< 0% charge (cutoff)

// Voltage ranges for percentage calculation
constexpr uint16_t RANGE_FULL_TO_NOMINAL = FULL_MV - NOMINAL_MV;  // 500mV
constexpr uint16_t RANGE_NOMINAL_TO_LOW = NOMINAL_MV - LOW_MV;    // 300mV
constexpr uint16_t RANGE_LOW_TO_EMPTY = LOW_MV - EMPTY_MV;        // 400mV
}  // namespace VoltageThresholds
}  // namespace BatteryConfig
