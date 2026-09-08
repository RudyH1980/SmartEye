/**
 * @file gt911_simple.cpp
 * @brief Simple GT911 Touch Controller Driver Implementation
 * @details Uses new I2C Master API directly
 */

#include "gt911_simple.hpp"

#include "board_config.hpp"
#include <cstdlib>
#include <cstring>
#include "esp_log.h"

constexpr static const char *TAG = "GT911_Simple";

namespace gt911 {

/**
 * @brief Write 16-bit register address + data to GT911
 */
static esp_err_t writeReg(
    i2c_master_dev_handle_t dev,
    uint16_t regAddr,
    const uint8_t *data,
    size_t len
) {
    uint8_t buffer[len + 2];
    buffer[0] = (regAddr >> 8) & 0xFF;  // Register address high byte
    buffer[1] = regAddr & 0xFF;         // Register address low byte
    memcpy(&buffer[2], data, len);

    return i2c_master_transmit(dev, buffer, len + 2, 1000);
}

/**
 * @brief Read from 16-bit register address
 */
static esp_err_t readReg(i2c_master_dev_handle_t dev, uint16_t regAddr, uint8_t *data, size_t len) {
    uint8_t regBuf[2];
    regBuf[0] = (regAddr >> 8) & 0xFF;
    regBuf[1] = regAddr & 0xFF;

    return i2c_master_transmit_receive(dev, regBuf, 2, data, len, 1000);
}

esp_err_t init(i2c_master_bus_handle_t i2cBus, uint8_t devAddr, GT911Handle **handle) {
    ESP_LOGI(TAG, "Initializing GT911 at address 0x%02X", devAddr);

    // Allocate handle
    auto *h = (GT911Handle *)malloc(sizeof(GT911Handle));
    if (h == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate GT911Handle");
        return ESP_ERR_NO_MEM;
    }

    // Configure I2C device
    i2c_device_config_t devCfg = {};
    devCfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    devCfg.device_address = devAddr;
    // 100kHz rather than 400kHz. At the higher speed this bus returns
    // corrupted samples: coordinates come back as register addresses or as
    // 0xFFFF, and a touch at an impossible position never reaches the screen
    // it was meant for. Touch is polled 50 times a second, so the slower
    // clock costs nothing noticeable.
    devCfg.scl_speed_hz = 100000;

    esp_err_t ret = i2c_master_bus_add_device(i2cBus, &devCfg, &h->i2cDev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add GT911 device: %s", esp_err_to_name(ret));
        free(h);
        return ret;
    }

    // Read product ID to verify communication
    uint8_t productId[4] = {0};
    ret = readReg(h->i2cDev, REG_PRODUCT_ID, productId, 4);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read product ID: %s", esp_err_to_name(ret));
        i2c_master_bus_rm_device(h->i2cDev);
        free(h);
        return ret;
    }

    ESP_LOGI(
        TAG,
        "GT911 Product ID: %c%c%c%c (0x%02X 0x%02X 0x%02X 0x%02X)",
        productId[0],
        productId[1],
        productId[2],
        productId[3],
        productId[0],
        productId[1],
        productId[2],
        productId[3]
    );

    // Read config version
    uint8_t configVer = 0;
    ret = readReg(h->i2cDev, REG_CONFIG_VERSION, &configVer, 1);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "GT911 Config Version: %d", configVer);
    }

    // Read resolution from config (0x8048-0x804B)
    uint8_t resData[4] = {0};
    ret = readReg(h->i2cDev, 0x8048, resData, 4);
    if (ret == ESP_OK) {
        uint16_t xMax = resData[0] | (resData[1] << 8);
        uint16_t yMax = resData[2] | (resData[3] << 8);
        ESP_LOGI(TAG, "GT911 Config Resolution: X_max=%d, Y_max=%d", xMax, yMax);
        h->xMax = xMax;
        h->yMax = yMax;
    } else {
        // Set default resolution if read fails
        ESP_LOGW(TAG, "Failed to read GT911 resolution config, using defaults");
        h->xMax = 480;
        h->yMax = 640;
    }

    *handle = h;
    ESP_LOGI(TAG, "✓ GT911 initialized successfully");

    return ESP_OK;
}

esp_err_t readProductId(GT911Handle *handle, uint8_t productId[4]) {
    if (handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    return readReg(handle->i2cDev, REG_PRODUCT_ID, productId, 4);
}

esp_err_t readTouchData(
    GT911Handle *handle,
    TouchPoint *points,
    uint8_t maxPoints,
    uint8_t *numTouches
) {
    if (handle == nullptr || points == nullptr || numTouches == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    *numTouches = 0;

    // Read status register
    uint8_t status = 0;
    esp_err_t ret = readReg(handle->i2cDev, REG_STATUS, &status, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    // Check if touch data is ready (bit 7) and get touch count (bits 0-3)
    bool dataReady = (status & 0x80) != 0;
    uint8_t touchCount = status & 0x0F;

    if (!dataReady) {
        // No new sample, so there is nothing to acknowledge. Writing anyway
        // doubles the traffic on a bus that is already the busiest thing in
        // this firmware, and this bus jams when pushed.
        return ESP_OK;
    }

    if (touchCount == 0) {
        // A fresh sample saying the finger is gone still has to be acked
        uint8_t zero = 0;
        writeReg(handle->i2cDev, REG_STATUS, &zero, 1);
        return ESP_OK;
    }

    // Limit touch count
    if (touchCount > maxPoints) {
        touchCount = maxPoints;
    }
    if (touchCount > MAX_TOUCH_POINTS) {
        touchCount = MAX_TOUCH_POINTS;
    }

    // Read touch data (8 bytes per touch point)
    uint8_t touchData[8 * MAX_TOUCH_POINTS];
    ret = readReg(handle->i2cDev, REG_TOUCH_DATA, touchData, 8 * touchCount);
    if (ret != ESP_OK) {
        return ret;
    }

    // Parse touch points
    // Match Waveshare's GT911.c:129-130 byte order
    for (uint8_t i = 0; i < touchCount; i++) {
        uint8_t *data = &touchData[i * 8];

        points[i].trackId = data[0];
        // GT911 format (little-endian): X_Low, X_High, Y_Low, Y_High
        // [0]=TrackID [1]=X_Low [2]=X_High [3]=Y_Low [4]=Y_High [5]=Size_Low [6]=Size_High
        // [7]=Reserved
        points[i].x = (uint16_t)(data[1] | (data[2] << 8));
        points[i].y = (uint16_t)(data[3] | (data[4] << 8));
        points[i].size = (uint16_t)(data[5] | (data[6] << 8));

        // A reading outside the panel is corruption, not a finger. Passing it
        // on puts the touch somewhere off-screen, where it belongs to no
        // widget at all and every gesture is silently lost.
        if ((points[i].x >= BoardConfig::LCD_WIDTH) || (points[i].y >= BoardConfig::LCD_HEIGHT)) {
            ESP_LOGD(TAG, "Discarding out-of-range touch %u,%u", points[i].x, points[i].y);
            *numTouches = 0;
            return ESP_OK;
        }
    }

    *numTouches = touchCount;

    // Clear status register to indicate data has been read
    uint8_t zero = 0;
    writeReg(handle->i2cDev, REG_STATUS, &zero, 1);

    return ESP_OK;
}

void deinit(GT911Handle *handle) {
    if (handle != nullptr) {
        if (handle->i2cDev != nullptr) {
            i2c_master_bus_rm_device(handle->i2cDev);
        }
        free(handle);
    }
}

}  // namespace gt911
