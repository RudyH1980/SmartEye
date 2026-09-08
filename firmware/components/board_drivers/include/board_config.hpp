/**
 * @file board_config.hpp
 * @brief ESP32-S3 Touch LCD 2.8B Board Configuration
 * @details Pin tanımları ve sabitler
 */

#pragma once

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_io_expander_tca9554.h"

namespace BoardConfig {

// ========== I2C Configuration ==========
constexpr gpio_num_t I2C_SCL_PIN = GPIO_NUM_7;
constexpr gpio_num_t I2C_SDA_PIN = GPIO_NUM_15;
constexpr gpio_num_t I2C_MASTER_NUM = GPIO_NUM_0;
constexpr uint32_t I2C_MASTER_TIMEOUT_MS = 1000;
constexpr uint32_t I2C_FREQ_HZ = 400000;
constexpr uint32_t I2C_ADDRESS = ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000;

// ========== LCD RGB Interface Pins ==========
constexpr gpio_num_t LCD_PIN_HSYNC = GPIO_NUM_38;
constexpr gpio_num_t LCD_PIN_VSYNC = GPIO_NUM_39;
constexpr gpio_num_t LCD_PIN_DE = GPIO_NUM_40;
constexpr gpio_num_t LCD_PIN_PCLK = GPIO_NUM_41;
constexpr gpio_num_t LCD_PIN_DATA0 = GPIO_NUM_5;    // B0
constexpr gpio_num_t LCD_PIN_DATA1 = GPIO_NUM_45;   // B1
constexpr gpio_num_t LCD_PIN_DATA2 = GPIO_NUM_48;   // B2
constexpr gpio_num_t LCD_PIN_DATA3 = GPIO_NUM_47;   // B3
constexpr gpio_num_t LCD_PIN_DATA4 = GPIO_NUM_21;   // B4
constexpr gpio_num_t LCD_PIN_DATA5 = GPIO_NUM_14;   // G0
constexpr gpio_num_t LCD_PIN_DATA6 = GPIO_NUM_13;   // G1
constexpr gpio_num_t LCD_PIN_DATA7 = GPIO_NUM_12;   // G2
constexpr gpio_num_t LCD_PIN_DATA8 = GPIO_NUM_11;   // G3
constexpr gpio_num_t LCD_PIN_DATA9 = GPIO_NUM_10;   // G4
constexpr gpio_num_t LCD_PIN_DATA10 = GPIO_NUM_9;   // G5
constexpr gpio_num_t LCD_PIN_DATA11 = GPIO_NUM_46;  // R0
constexpr gpio_num_t LCD_PIN_DATA12 = GPIO_NUM_3;   // R1
constexpr gpio_num_t LCD_PIN_DATA13 = GPIO_NUM_8;   // R2
constexpr gpio_num_t LCD_PIN_DATA14 = GPIO_NUM_18;  // R3
constexpr gpio_num_t LCD_PIN_DATA15 = GPIO_NUM_17;  // R4

// ========== LCD SPI for Init (ST7701) ==========
constexpr gpio_num_t LCD_SPI_MOSI = GPIO_NUM_1;
constexpr gpio_num_t LCD_SPI_SCLK = GPIO_NUM_2;
constexpr gpio_num_t LCD_SPI_CS = GPIO_NUM_NC;  // CS controlled by IO Expander

// ========== LCD Specs ==========
// RGB Panel: h_res=480, v_res=480 (round panel, 2.8C variant)
constexpr uint16_t LCD_WIDTH = 480;                      // RGB panel h_res
constexpr uint16_t LCD_HEIGHT = 480;                     // RGB panel v_res
constexpr uint32_t LCD_PIXEL_CLK_HZ = 18 * 1000 * 1000;  // 18MHz
// LEDC ayarları (LEDC_MODE tanımı buraya eklendi)
constexpr ledc_mode_t LEDC_MODE = LEDC_LOW_SPEED_MODE;
constexpr ledc_channel_t LEDC_CHANNEL = LEDC_CHANNEL_0;
constexpr ledc_timer_t LEDC_TIMER = LEDC_TIMER_0;

// ========== Touch Pins ==========
constexpr gpio_num_t TOUCH_INT_PIN = GPIO_NUM_16;
constexpr gpio_num_t TOUCH_RST_PIN = GPIO_NUM_NC;

// ========== SD Card SDMMC Pins (1-bit mode) ==========
constexpr gpio_num_t SD_PIN_CLK = GPIO_NUM_2;  // SDMMC_CLK
constexpr gpio_num_t SD_PIN_CMD = GPIO_NUM_1;  // SDMMC_CMD
constexpr gpio_num_t SD_PIN_D0 = GPIO_NUM_42;  // SDMMC_D0 (1-bit mode only)

// ========== LCD Backlight ==========
constexpr gpio_num_t LCD_BACKLIGHT_PIN = GPIO_NUM_6;
constexpr uint32_t LCD_BACKLIGHT_FREQ = 4000;
constexpr uint32_t LCD_BACKLIGHT_DUTY_MAX = 8192;

// ========== I2C Addresses ==========
constexpr uint8_t TCA9554_ADDR = 0x20;
constexpr uint8_t GT911_ADDR = 0x5D;
constexpr uint8_t QMI8658_ADDR = 0x6B;
constexpr uint8_t PCF85063_ADDR = 0x51;

// ========== TCA9554 IO Expander Pins (Bit Maskeler) ==========
// DOĞRU PIN MAPPING (Waveshare'in Set_EXIO() fonksiyonundan doğrulandı):
// Set_EXIO(pin, state) → bitmask = (1 << (pin-1))
// EXIO1 (pin=1) → bit 0 → P0_0 (mask 0x01) = LCD_RESET
// EXIO2 (pin=2) → bit 1 → P0_1 (mask 0x02) = TOUCH_RESET
// EXIO3 (pin=3) → bit 2 → P0_2 (mask 0x04) = LCD_CS
// EXIO4 (pin=4) → bit 3 → P0_3 (mask 0x08) = SD_POWER
// EXIO8 (pin=8) → bit 7 → P0_7 (mask 0x80) = BUZZER
constexpr uint8_t TCA9554_LCD_RESET_MASK = (1 << 0);    // P0_0, EXIO1, LCD Reset
constexpr uint8_t TCA9554_TOUCH_RESET_MASK = (1 << 1);  // P0_1, EXIO2, Touch Reset
constexpr uint8_t TCA9554_LCD_CS_MASK = (1 << 2);       // P0_2, EXIO3, LCD CS (CRITICAL!)
constexpr uint8_t TCA9554_SD_POWER_MASK = (1 << 3);     // P0_3, EXIO4, SD Power
constexpr uint8_t TCA9554_BUZZER_MASK = (1 << 7);       // P0_7, EXIO8, Buzzer

}  // namespace BoardConfig
