/**
 * @file rtc_services.cpp
 * @brief RTC Services Implementation
 */

#include "rtc_services.hpp"
#include <sys/time.h>
#include <cstring>
#include <utility>
#include "esp_log.h"

static const char *tag = "RTC_Services";

namespace rtc_services {

// ============================================================================
// PRIVATE STATE
// ============================================================================

static pcf85063a_dev_t *gRtc = nullptr;
static AlarmCallback gAlarmCallback = nullptr;

// Day and month name lookup tables
static const char *dayNames[] =
    {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

static const char *monthNames[] = {
    "",
    "January",
    "February",
    "March",
    "April",
    "May",
    "June",
    "July",
    "August",
    "September",
    "October",
    "November",
    "December"};

// ============================================================================
// TIME MANAGEMENT
// ============================================================================

esp_err_t initRTC(pcf85063a_dev_t *rtc) {
    if (rtc == nullptr) {
        ESP_LOGE(tag, "RTC device handle is null");
        return ESP_ERR_INVALID_ARG;
    }

    gRtc = rtc;

    // Optional: Reset RTC to known state
    // pcf85063a_reset(g_rtc);

    ESP_LOGI(tag, "RTC service initialized");
    return ESP_OK;
}

esp_err_t setTime(
    uint16_t year,
    uint8_t month,
    uint8_t day,
    uint8_t hour,
    uint8_t min,
    uint8_t sec
) {
    if (gRtc == nullptr) {
        ESP_LOGE(tag, "RTC not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Calculate day of week (Zeller's congruence algorithm)
    uint16_t y = year;
    uint8_t m = month;
    if (m < 3) {
        m += 12;
        y -= 1;
    }
    uint8_t dotw = (day + (13 * (m + 1)) / 5 + (y % 100) + (y % 100) / 4 + (y / 100) / 4 -
                    2 * (y / 100)) %
                   7;

    pcf85063a_datetime_t datetime = {
        .year = year,
        .month = month,
        .day = day,
        .dotw = dotw,
        .hour = hour,
        .min = min,
        .sec = sec};

    esp_err_t ret = pcf85063a_set_time_date(gRtc, datetime);
    if (ret != ESP_OK) {
        ESP_LOGE(tag, "Failed to set RTC time: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(tag, "RTC time set: %04d-%02d-%02d %02d:%02d:%02d", year, month, day, hour, min, sec);
    return ESP_OK;
}

esp_err_t getTime(
    uint16_t *year,
    uint8_t *month,
    uint8_t *day,
    uint8_t *hour,
    uint8_t *min,
    uint8_t *sec
) {
    if (gRtc == nullptr) {
        ESP_LOGE(tag, "RTC not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    pcf85063a_datetime_t datetime;
    esp_err_t ret = pcf85063a_get_time_date(gRtc, &datetime);
    if (ret != ESP_OK) {
        ESP_LOGE(tag, "Failed to read RTC time: %s", esp_err_to_name(ret));
        return ret;
    }

    if (year != nullptr) {
        *year = datetime.year;
    }
    if (month != nullptr) {
        *month = datetime.month;
    }
    if (day != nullptr) {
        *day = datetime.day;
    }
    if (hour != nullptr) {
        *hour = datetime.hour;
    }
    if (min != nullptr) {
        *min = datetime.min;
    }
    if (sec != nullptr) {
        *sec = datetime.sec;
    }

    return ESP_OK;
}

esp_err_t getDayOfWeek(uint8_t *dotw) {
    if (gRtc == nullptr) {
        ESP_LOGE(tag, "RTC not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    pcf85063a_datetime_t datetime;
    esp_err_t ret = pcf85063a_get_time_date(gRtc, &datetime);
    if (ret != ESP_OK) {
        return ret;
    }

    if (dotw != nullptr) {
        *dotw = datetime.dotw;
    }
    return ESP_OK;
}

// ============================================================================
// SYSTEM TIME SYNCHRONIZATION
// ============================================================================

esp_err_t syncSystemTimeFromRTC() {
    if (gRtc == nullptr) {
        ESP_LOGE(tag, "RTC not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Read RTC time
    pcf85063a_datetime_t datetime;
    esp_err_t ret = pcf85063a_get_time_date(gRtc, &datetime);
    if (ret != ESP_OK) {
        ESP_LOGE(tag, "Failed to read RTC for sync: %s", esp_err_to_name(ret));
        return ret;
    }

    // Convert to struct tm
    struct tm timeinfo = {};
    timeinfo.tm_year = datetime.year - 1900;  // tm_year is years since 1900
    timeinfo.tm_mon = datetime.month - 1;     // tm_mon is 0-11
    timeinfo.tm_mday = datetime.day;
    timeinfo.tm_hour = datetime.hour;
    timeinfo.tm_min = datetime.min;
    timeinfo.tm_sec = datetime.sec;
    timeinfo.tm_wday = datetime.dotw;

    // Convert to time_t
    time_t timestamp = mktime(&timeinfo);

    // Set system time
    struct timeval tv = {.tv_sec = timestamp, .tv_usec = 0};
    settimeofday(&tv, nullptr);

    ESP_LOGI(
        tag,
        "System time synced from RTC: %04d-%02d-%02d %02d:%02d:%02d",
        datetime.year,
        datetime.month,
        datetime.day,
        datetime.hour,
        datetime.min,
        datetime.sec
    );

    return ESP_OK;
}

esp_err_t syncRTCFromSystemTime() {
    if (gRtc == nullptr) {
        ESP_LOGE(tag, "RTC not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Get system time
    time_t now = 0;
    time(&now);
    struct tm *timeinfo = localtime(&now);

    // Set RTC
    return setTime(
        timeinfo->tm_year + 1900,
        timeinfo->tm_mon + 1,
        timeinfo->tm_mday,
        timeinfo->tm_hour,
        timeinfo->tm_min,
        timeinfo->tm_sec
    );
}

esp_err_t getTimestamp(time_t *outTimestamp) {
    if (gRtc == nullptr || outTimestamp == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    pcf85063a_datetime_t datetime;
    esp_err_t ret = pcf85063a_get_time_date(gRtc, &datetime);
    if (ret != ESP_OK) {
        return ret;
    }

    struct tm timeinfo = {};
    timeinfo.tm_year = datetime.year - 1900;
    timeinfo.tm_mon = datetime.month - 1;
    timeinfo.tm_mday = datetime.day;
    timeinfo.tm_hour = datetime.hour;
    timeinfo.tm_min = datetime.min;
    timeinfo.tm_sec = datetime.sec;

    *outTimestamp = mktime(&timeinfo);
    return ESP_OK;
}

esp_err_t setTimeFromTimestamp(time_t timestamp) {
    if (gRtc == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    struct tm *timeinfo = localtime(&timestamp);
    return setTime(
        timeinfo->tm_year + 1900,
        timeinfo->tm_mon + 1,
        timeinfo->tm_mday,
        timeinfo->tm_hour,
        timeinfo->tm_min,
        timeinfo->tm_sec
    );
}

// ============================================================================
// ALARM SERVICE
// ============================================================================

esp_err_t setAlarmWithCallback(
    uint8_t hour,
    uint8_t minute,
    uint8_t second,
    AlarmCallback callback
) {
    if (gRtc == nullptr) {
        ESP_LOGE(tag, "RTC not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    gAlarmCallback = std::move(callback);

    pcf85063a_datetime_t alarmTime = {};
    alarmTime.hour = hour;
    alarmTime.min = minute;
    alarmTime.sec = second;

    esp_err_t ret = pcf85063a_set_alarm(gRtc, alarmTime);
    if (ret != ESP_OK) {
        ESP_LOGE(tag, "Failed to set alarm: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = pcf85063a_enable_alarm(gRtc);
    if (ret != ESP_OK) {
        ESP_LOGE(tag, "Failed to enable alarm: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(tag, "Alarm set: %02d:%02d:%02d", hour, minute, second);
    return ESP_OK;
}

void disableAlarm() {
    if (gRtc == nullptr) {
        return;
    }

    // Disable alarm by writing to control register
    // (Driver doesn't expose disable function, so we clear the flag)
    clearAlarmFlag();
    gAlarmCallback = nullptr;

    ESP_LOGI(tag, "Alarm disabled");
}

bool isAlarmTriggered() {
    if (gRtc == nullptr) {
        return false;
    }

    uint8_t alarmFlag = 0;
    esp_err_t ret = pcf85063a_get_alarm_flag(gRtc, &alarmFlag);
    if (ret != ESP_OK) {
        return false;
    }

    bool triggered = (alarmFlag & PCF85063A_RTC_CTRL_2_AF) != 0;

    // If alarm triggered and callback is set, execute it
    if (triggered && gAlarmCallback) {
        gAlarmCallback();
        // Re-enable alarm for next trigger
        pcf85063a_enable_alarm(gRtc);
    }

    return triggered;
}

void clearAlarmFlag() {
    if (gRtc == nullptr) {
        return;
    }

    // Re-enabling alarm clears the flag
    pcf85063a_enable_alarm(gRtc);
}

// ============================================================================
// DEEP SLEEP TIMER SERVICE (PLACEHOLDER)
// ============================================================================

esp_err_t configureDeepSleepWakeup(uint32_t wakeupSeconds) {
    // Placeholder for future deep sleep integration
    ESP_LOGI(tag, "Deep sleep wakeup configured: %lu seconds", wakeupSeconds);
    // Future: Configure RTC timer registers for wake-up
    return ESP_OK;
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

esp_err_t formatTime(char *buffer, size_t bufferSize, bool includeDate) {
    if (buffer == nullptr || bufferSize == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t year = 0;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t min;
    uint8_t sec;
    esp_err_t ret = getTime(&year, &month, &day, &hour, &min, &sec);
    if (ret != ESP_OK) {
        return ret;
    }

    if (includeDate) {
        snprintf(
            buffer,
            bufferSize,
            "%04d-%02d-%02d %02d:%02d:%02d",
            year,
            month,
            day,
            hour,
            min,
            sec
        );
    } else {
        snprintf(buffer, bufferSize, "%02d:%02d:%02d", hour, min, sec);
    }

    return ESP_OK;
}

esp_err_t formatDate(char *buffer, size_t bufferSize) {
    if (buffer == nullptr || bufferSize == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t year = 0;
    uint8_t month;
    uint8_t day;
    uint8_t dotw;
    esp_err_t ret = getTime(&year, &month, &day, nullptr, nullptr, nullptr);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = getDayOfWeek(&dotw);
    if (ret != ESP_OK) {
        return ret;
    }

    snprintf(buffer, bufferSize, "%s, %s %d", getDayName(dotw), getMonthName(month), day);

    return ESP_OK;
}

const char *getDayName(uint8_t dotw) {
    if (dotw > 6) {
        return "Unknown";
    }
    return dayNames[dotw];
}

const char *getMonthName(uint8_t month) {
    if (month < 1 || month > 12) {
        return "Unknown";
    }
    return monthNames[month];
}

}  // namespace rtc_services
