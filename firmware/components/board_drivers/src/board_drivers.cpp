/**
 * @file board_drivers.cpp
 * @brief ESP32-S3 Touch LCD 2.8B Board Drivers - Clean Implementation with NEW I2C API
 * @version 2.0
 * @date 2025-10-29
 *
 * Clean rewrite using NEW I2C Master API (i2c_master.h)
 * - i2c_new_master_bus()
 * - i2c_master_bus_add_device()
 * - i2c_master_transmit()
 */

#include "board_drivers.hpp"
#include <cstdlib>
#include "board_constants.hpp"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gt911_simple.hpp"    // Simple GT911 driver
#include "lcd_st7701_rgb.hpp"  // New modular LCD driver
#include "pcf85063a.h"         // Waveshare RTC driver
#include "qmi8658.h"           // Waveshare IMU driver
#include "sd_services.hpp"     // SD Card services

constexpr static const char *TAG = "BoardDrivers_V2";

namespace BoardDrivers {

namespace {
// Created in initI2C, before any task can be reading. Creating it lazily on
// first use races: two tasks arriving together each make their own mutex and
// neither excludes the other.
SemaphoreHandle_t gI2cMutex = nullptr;
i2c_master_bus_handle_t gI2cBus = nullptr;
}  // namespace

bool i2cLock(uint32_t timeoutMs) {
    if (gI2cMutex == nullptr) {
        return false;
    }
    return xSemaphoreTake(gI2cMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

esp_err_t i2cRecover() {
    if (gI2cBus == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    // A transaction cut short can leave a device holding the data line down,
    // and from then on every read reports the bus as busy. Resetting the
    // controller clocks the bus free again.
    ESP_LOGW(TAG, "Resetting a jammed I2C bus");
    return i2c_master_bus_reset(gI2cBus);
}

void i2cUnlock() {
    if (gI2cMutex != nullptr) {
        xSemaphoreGive(gI2cMutex);
    }
}

using namespace BoardConfig;
using namespace BoardDrivers;

// =============================================================================
// STEP 1: I2C BUS INITIALIZATION (NEW API)
// =============================================================================

esp_err_t initI2C(i2c_master_bus_handle_t *handle) {
    ESP_LOGI(TAG, "=== STEP 1: Initializing I2C Bus (NEW API) ===");

    i2c_master_bus_config_t busConfig = {};
    busConfig.i2c_port = I2C_NUM_0;
    busConfig.sda_io_num = I2C_SDA_PIN;
    busConfig.scl_io_num = I2C_SCL_PIN;
    busConfig.clk_source = I2C_CLK_SRC_DEFAULT;

    // Without a glitch filter the hardware reads every spike on the line as a
    // start or stop condition, and the bus is then reported as permanently
    // busy: touch stops responding until the controller is reset. Seven is
    // the value Espressif recommends, and it was left commented out here.
    busConfig.glitch_ignore_cnt = 7;
    busConfig.flags.enable_internal_pullup = true;

    esp_err_t ret = i2c_new_master_bus(&busConfig, handle);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C master bus: %s", esp_err_to_name(ret));
        return ret;
    }

    gI2cBus = *handle;
    gI2cMutex = xSemaphoreCreateMutex();
    if (gI2cMutex == nullptr) {
        ESP_LOGE(TAG, "Failed to create the I2C mutex");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "✓ I2C Bus initialized (SDA=GPIO%d, SCL=GPIO%d)", I2C_SDA_PIN, I2C_SCL_PIN);

    return ESP_OK;
}

// =============================================================================
// STEP 2: BACKLIGHT PWM INITIALIZATION
// =============================================================================

esp_err_t initBacklight(ledc_channel_t *channel) {
    ESP_LOGI(TAG, "=== STEP 2: Initializing Backlight PWM ===");

    *channel = LEDC_CHANNEL_0;

    // Configure LEDC timer
    ledc_timer_config_t ledcTimer = {};
    ledcTimer.speed_mode = LEDC_LOW_SPEED_MODE;
    ledcTimer
        .duty_resolution = LEDC_TIMER_13_BIT;  // 13-bit = 8192 levels for smooth backlight control
    ledcTimer.timer_num = LEDC_TIMER_0;
    ledcTimer.freq_hz = LCD_BACKLIGHT_FREQ;
    ledcTimer.clk_cfg = LEDC_AUTO_CLK;

    esp_err_t ret = ledc_timer_config(&ledcTimer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config LEDC timer: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure LEDC channel
    ledc_channel_config_t ledcChannel = {};
    ledcChannel.gpio_num = LCD_BACKLIGHT_PIN;
    ledcChannel.speed_mode = LEDC_LOW_SPEED_MODE;
    ledcChannel.channel = *channel;
    ledcChannel.intr_type = LEDC_INTR_DISABLE;
    ledcChannel.timer_sel = LEDC_TIMER_0;
    ledcChannel.duty = BacklightConfig::DUTY_OFF;  // Start with backlight off
    ledcChannel.hpoint = 0;

    ret = ledc_channel_config(&ledcChannel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config LEDC channel: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(
        TAG,
        "✓ Backlight PWM initialized (GPIO%d, %dHz)",
        LCD_BACKLIGHT_PIN,
        LCD_BACKLIGHT_FREQ
    );
    return ESP_OK;
}

// =============================================================================
// STEP 3: IO EXPANDER (TCA9554) IMPLEMENTATION
// =============================================================================
esp_err_t initIOExpander(i2c_master_bus_handle_t i2cBus, esp_io_expander_handle_t *handle) {
    ESP_LOGI(TAG, "Initializing IO expander (NEW I2C API)...");
    esp_err_t ret = ESP_OK;
    ret = esp_io_expander_new_i2c_tca9554(i2cBus, I2C_ADDRESS, handle);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add TCA9554 to I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // KRİTİK: Waveshare gibi TÜM pinleri OUTPUT moduna ayarla
    // TCA9554'te 8 pin var (P0_0 - P0_7), hepsini OUTPUT yap
    ESP_LOGI(TAG, "Setting all TCA9554 pins to OUTPUT mode (Waveshare style)");

    ret = esp_io_expander_set_dir(*handle, TCA9554Config::ALL_PINS_MASK, IO_EXPANDER_OUTPUT);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure TCA9554 pins: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "✓ TCA9554 all pins set to OUTPUT mode");

    // KRİTİK: Buzzer'ı KAPAT (P0_7 = bit 7 = 0x80)
    // Waveshare'de Buzzer_Off() yapılıyor
    ESP_LOGI(TAG, "Turning off buzzer (P0_7)");
    ret = esp_io_expander_set_level(*handle, TCA9554Config::BUZZER_PIN_MASK, 0);  // LOW = OFF

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to turn off buzzer: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "✓ Buzzer turned off");

    // KRİTİK: Diğer pinlerin başlangıç değerlerini ayarla
    // P0_0 (LCD Reset) = HIGH (1) - aktif değil
    // P0_1 (LCD CS) = HIGH (1) - aktif değil (LOW aktif)
    // P0_2 (LCD Power) = LOW (0) - henüz açma (daha sonra açılacak)
    // P0_3-P0_6 = LOW (0) - kullanılmıyor
    // P0_7 (Buzzer) = zaten kapatıldı

    ESP_LOGI(TAG, "Setting initial pin states");

    // Reset ve CS pinlerini HIGH yap (deaktif)
    ret = esp_io_expander_set_level(*handle, TCA9554Config::LCD_CONTROL_MASK, 1);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set initial HIGH pins: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "✓ Initial pin states configured");

    return ESP_OK;
}

// =============================================================================
// STEP 4: LCD (ST7701) INITIALIZATION
// =============================================================================

esp_err_t initLCD(esp_io_expander_handle_t ioExpander, lcd::ST7701Handle **handle) {
    ESP_LOGI(TAG, "=== STEP 4: Initializing LCD (ST7701 + RGB) - New Driver ===");

    // Configure LCD initialization using new modular driver
    lcd::ST7701Config config = {
        .spiMosi = LCD_SPI_MOSI,
        .spiSclk = LCD_SPI_SCLK,
        .ioExpander = ioExpander,
        .width = LCD_WIDTH,
        .height = LCD_HEIGHT,
        .pixelClockHz = LCD_PIXEL_CLK_HZ,
    };

    // Initialize LCD using new driver
    esp_err_t ret = lcd::st7701RgbInit(config, handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LCD with new driver: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "✓ LCD (ST7701 + RGB) initialized successfully with new driver");
    return ESP_OK;
}

// =============================================================================
// STEP 5: TOUCH (GT911) INITIALIZATION
// =============================================================================

esp_err_t initTouch(
    i2c_master_bus_handle_t i2cBus,
    esp_io_expander_handle_t ioExpander,
    gt911::GT911Handle **handle
) {
    ESP_LOGI(TAG, "=== STEP 5: Initializing Touch (GT911 - Simple Driver) ===");

    // Step 1: Configure INT pin as output for reset sequence
    gpio_config_t ioConf = {};
    ioConf.pin_bit_mask = (1ULL << TOUCH_INT_PIN);
    ioConf.mode = GPIO_MODE_OUTPUT;
    ioConf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    ioConf.pull_up_en = GPIO_PULLUP_DISABLE;
    ioConf.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&ioConf));

