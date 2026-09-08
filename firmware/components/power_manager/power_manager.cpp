/**
 * @file power_manager.cpp
 * @brief Power Manager Component Implementation
 */

#include "power_manager.hpp"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "power_constants.hpp"

constexpr static const char *TAG = "PowerManager";

using namespace TimeConversion;
using namespace BacklightLevels;
using namespace AutoSleepConfig;
using namespace BatteryConfig;

PowerManager::PowerManager()
    : currentMode(PowerMode::ACTIVE),
      initialized(false),
      autoSleepTimeoutSec(0),
      lastActivityTimestamp(0),
      autoSleepTaskHandle(nullptr),
      preSleepCallback(nullptr),
      postWakeupCallback(nullptr),
      batteryMonitoringEnabled(false),
      adcHandle(nullptr) {}


esp_err_t PowerManager::init(const Config &config) {
    if (initialized) {
        ESP_LOGW(TAG, "PowerManager already initialized");
        return ESP_OK;
    }

    pConfig = config;
    currentMode = PowerMode::ACTIVE;
    initialized = true;

    // Initialize auto-sleep
    autoSleepTimeoutSec = pConfig.autoSleepTimeoutSec;
    lastActivityTimestamp = esp_timer_get_time() / US_PER_SECOND;

    // Check wake-up reason
    esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
    switch (wakeupReason) {
        case ESP_SLEEP_WAKEUP_EXT0:
            ESP_LOGI(TAG, "Wake-up from: Touch interrupt (GPIO %d)", pConfig.touchIntPin);
            break;
        case ESP_SLEEP_WAKEUP_TIMER:
            ESP_LOGI(TAG, "Wake-up from: Timer");
            break;
        case ESP_SLEEP_WAKEUP_UNDEFINED:
        default:
            ESP_LOGI(TAG, "Wake-up from: Power-on reset or external reset");
            break;
    }

    // Initialize battery monitoring if enabled
    if (pConfig.enableBatteryMonitoring && pConfig.batteryAdcPin != GPIO_NUM_NC) {
        adc_oneshot_unit_init_cfg_t initConfig = {
            .unit_id = ADC_UNIT_1,  // Use ADC1 for GPIO1-10
            .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };

        auto *adcHandlePtr = (adc_oneshot_unit_handle_t *)malloc(sizeof(adc_oneshot_unit_handle_t));
        if (adcHandlePtr != nullptr) {
            esp_err_t ret = adc_oneshot_new_unit(&initConfig, adcHandlePtr);
            if (ret == ESP_OK) {
                adcHandle = adcHandlePtr;

                // Configure ADC channel
                adc_oneshot_chan_cfg_t chanConfig = {
                    .atten = ADC_ATTEN_DB_12,  // 0-3.3V range
                    .bitwidth = ADC_BITWIDTH_12,
                };

                // Map GPIO to ADC channel (simplified - works for GPIO1-10)
                auto channel = (adc_channel_t)(pConfig.batteryAdcPin - GPIO_NUM_1);
                ret = adc_oneshot_config_channel(*adcHandlePtr, channel, &chanConfig);

                if (ret == ESP_OK) {
                    batteryMonitoringEnabled = true;
                    ESP_LOGI(TAG, "Battery monitoring enabled on GPIO %d", pConfig.batteryAdcPin);
                } else {
                    ESP_LOGE(TAG, "Failed to configure ADC channel: %s", esp_err_to_name(ret));
                    free(adcHandlePtr);
                    adcHandle = nullptr;
                }
            } else {
                ESP_LOGE(TAG, "Failed to initialize ADC: %s", esp_err_to_name(ret));
                free(adcHandlePtr);
            }
        }
    }

    // Start auto-sleep task if enabled
    if (autoSleepTimeoutSec > 0) {
        startAutoSleepTask();
        ESP_LOGI(TAG, "Auto-sleep enabled: %lu seconds", autoSleepTimeoutSec);
    }

    ESP_LOGI(TAG, "PowerManager initialized");
    return ESP_OK;
}

