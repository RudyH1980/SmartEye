/**
 * @file board_drivers.hpp
 * @brief ESP32-S3 Touch LCD 2.8B Board Drivers - Unified Header
 * @details Tüm board driver'larını tek yerden erişilebilir yapar
 */

#pragma once

#include "board_config.hpp"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_io_expander.h"
#include "esp_lcd_panel_ops.h"
#include "gt911_simple.hpp"    // New: Simple GT911 driver
#include "lcd_st7701_rgb.hpp"  // Yeni LCD driver

/**
 * @brief Board Drivers Namespace
 */
namespace BoardDrivers {

/**
 * @brief Hardware handles struct
 */
struct HardwareHandles {
    // I2C
    i2c_master_bus_handle_t i2cBus;

    // IO Expander
    esp_io_expander_handle_t ioExpander;

    // LCD (Yeni: ST7701 + RGB handle)
    lcd::ST7701Handle *lcdHandle;  // Waveshare style LCD handle

    // Touch (New: Simple GT911 driver)
    gt911::GT911Handle *touch;

    // Sensors (opaque pointers - cast to qmi8658_dev_t*/pcf85063a_dev_t* when needed)
    void *imu;
    void *rtc;

    // Backlight
    ledc_channel_t backlightChannel;
};

/**
 * @brief Initialize all board hardware
 * @param handles Output - initialized hardware handles
 * @return ESP_OK on success
 */
esp_err_t initAll(HardwareHandles &handles);

/**
 * @brief Initialize I2C bus
 * @param handle Output - I2C bus handle
 * @return ESP_OK on success
 */
esp_err_t initI2C(i2c_master_bus_handle_t *handle);

/**
 * @brief Initialize IO Expander (TCA9554)
 * @param i2cBus I2C bus handle
 * @param handle Output - IO expander handle
 * @return ESP_OK on success
 */
esp_err_t initIOExpander(i2c_master_bus_handle_t i2cBus, esp_io_expander_handle_t *handle);

/**
 * @brief Claim the shared I2C bus
 * @details Touch, IMU, RTC and the IO expander all sit on one bus, and they
 *          are read from different tasks. Overlapping transactions wedge that
 *          bus for good ("I2C bus is still busy"), after which touch stops
 *          responding entirely. Every access from outside board_drivers has
 *          to be wrapped in this.
 * @param timeoutMs How long to wait for the bus
 * @return true when the bus is yours; only then call i2cUnlock()
 */
bool i2cLock(uint32_t timeoutMs);

/** @brief Release the shared I2C bus */
void i2cUnlock();

/**
 * @brief Clock a jammed I2C bus free
 * @details Call this when reads keep failing: a device left holding the data
 *          line down makes every later transaction report the bus as busy,
 *          and nothing recovers on its own.
 */
esp_err_t i2cRecover();

/**
 * @brief Initialize LCD Display (ST7701 + RGB)
 * @param io_expander IO expander handle (for reset/CS control)
 * @param handle Output - LCD handle (contains SPI device and RGB panel)
 * @return ESP_OK on success
 */
esp_err_t initLCD(esp_io_expander_handle_t ioExpander, lcd::ST7701Handle **handle);

/**
 * @brief Initialize Touch Controller (GT911)
 * @param i2cBus I2C bus handle
 * @param ioExpander IO expander handle (for reset control)
 * @param handle Output - GT911 handle
 * @return ESP_OK on success
 */
esp_err_t initTouch(
    i2c_master_bus_handle_t i2cBus,
    esp_io_expander_handle_t ioExpander,
    gt911::GT911Handle **handle
);

/**
 * @brief Initialize IMU Sensor (QMI8658)
 * @param i2cBus I2C bus handle
 * @param handle Output - IMU device handle (cast to qmi8658_dev_t*)
 * @return ESP_OK on success
 */
esp_err_t initIMU(i2c_master_bus_handle_t i2cBus, void **handle);

/**
 * @brief Initialize RTC (PCF85063)
 * @param i2cBus I2C bus handle
 * @param handle Output - RTC device handle (cast to pcf85063a_dev_t*)
 * @return ESP_OK on success
 */
esp_err_t initRTC(i2c_master_bus_handle_t i2cBus, void **handle);

/**
 * @brief Initialize LCD Backlight PWM
 * @param channel Output - LEDC channel
 * @return ESP_OK on success
 */
esp_err_t initBacklight(ledc_channel_t *channel);

}  // namespace BoardDrivers