    // Step 2: GT911 Reset Sequence (Waveshare style)
    ESP_LOGI(TAG, "Performing GT911 reset sequence...");

    // INT pin LOW
    gpio_set_level(TOUCH_INT_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(GT911Timing::RESET_DELAY_MS));

    // Reset pin (EXIO2 = P0_1): LOW -> HIGH
    ESP_ERROR_CHECK(esp_io_expander_set_level(ioExpander, TCA9554_TOUCH_RESET_MASK, 0));
    vTaskDelay(pdMS_TO_TICKS(GT911Timing::RESET_DELAY_MS));
    ESP_ERROR_CHECK(esp_io_expander_set_level(ioExpander, TCA9554_TOUCH_RESET_MASK, 1));
    vTaskDelay(pdMS_TO_TICKS(GT911Timing::PULSE_DELAY_MS));

    // INT pin HIGH, then switch to INPUT
    gpio_set_level(TOUCH_INT_PIN, 1);
    ioConf.mode = GPIO_MODE_INPUT;
    ESP_ERROR_CHECK(gpio_config(&ioConf));

    // Wait for GT911 to boot up after reset
    vTaskDelay(pdMS_TO_TICKS(GT911Timing::BOOT_DELAY_MS));

    ESP_LOGI(TAG, "✓ GT911 reset complete");