esp_err_t PowerManager::setBacklight(uint8_t percent) const {
    if (!initialized) {
        ESP_LOGE(TAG, "PowerManager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (percent > 100) {
        percent = 100;
    }

    uint32_t duty = (pConfig.backlightDutyMax * percent) / 100;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, pConfig.backlightChannel, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, pConfig.backlightChannel));

    ESP_LOGI(TAG, "Backlight set to %d%%", percent);
    return ESP_OK;
}

esp_err_t PowerManager::setMode(PowerMode mode) {
    if (!initialized) {
        ESP_LOGE(TAG, "PowerManager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (mode == currentMode) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Power mode changing: %d -> %d", (int)currentMode, (int)mode);

    esp_err_t ret = ESP_OK;
    switch (mode) {
        case PowerMode::ACTIVE:
            ret = enterActiveMode();
            break;

        case PowerMode::LOW_POWER:
            ret = enterLowPowerMode();
            break;

        case PowerMode::SLEEP:
            ret = enterSleepMode();
            break;

        case PowerMode::DEEP_SLEEP:
            ret = enterDeepSleepMode();
            break;
    }

    if (ret == ESP_OK) {
        currentMode = mode;
    }

    return ret;
}

PowerManager::PowerMode PowerManager::getMode() const {
    return currentMode;
}

esp_err_t PowerManager::enterActiveMode() const {
    ESP_LOGI(TAG, "Entering ACTIVE mode...");

    // LCD'yi aç
    if (pConfig.lcdPanel != nullptr) {
        esp_lcd_panel_disp_on_off(pConfig.lcdPanel, true);
    }

    // Touch'ı uyandır (eğer sleep'teyse)
    if (pConfig.touchHandle != nullptr) {
        esp_lcd_touch_exit_sleep(pConfig.touchHandle);
    }

    // IMU aktif (varsayılan olarak zaten aktif)
    // Not: QMI8658 explicit power mode fonksiyonu sunmuyor

    // Backlight %100
    setBacklight(100);

    ESP_LOGI(TAG, "ACTIVE mode entered");
    return ESP_OK;
}

esp_err_t PowerManager::enterLowPowerMode() const {
    ESP_LOGI(TAG, "Entering LOW_POWER mode...");

    // Reduce backlight to low-power level
    setBacklight(LOW_POWER_MODE_PCT);

    // IMU low power mode (QMI8658'de low-power ODR modu kullanılabilir)
    // Bu kısım opsiyonel - gerekirse IMU'yu düşük frekansta çalıştır

    ESP_LOGI(TAG, "LOW_POWER mode entered");
    return ESP_OK;
}

esp_err_t PowerManager::enterSleepMode() {
    ESP_LOGI(TAG, "Entering SLEEP mode...");

    // Call pre-sleep callback (e.g., unmount SD card)
    if (preSleepCallback != nullptr) {
        ESP_LOGI(TAG, "Calling pre-sleep callback...");
        preSleepCallback();
    }

    // Backlight kapat
    setBacklight(0);

    // LCD panel'i kapat (RGB clock hala çalışıyor ama display kapalı)
    if (pConfig.lcdPanel != nullptr) {
        esp_lcd_panel_disp_on_off(pConfig.lcdPanel, false);
    }

    // Touch'ı sleep mode'a al (interrupt hala aktif)
    if (pConfig.touchHandle != nullptr) {
        esp_lcd_touch_enter_sleep(pConfig.touchHandle);
    }

    // IMU suspend (QMI8658 için şu an aktif değil - gerekirse eklenebilir)

    // Touch interrupt ile uyanma ayarla
    if (pConfig.touchIntPin != GPIO_NUM_NC) {
        esp_sleep_enable_ext0_wakeup(pConfig.touchIntPin, 0);  // LOW seviyesinde uyan
    }

    ESP_LOGI(TAG, "SLEEP mode entered - waiting for touch interrupt...");

    // Light sleep'e gir
    esp_light_sleep_start();

    // Buraya uyanınca gelir
    ESP_LOGI(TAG, "Woke up from SLEEP mode");

    // Call post-wakeup callback (e.g., remount SD card)
    if (postWakeupCallback != nullptr) {
        ESP_LOGI(TAG, "Calling post-wakeup callback...");
        postWakeupCallback();
    }

    // Otomatik olarak ACTIVE mode'a dön
    return setMode(PowerMode::ACTIVE);
}

esp_err_t PowerManager::enterDeepSleepMode() {
    ESP_LOGI(TAG, "Entering DEEP_SLEEP mode...");

    // Stop auto-sleep task before entering deep sleep
    stopAutoSleepTask();

    // Call pre-sleep callback (e.g., unmount SD card)
    if (preSleepCallback != nullptr) {
        ESP_LOGI(TAG, "Calling pre-sleep callback...");
        preSleepCallback();
    }

    // Backlight kapat
    setBacklight(0);

    // LCD panel'i kapat
    if (pConfig.lcdPanel != nullptr) {
        esp_lcd_panel_disp_on_off(pConfig.lcdPanel, false);
    }

    // Timer ile uyanma ayarla
    if (pConfig.deepSleepWakeupSec > 0) {
        esp_sleep_enable_timer_wakeup(pConfig.deepSleepWakeupSec * US_PER_SECOND);
        ESP_LOGI(TAG, "DEEP_SLEEP mode - will wake up in %lu seconds", pConfig.deepSleepWakeupSec);
    }

    // Touch interrupt ile uyanma ayarla
    if (pConfig.touchIntPin != GPIO_NUM_NC) {
        esp_sleep_enable_ext0_wakeup(pConfig.touchIntPin, 0);  // LOW seviyesinde uyan
    }

    // Deep sleep'e gir (buradan sonra reset gibi baştan başlar)
    esp_deep_sleep_start();

    // Buraya hiç gelmez
    return ESP_OK;
}

// ========================================
// Auto-Sleep Implementation
// ========================================

esp_err_t PowerManager::setAutoSleepTimeout(uint32_t timeoutSec) {
    if (!initialized) {
        ESP_LOGE(TAG, "PowerManager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    bool wasEnabled = (autoSleepTimeoutSec > 0);
    autoSleepTimeoutSec = timeoutSec;

    if (timeoutSec > 0) {
        if (!wasEnabled) {
            startAutoSleepTask();
        }
        resetAutoSleepTimer();
        ESP_LOGI(TAG, "Auto-sleep timeout set to %lu seconds", timeoutSec);
    } else {
        if (wasEnabled) {
            stopAutoSleepTask();
        }
        ESP_LOGI(TAG, "Auto-sleep disabled");
    }

    return ESP_OK;
}

void PowerManager::resetAutoSleepTimer() {
    lastActivityTimestamp = esp_timer_get_time() / US_PER_SECOND;
}

bool PowerManager::isAutoSleepEnabled() const {
    return (autoSleepTimeoutSec > 0 && autoSleepTaskHandle != nullptr);
}

uint32_t PowerManager::getTimeUntilAutoSleep() const {
    if (autoSleepTimeoutSec == 0) {
        return 0;
    }

    int64_t currentTime = esp_timer_get_time() / US_PER_SECOND;
    int64_t elapsedTime = currentTime - lastActivityTimestamp;

    if (elapsedTime >= autoSleepTimeoutSec) {
        return 0;
    }

    return autoSleepTimeoutSec - elapsedTime;
}

const char *PowerManager::getWakeupReason() {
    esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
    switch (wakeupReason) {
        case ESP_SLEEP_WAKEUP_EXT0:
            return "Touch Interrupt";
        case ESP_SLEEP_WAKEUP_TIMER:
            return "Timer";
        case ESP_SLEEP_WAKEUP_UNDEFINED:
        default:
            return "Power-on Reset";
    }
}

void PowerManager::autoSleepTask(void *pvParameters) {
    auto *self = static_cast<PowerManager *>(pvParameters);
    ESP_LOGI(TAG, "Auto-sleep task started");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(CHECK_INTERVAL_MS));

        if (self->autoSleepTimeoutSec == 0) {
            continue;  // Auto-sleep disabled
        }

        // Skip if already in sleep mode
        if (self->currentMode == PowerMode::SLEEP || self->currentMode == PowerMode::DEEP_SLEEP) {
            continue;
        }

        uint32_t timeRemaining = self->getTimeUntilAutoSleep();

        if (timeRemaining == 0) {
            ESP_LOGI(TAG, "Auto-sleep timeout expired - entering sleep mode");
            self->setMode(PowerMode::SLEEP);
            // After waking up from sleep, reset the timer
            self->resetAutoSleepTimer();
        } else if (timeRemaining <= WARNING_THRESHOLD_SEC && timeRemaining % WARNING_INTERVAL_SEC == 0) {
            ESP_LOGI(TAG, "Auto-sleep in %lu seconds...", timeRemaining);
        }
    }
}

void PowerManager::startAutoSleepTask() {
    if (autoSleepTaskHandle != nullptr) {
        ESP_LOGW(TAG, "Auto-sleep task already running");
        return;
    }

    xTaskCreate(
        autoSleepTask,
        "power_autosleep",
        TASK_STACK_SIZE,
        this,
        TASK_PRIORITY,
        &autoSleepTaskHandle
    );

    ESP_LOGI(TAG, "Auto-sleep task started");
}

void PowerManager::stopAutoSleepTask() {
    if (autoSleepTaskHandle != nullptr) {
        vTaskDelete(autoSleepTaskHandle);
        autoSleepTaskHandle = nullptr;
        ESP_LOGI(TAG, "Auto-sleep task stopped");
    }
}

// ========================================
// Callback Registration
// ========================================

void PowerManager::registerPreSleepCallback(PreSleepCallback callback) {
    preSleepCallback = callback;
    if (callback != nullptr) {
        ESP_LOGI(TAG, "Pre-sleep callback registered");
    } else {
        ESP_LOGI(TAG, "Pre-sleep callback unregistered");
    }
}

void PowerManager::registerPostWakeupCallback(PostWakeupCallback callback) {
    postWakeupCallback = callback;
    if (callback != nullptr) {
        ESP_LOGI(TAG, "Post-wakeup callback registered");
    } else {
        ESP_LOGI(TAG, "Post-wakeup callback unregistered");
    }
}

// ========================================
// Battery Monitoring
// ========================================

uint16_t PowerManager::getBatteryVoltage() const {
    if (!batteryMonitoringEnabled || adcHandle == nullptr) {
        return 0;
    }

    auto *handle = (adc_oneshot_unit_handle_t *)adcHandle;
    auto channel = (adc_channel_t)(pConfig.batteryAdcPin - GPIO_NUM_1);

    int rawValue = 0;
    esp_err_t ret = adc_oneshot_read(*handle, channel, &rawValue);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read ADC: %s", esp_err_to_name(ret));
        return 0;
    }

    // Convert raw ADC value to millivolts
    // ADC_ATTEN_DB_12 gives 0-3100mV range for ESP32-S3
    // 12-bit ADC = 4096 steps
    // Assuming 2:1 voltage divider for battery (0-6.2V battery -> 0-3.1V ADC)
    uint16_t voltageMv = (rawValue * ADC_MAX_MV / ADC_12BIT_MAX) * VOLTAGE_DIVIDER_RATIO;

    return voltageMv;
}

