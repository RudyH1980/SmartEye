/**
 * @file lvgl_wrapper.hpp
 * @brief LVGL initialization and driver wrapper for ESP32-S3 Touch LCD 2.8B
 * @details Provides clean C++ API for LVGL with display and touch drivers
 */

#pragma once

#include "board_drivers.hpp"
#include "esp_err.h"
#include "lvgl.h"

namespace lvgl_wrapper {

/**
 * @brief LVGL handles structure
 */
struct LvglHandles {
    lv_display_t *display;  ///< LVGL display handle
    lv_indev_t *touchpad;   ///< LVGL touch input device handle
};

/**
 * @brief Initialize LVGL with display and touch drivers
 * @param hw Board hardware handles (from board_drivers)
 * @param lvglHandles Output LVGL handles
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t init(const BoardDrivers::HardwareHandles &hw, LvglHandles &lvglHandles);

/**
 * @brief Get LVGL timer period for task delay
 * @return Timer period in milliseconds
 */
uint32_t getTimerPeriodMs();

/**
 * @brief Lock LVGL mutex (thread-safe operations)
 * @param timeout_ms Timeout in milliseconds
 * @return true if locked successfully
 */
bool lock(uint32_t timeout_ms);

/**
 * @brief Unlock LVGL mutex
 */
void unlock();

}  // namespace lvgl_wrapper