    // Step 3: Initialize GT911 using simple driver (direct I2C access)
    esp_err_t ret = gt911::init(i2cBus, GT911_ADDR, handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize GT911: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "✓ GT911 touch controller initialized with simple driver");
    return ESP_OK;
}

// =============================================================================
// STEP 6: IMU (QMI8658) INITIALIZATION
// =============================================================================

esp_err_t initIMU(i2c_master_bus_handle_t i2cBus, void **handle) {
    ESP_LOGI(TAG, "=== STEP 6: Initializing IMU (QMI8658) ===");

    // Allocate QMI8658 device structure
    auto *imuDev = (qmi8658_dev_t *)malloc(sizeof(qmi8658_dev_t));

    if (imuDev == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate QMI8658 device structure");
        return ESP_ERR_NO_MEM;
    }

    // Initialize QMI8658 using Waveshare managed component
    esp_err_t ret = qmi8658_init(imuDev, i2cBus, QMI8658_ADDR);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize QMI8658: %s", esp_err_to_name(ret));
        free(imuDev);

        return ret;
    }

    // Verify WHO_AM_I
    uint8_t whoAmI = 0;
    ret = qmi8658_get_who_am_i(imuDev, &whoAmI);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "QMI8658 WHO_AM_I: 0x%02X (expected: 0x05)", whoAmI);
        if (whoAmI != QMI8658Config::EXPECTED_WHO_AM_I) {
            ESP_LOGW(TAG, "Unexpected WHO_AM_I value!");
        }
    }

    // Configure accelerometer: 8G range, 1000Hz ODR
    ret = qmi8658_set_accel_range(imuDev, QMI8658_ACCEL_RANGE_8G);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set accelerometer range");
    }

    ret = qmi8658_set_accel_odr(imuDev, QMI8658_ACCEL_ODR_1000HZ);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set accelerometer ODR");
    }

    // Configure gyroscope: 512DPS range, 1000Hz ODR
    ret = qmi8658_set_gyro_range(imuDev, QMI8658_GYRO_RANGE_512DPS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set gyroscope range");
    }

    ret = qmi8658_set_gyro_odr(imuDev, QMI8658_GYRO_ODR_1000HZ);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set gyroscope ODR");
    }

    // Set units: m/s² for accel, rad/s for gyro
    qmi8658_set_accel_unit_mps2(imuDev, true);
    qmi8658_set_gyro_unit_rads(imuDev, true);

    // Enable accelerometer and gyroscope
    ret = qmi8658_enable_sensors(imuDev, QMI8658_ENABLE_ACCEL | QMI8658_ENABLE_GYRO);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enable sensors");
    }

    ESP_LOGI(TAG, "✓ QMI8658 initialized successfully");
    ESP_LOGI(TAG, "  - Accelerometer: 8G range, 1000Hz ODR, m/s² units");
    ESP_LOGI(TAG, "  - Gyroscope: 512DPS range, 1000Hz ODR, rad/s units");

    *handle = (void *)imuDev;
    return ESP_OK;
}

