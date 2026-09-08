/**
 * @file lcd_st7701_rgb.hpp
 * @brief ST7701 LCD Controller + RGB Interface Driver (Waveshare Style)
 * @details Manuel 3-wire SPI initialization + Ayrı RGB panel
 *
 * Bu implementasyon Waveshare'in yaklaşımını takip eder:
 * 1. ST7701 init komutları manuel SPI ile gönderilir
 * 2. RGB panel ayrı olarak oluşturulur
 * 3. İki sistem bağımsız çalışır
 */

#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_io_expander.h"
#include "esp_lcd_panel_rgb.h"

namespace BoardDrivers::lcd {

/**
 * @brief ST7701 LCD initialization configuration
 */
struct ST7701Config {
    // SPI pins for command interface
    gpio_num_t spiMosi;
    gpio_num_t spiSclk;

    // IO Expander handle (for CS, Reset, Power control)
    esp_io_expander_handle_t ioExpander;

    // LCD specifications
    uint16_t width;
    uint16_t height;
    uint32_t pixelClockHz;
};

/**
 * @brief ST7701 + RGB panel handle
 */
struct ST7701Handle {
    spi_device_handle_t spiDevice;        // SPI device for commands
    esp_lcd_panel_handle_t rgbPanel;      // RGB panel handle
    esp_io_expander_handle_t ioExpander;  // IO expander for control pins
    uint16_t width;
    uint16_t height;
};

/**
 * @brief Initialize ST7701 LCD with RGB interface (Waveshare style)
 *
 * @param config LCD configuration
 * @param[out] outHandle Returned LCD handle
 * @return esp_err_t
 */
esp_err_t st7701RgbInit(const ST7701Config &config, ST7701Handle **outHandle);

/**
 * @brief Get RGB panel frame buffer
 *
 * @param handle LCD handle
 * @param fbNum Frame buffer number (usually 1)
 * @param[out] frameBuffer Returned frame buffer pointer
 * @return esp_err_t
 */
esp_err_t st7701RgbGetFrameBuffer(ST7701Handle *handle, uint32_t fbNum, void **frameBuffer);

/**
 * @brief Draw bitmap to LCD
 *
 * @param handle LCD handle
 * @param xStart Start X coordinate
 * @param y_start Start Y coordinate
 * @param x_end End X coordinate
 * @param y_end End Y coordinate
 * @param color_data Color data buffer
 * @return esp_err_t
 */
esp_err_t st7701RgbDrawBitmap(
    ST7701Handle *handle,
    int xStart,
    int yStart,
    int xEnd,
    int yEnd,
    const void *colorData
);

/**
 * @brief Turn display on/off
 *
 * @param handle LCD handle
 * @param on true = on, false = off
 * @return esp_err_t
 */
esp_err_t st7701RgbDisplayOnOff(ST7701Handle *handle, bool isOn);

/**
 * @brief Delete ST7701 handle and free resources
 *
 * @param handle LCD handle
 * @return esp_err_t
 */
esp_err_t st7701RgbDel(ST7701Handle *handle);

}  // namespace BoardDrivers::lcd
