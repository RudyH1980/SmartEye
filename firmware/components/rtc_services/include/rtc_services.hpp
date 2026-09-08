/**
 * @file rtc_services.hpp
 * @brief RTC Services - High-level API for PCF85063A Real-Time Clock
 * @details Provides time management, alarm services, and sleep timer support
 *
 * Features:
 * - Time/date get/set operations
 * - System time synchronization (POSIX time ↔ RTC)
 * - Alarm management with callbacks
 * - Deep sleep wake-up timer (future)
 * - Thread-safe operations
 *
 * Hardware: PCF85063A RTC chip (I2C address 0x51)
 */

#pragma once

#include <ctime>
#include <functional>
#include "esp_err.h"

extern "C" {
#include "pcf85063a.h"
}

namespace rtc_services {

// ============================================================================
// TIME MANAGEMENT
// ============================================================================

/**
 * @brief Initialize RTC service
 * @param rtc Pointer to PCF85063A device handle
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t initRTC(pcf85063a_dev_t *rtc);

/**
 * @brief Set current date and time
 * @param year Full year (e.g., 2025)
 * @param month Month (1-12)
 * @param day Day of month (1-31)
 * @param hour Hour (0-23)
 * @param min Minute (0-59)
 * @param sec Second (0-59)
 * @return ESP_OK on success
 */
esp_err_t setTime(
    uint16_t year,
    uint8_t month,
    uint8_t day,
    uint8_t hour,
    uint8_t min,
    uint8_t sec
);

/**
 * @brief Get current date and time
 * @param year Pointer to store year
 * @param month Pointer to store month (1-12)
 * @param day Pointer to store day (1-31)
 * @param hour Pointer to store hour (0-23)
 * @param min Pointer to store minute (0-59)
 * @param sec Pointer to store second (0-59)
 * @return ESP_OK on success
 */
esp_err_t getTime(
    uint16_t *year,
    uint8_t *month,
    uint8_t *day,
    uint8_t *hour,
    uint8_t *min,
    uint8_t *sec
);

/**
 * @brief Get day of week
 * @param dotw Pointer to store day of week (0=Sunday, 1=Monday, ..., 6=Saturday)
 * @return ESP_OK on success
 */
esp_err_t getDayOfWeek(uint8_t *dotw);

// ============================================================================
// SYSTEM TIME SYNCHRONIZATION
// ============================================================================

/**
 * @brief Synchronize system time FROM RTC
 * @details Reads RTC time and updates ESP32 POSIX system time
 * @return ESP_OK on success
 */
esp_err_t syncSystemTimeFromRTC();

/**
 * @brief Synchronize RTC FROM system time
 * @details Reads ESP32 POSIX system time and updates RTC
 * @return ESP_OK on success
 */
esp_err_t syncRTCFromSystemTime();

/**
 * @brief Get RTC time as POSIX timestamp
 * @param outTimestamp Pointer to store Unix timestamp (seconds since 1970-01-01)
 * @return ESP_OK on success
 */
esp_err_t getTimestamp(time_t *outTimestamp);

/**
 * @brief Set RTC time from POSIX timestamp
 * @param timestamp Unix timestamp (seconds since 1970-01-01)
 * @return ESP_OK on success
 */
esp_err_t setTimeFromTimestamp(time_t timestamp);

// ============================================================================
// ALARM SERVICE
// ============================================================================

/**
 * @brief Alarm callback function type
 */
using AlarmCallback = std::function<void()>;

/**
 * @brief Set alarm with callback
 * @param hour Alarm hour (0-23)
 * @param minute Alarm minute (0-59)
 * @param second Alarm second (0-59)
 * @param callback Function to call when alarm triggers (can be nullptr for polling mode)
 * @return ESP_OK on success
 */
esp_err_t setAlarmWithCallback(
    uint8_t hour,
    uint8_t minute,
    uint8_t second,
    AlarmCallback callback = nullptr
);

/**
 * @brief Disable current alarm
 */
void disableAlarm();

/**
 * @brief Check if alarm has triggered
 * @return true if alarm flag is set
 */
bool isAlarmTriggered();

/**
 * @brief Clear alarm flag
 */
void clearAlarmFlag();

// ============================================================================
// DEEP SLEEP TIMER SERVICE (FUTURE)
// ============================================================================

/**
 * @brief Configure RTC for deep sleep wake-up
 * @param wakeupSeconds Seconds until wake-up
 * @return ESP_OK on success
 * @note This is a placeholder for future sleep mode integration
 */
esp_err_t configureDeepSleepWakeup(uint32_t wakeupSeconds);

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

/**
 * @brief Format current time as string
 * @param buffer Buffer to store formatted string
 * @param bufferSize Size of buffer
 * @param format Format: "HH:MM:SS" or "YYYY-MM-DD HH:MM:SS"
 * @return ESP_OK on success
 */
esp_err_t formatTime(char *buffer, size_t bufferSize, bool includeDate = false);

/**
 * @brief Format current date as string
 * @param buffer Buffer to store formatted string
 * @param bufferSize Size of buffer
 * @param format Format: "Day, Mon DD" (e.g., "Monday, Nov 3")
 * @return ESP_OK on success
 */
esp_err_t formatDate(char *buffer, size_t bufferSize);

/**
 * @brief Get day of week name
 * @param dotw Day of week (0=Sunday, 1=Monday, ..., 6=Saturday)
 * @return Day name string (e.g., "Monday")
 */
const char *getDayName(uint8_t dotw);

/**
 * @brief Get month name
 * @param month Month number (1-12)
 * @return Month name string (e.g., "January")
 */
const char *getMonthName(uint8_t month);

}  // namespace rtc_services
