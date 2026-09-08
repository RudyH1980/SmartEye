/**
 * @file power_manager.hpp
 * @brief Power Manager Component - Battery saving and power mode management
 * @details Merkezi güç yönetimi: backlight, LCD, touch, IMU kontrolü
 */

#pragma once

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"

/**
 * @brief Power Manager Sınıfı
 * @details Tüm donanım güç yönetimini merkezi olarak kontrol eder
 */
class PowerManager {
   public:
    /**
     * @brief Güç modları
     */
    enum class PowerMode {
        ACTIVE,     // Tam performans - tüm donanım aktif (~250-350mA)
        LOW_POWER,  // Backlight düşük - güç tasarrufu (~50-100mA)
        SLEEP,      // Ekran kapalı - touch interrupt ile uyanma (~5-10mA)
        DEEP_SLEEP  // Sadece RTC - timer ile uyanma (~100-500µA)
    };

    /**
     * @brief Güç yöneticisi konfigürasyonu
     */
    struct Config {
        // Hardware handles
        esp_lcd_panel_handle_t lcdPanel;
        esp_lcd_touch_handle_t touchHandle;

        // Backlight PWM
        ledc_channel_t backlightChannel;
        uint32_t backlightDutyMax;

        // Wakeup pins
        gpio_num_t touchIntPin;

        // Deep sleep timer (seconds)
        uint32_t deepSleepWakeupSec;

        // Auto-sleep timeout (seconds, 0 = disabled)
        uint32_t autoSleepTimeoutSec;

        // Battery monitoring (optional)
        bool enableBatteryMonitoring;
        gpio_num_t batteryAdcPin;  // GPIO for battery voltage sensing (e.g., GPIO1-10)
    };

    /**
     * @brief Constructor
     */
    PowerManager();

    /**
     * @brief Destructor
     */
    ~PowerManager() = default;

    // Kopyalama işlemlerini engelle
    PowerManager(const PowerManager &) = delete;
    PowerManager &operator=(const PowerManager &) = delete;


    // Taşıma işlemlerini engelle
    PowerManager(PowerManager &&) = delete;
    PowerManager &operator=(PowerManager &&) = delete;

    /**
     * @brief Güç yöneticisini başlat
     * @param config Konfigürasyon
     * @return ESP_OK on success
     */
    esp_err_t init(const Config &config);

    /**
     * @brief Backlight parlaklığını ayarla (0-100%)
     * @param percent Parlaklık yüzdesi (0-100)
     * @return ESP_OK on success
     */
    esp_err_t setBacklight(uint8_t percent) const;

    /**
     * @brief Güç modunu değiştir
     * @param mode Yeni güç modu
     * @return ESP_OK on success
     */
    esp_err_t setMode(PowerMode mode);

    /**
     * @brief Mevcut güç modunu al
     * @return Mevcut güç modu
     */
    [[nodiscard]] PowerMode getMode() const;

    /**
     * @brief Auto-sleep timeout'u ayarla (0 = devre dışı)
     * @param timeoutSec Timeout süresi (saniye)
     * @return ESP_OK on success
     */
    esp_err_t setAutoSleepTimeout(uint32_t timeoutSec);

    /**
     * @brief Auto-sleep timer'ı sıfırla (kullanıcı aktivitesinde çağrılmalı)
     */
    void resetAutoSleepTimer();

    /**
     * @brief Auto-sleep'in aktif olup olmadığını kontrol et
     * @return true if enabled
     */
    [[nodiscard]] bool isAutoSleepEnabled() const;

    /**
     * @brief Auto-sleep'e kalan süreyi al
     * @return Kalan saniye (0 = devre dışı veya süresi doldu)
     */
    [[nodiscard]] uint32_t getTimeUntilAutoSleep() const;

    /**
     * @brief Wake-up nedenini al
     * @return Wake-up nedeni string
     */
    [[nodiscard]] static const char *getWakeupReason();

    /**
     * @brief Pre-sleep callback function type
     * Called before entering sleep to allow peripheral shutdown
     */
    using PreSleepCallback = void (*)();

    /**
     * @brief Post-wakeup callback function type
     * Called after waking up to allow peripheral restoration
     */
    using PostWakeupCallback = void (*)();

    /**
     * @brief Register pre-sleep callback (e.g., SD card unmount)
     * @param callback Function to call before sleep (nullptr to unregister)
     */
    void registerPreSleepCallback(PreSleepCallback callback);

    /**
     * @brief Register post-wakeup callback (e.g., SD card remount)
     * @param callback Function to call after wakeup (nullptr to unregister)
     */
    void registerPostWakeupCallback(PostWakeupCallback callback);

    /**
     * @brief Get battery voltage in millivolts
     * @return Battery voltage (0 if not available/configured)
     */
    [[nodiscard]] uint16_t getBatteryVoltage() const;

    /**
     * @brief Get estimated battery percentage (0-100)
     * @return Battery percentage (0 if not available)
     */
    [[nodiscard]] uint8_t getBatteryPercent() const;

    /**
     * @brief Check if running on battery power
     * @return true if on battery, false if USB powered or unknown
     */
    [[nodiscard]] bool isOnBattery() const;

   private:
    Config pConfig{};
    PowerMode currentMode;
    bool initialized;

    // Auto-sleep timer
    uint32_t autoSleepTimeoutSec;
    int64_t lastActivityTimestamp;
    TaskHandle_t autoSleepTaskHandle;

    // Callbacks
    PreSleepCallback preSleepCallback;
    PostWakeupCallback postWakeupCallback;

    // Battery monitoring
    bool batteryMonitoringEnabled;
    void *adcHandle;  // ADC oneshot handle (opaque pointer)

    // Mode transition functions
    esp_err_t enterActiveMode() const;
    esp_err_t enterLowPowerMode() const;
    esp_err_t enterSleepMode();
    esp_err_t enterDeepSleepMode();

    // Auto-sleep task
    static void autoSleepTask(void *pvParameters);
    void startAutoSleepTask();
    void stopAutoSleepTask();
};
