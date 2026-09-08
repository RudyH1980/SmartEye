/**
 * @file gt911_simple.hpp
 * @brief Simple GT911 Touch Controller Driver for New I2C Master API
 * @details Direct I2C register access - bypasses ESP-IDF esp_lcd_touch component
 */

#pragma once

#include <cstdint>
#include "driver/i2c_master.h"
#include "esp_err.h"

namespace gt911 {

// GT911 Register Addresses
constexpr uint16_t REG_PRODUCT_ID = 0x8140;      // Product ID (4 bytes: "911")
constexpr uint16_t REG_CONFIG_VERSION = 0x8047;  // Config version
constexpr uint16_t REG_STATUS = 0x814E;          // Touch status
constexpr uint16_t REG_TOUCH_DATA = 0x814F;      // Touch data start

constexpr uint8_t MAX_TOUCH_POINTS = 5;

/**
 * @brief GT911 Touch Point Data
 */
struct TouchPoint {
    uint16_t x;
    uint16_t y;
    uint16_t size;  // Touch area size
    uint8_t trackId;
};

/**
 * @brief GT911 Handle
 */
struct GT911Handle {
    i2c_master_dev_handle_t i2cDev;
    uint16_t xMax;
    uint16_t yMax;
};

/**
 * @brief Initialize GT911 (after hardware reset)
 * @param i2cBus I2C master bus handle
 * @param devAddr GT911 I2C address (usually 0x5D or 0x14)
 * @param handle Output - GT911 handle
 * @return ESP_OK on success
 */
esp_err_t init(i2c_master_bus_handle_t i2cBus, uint8_t devAddr, GT911Handle **handle);

/**
 * @brief Read GT911 product ID
 * @param handle GT911 handle
 * @param productId Output - 4 byte product ID
 * @return ESP_OK on success
 */
esp_err_t readProductId(GT911Handle *handle, uint8_t productId[4]);

/**
 * @brief Read touch data
 * @param handle GT911 handle
 * @param points Output array for touch points
 * @param maxPoints Maximum number of points to read
 * @param numTouches Output - actual number of touches detected
 * @return ESP_OK when a fresh sample was read (numTouches may be 0, meaning
 *         the finger was lifted), ESP_ERR_NOT_FINISHED when the controller
 *         has no new sample yet and the previous state still stands, or an
 *         error code on I2C failure.
 */
esp_err_t readTouchData(
    GT911Handle *handle,
    TouchPoint *points,
    uint8_t maxPoints,
    uint8_t *numTouches
);

/**
 * @brief Deinitialize GT911
 * @param handle GT911 handle to free
 */
void deinit(GT911Handle *handle);

}  // namespace gt911