// =============================================================================
// STEP 7: RTC (PCF85063) INITIALIZATION
// =============================================================================

esp_err_t initRTC(i2c_master_bus_handle_t i2cBus, void **handle) {
    ESP_LOGI(TAG, "=== STEP 7: Initializing RTC (PCF85063) ===");

    if (i2cBus == nullptr || handle == nullptr) {
        ESP_LOGE(TAG, "Invalid I2C bus or handle pointer");
        return ESP_ERR_INVALID_ARG;
    }

    // Allocate RTC device struct
    auto *rtc = (pcf85063a_dev_t *)malloc(sizeof(pcf85063a_dev_t));
    if (rtc == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate RTC device struct");
        return ESP_ERR_NO_MEM;
    }

    // Initialize RTC
    esp_err_t ret = pcf85063a_init(rtc, i2cBus, PCF85063_ADDR);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize RTC: %s", esp_err_to_name(ret));
        free(rtc);
        return ret;
    }

    *handle = (void *)rtc;
    ESP_LOGI(TAG, "✓ RTC (PCF85063) initialized at I2C address 0x%02X", PCF85063_ADDR);

    return ESP_OK;
}

// =============================================================================
// INITIALIZE ALL HARDWARE
// =============================================================================

esp_err_t initAll(HardwareHandles &handles) {
    ESP_LOGI(TAG, "Initializing board hardware...");

    // Initialize I2C bus
    ESP_ERROR_CHECK(initI2C(&handles.i2cBus));

    // Initialize backlight PWM
    ESP_ERROR_CHECK(initBacklight(&handles.backlightChannel));

    // Initialize IO Expander (TCA9554)
    ESP_ERROR_CHECK(initIOExpander(handles.i2cBus, &handles.ioExpander));

    // Initialize LCD (ST7701 + RGB)
    ESP_ERROR_CHECK(initLCD(handles.ioExpander, &handles.lcdHandle));

    // Initialize Touch (GT911)
    ESP_ERROR_CHECK(initTouch(handles.i2cBus, handles.ioExpander, &handles.touch));

    // Initialize IMU (QMI8658)
    ESP_ERROR_CHECK(initIMU(handles.i2cBus, &handles.imu));

    // Initialize RTC (PCF85063)
    ESP_ERROR_CHECK(initRTC(handles.i2cBus, &handles.rtc));

    // Setup SD Card Services (set IO expander handle for power control)
    // Note: SD card is NOT automatically mounted here. User must call sd_services::initSD()
    sd_services::setIOExpanderHandle(handles.ioExpander);
    ESP_LOGI(TAG, "✓ SD services configured (call sd_services::initSD() to mount)");

    ESP_LOGI(TAG, "✓ All hardware initialized successfully");

    return ESP_OK;
}

}  // namespace BoardDrivers