uint8_t PowerManager::getBatteryPercent() const {
    using namespace VoltageThresholds;
    uint16_t voltage = getBatteryVoltage();

    if (voltage == 0) {
        return 0;
    }

    // LiPo battery voltage-to-percentage curve
    if (voltage >= FULL_MV) {
        return 100;
    }
    if (voltage <= EMPTY_MV) {
        return 0;
    } else if (voltage >= NOMINAL_MV) {
        // Linear from 50% to 100% (3700mV to 4200mV)
        return 50 + ((voltage - NOMINAL_MV) * 50 / RANGE_FULL_TO_NOMINAL);
    } else if (voltage >= LOW_MV) {
        // Linear from 10% to 50% (3400mV to 3700mV)
        return 10 + ((voltage - LOW_MV) * 40 / RANGE_NOMINAL_TO_LOW);
    } else {
        // Linear from 0% to 10% (3000mV to 3400mV)
        return (voltage - EMPTY_MV) * 10 / RANGE_LOW_TO_EMPTY;
    }
}

bool PowerManager::isOnBattery() const {
    uint16_t voltage = getBatteryVoltage();

    if (voltage == 0) {
        return false;  // Unknown
    }

    // If voltage is less than USB threshold, likely on battery
    // USB power is typically 5V, battery is 3.0-4.2V
    return (voltage < USB_POWER_THRESHOLD_MV);
}
