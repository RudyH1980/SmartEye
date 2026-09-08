/**
 * @file main.cpp
 * @brief ESP32-S3 Touch LCD 2.8B - Main Application with LVGL
 * @details Clean modular structure with LVGL GUI framework
 *
 * Hardware: ESP32-S3-Touch-LCD-2.8B (Waveshare)
 * - 2.8" LCD (480x640, ST7701 driver)
 * - GT911 Touch Controller
 * - QMI8658 6-axis IMU
 * - PCF85063 RTC
 * - TCA9554 IO Expander
 */

#include <cstdio>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

// Board drivers
#include "board_drivers.hpp"

// LVGL wrapper
#include "lvgl_wrapper.hpp"

// UI
#include "ui.hpp"

// Test functions (optional)
#include "test.hpp"

#include "portal_demo.hpp"

constexpr static const char *TAG = "MAIN";

/**
 * @brief Available demo modes
 */
enum class DemoMode {
    CORNER_SQUARES,   // Test corner squares (coordinate verification)
    TOUCH_DRAWING,    // Touch drawing test
    IMU_CONSOLE,      // IMU sensor test (console only)
    IMU_3D_CUBE,      // IMU 3D cube visualization (LVGL GUI)
    RTC_CLOCK,        // Real-time clock display (LVGL GUI)
    PORTAL_CLIMATE,   // Portal-style climate screen on mock data
    LVGL_SOLID,       // Bisect aid: one flat colour through LVGL, no widgets
    SD_CARD_DEMO,     // SD Card info, file browser, and format (LVGL GUI)
    POWER_MGMT_TEST,  // Power Management test with SD Card integration (LVGL GUI)
    DEFAULT_UI,       // Default LVGL UI with clock
};

// ========================================
// SELECT DEMO MODE HERE
// ========================================
constexpr DemoMode ACTIVE_MODE = DemoMode::PORTAL_CLIMATE;
// ========================================

/**
 * @brief Configure log levels for different components
 */
static void configureLogLevels() {
    esp_log_level_set("*", ESP_LOG_INFO);
    // Optional: Enable DEBUG for specific components during development
    // esp_log_level_set("BoardDrivers_V2", ESP_LOG_DEBUG);
    // esp_log_level_set("LCD_ST7701_RGB", ESP_LOG_DEBUG);
    // esp_log_level_set("LVGL", ESP_LOG_DEBUG);
}

/**
 * @brief Initialize NVS Flash
 */
static void initNvs() {
    ESP_LOGI(TAG, "Initializing NVS...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");
}

/**
 * @brief Main application entry point
 */
extern "C" void app_main(void) {
    configureLogLevels();

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "ESP32-S3 Touch LCD 2.8B - LVGL Demo");
    ESP_LOGI(TAG, "========================================");

    // 1. Initialize NVS
    initNvs();

    // 2. Initialize all board hardware
    BoardDrivers::HardwareHandles hw = {};
    ESP_LOGI(TAG, "Initializing hardware drivers...");
    ESP_ERROR_CHECK(BoardDrivers::initAll(hw));

    // 3. Turn on backlight at 80%
    ESP_LOGI(TAG, "Setting backlight to 80%%");
    tests::setBacklight(hw, 80);

    // 4. Clear screen to black
    ESP_LOGI(TAG, "Clearing screen to black");
    tests::fillScreen(hw, 0x0000);

    ESP_LOGI(TAG, "Hardware initialization complete!");

    // ==========================================
    // RUN SELECTED DEMO MODE
    // ==========================================
    ESP_LOGI(TAG, "Starting demo mode...");

    switch (ACTIVE_MODE) {
        case DemoMode::CORNER_SQUARES:
            ESP_LOGI(TAG, "MODE: Corner Squares Test");
            tests::testCornerSquares(hw);
            while (true) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            break;

        case DemoMode::TOUCH_DRAWING:
            ESP_LOGI(TAG, "MODE: Touch Drawing Test");
            tests::demoTouchDrawing(hw);
            break;

        case DemoMode::IMU_CONSOLE:
            ESP_LOGI(TAG, "MODE: IMU Console Test");
            tests::demoIMU(hw);
            break;

        case DemoMode::IMU_3D_CUBE: {
            ESP_LOGI(TAG, "MODE: IMU 3D Cube Visualization");

            // Initialize LVGL for GUI
            lvgl_wrapper::LvglHandles lvglHandles = {};
            ESP_ERROR_CHECK(lvgl_wrapper::init(hw, lvglHandles));

            // Start IMU 3D cube demo (never returns)
            tests::demoIMU_GUI(hw);
            break;
        }

        case DemoMode::RTC_CLOCK: {
            ESP_LOGI(TAG, "MODE: Real-Time Clock Display");

            // Initialize LVGL for GUI
            lvgl_wrapper::LvglHandles lvglHandles = {};
            ESP_ERROR_CHECK(lvgl_wrapper::init(hw, lvglHandles));

            // Start RTC clock demo (never returns)
            tests::demoRTCClock(hw);
            break;
        }

        case DemoMode::PORTAL_CLIMATE: {
            ESP_LOGI(TAG, "MODE: Portal Climate Screen");

            lvgl_wrapper::LvglHandles lvglHandles = {};
            ESP_ERROR_CHECK(lvgl_wrapper::init(hw, lvglHandles));

            portal_demo::runPortal(hw);
            break;
        }

        case DemoMode::LVGL_SOLID: {
            ESP_LOGI(TAG, "MODE: LVGL Solid Colour (flush-path bisect)");

            lvgl_wrapper::LvglHandles lvglHandles = {};
            ESP_ERROR_CHECK(lvgl_wrapper::init(hw, lvglHandles));

            if (lvgl_wrapper::lock(0)) {
                lv_obj_t *scr = lv_screen_active();
                lv_obj_set_style_bg_color(scr, lv_color_hex(0x0000FF), 0);
                lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
                lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_NONE, 0);
                lvgl_wrapper::unlock();
            }

            while (true) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            break;
        }

        case DemoMode::SD_CARD_DEMO: {
            ESP_LOGI(TAG, "MODE: SD Card Demo");

            // Initialize LVGL for GUI
            lvgl_wrapper::LvglHandles lvglHandles = {};
            ESP_ERROR_CHECK(lvgl_wrapper::init(hw, lvglHandles));

            // Start SD card demo (never returns)
            tests::demoSDCard(hw);
            break;
        }

        case DemoMode::POWER_MGMT_TEST: {
            ESP_LOGI(TAG, "MODE: Power Management Test");

            // Initialize LVGL for GUI
            lvgl_wrapper::LvglHandles lvglHandles = {};
            ESP_ERROR_CHECK(lvgl_wrapper::init(hw, lvglHandles));

            // Start power management test (never returns)
            tests::testPowerManagement(hw);
            break;
        }

        case DemoMode::DEFAULT_UI: {
            ESP_LOGI(TAG, "MODE: Default LVGL UI");

            // Initialize LVGL for GUI
            lvgl_wrapper::LvglHandles lvglHandles = {};
            ESP_ERROR_CHECK(lvgl_wrapper::init(hw, lvglHandles));

            // Create default UI
            if (lvgl_wrapper::lock(0)) {
                ui::createDemoUI();
                lvgl_wrapper::unlock();
            }

            ESP_LOGI(TAG, "LVGL GUI started successfully!");

            // Update clock every second
            uint32_t lastClockUpdate = 0;
            while (true) {
                uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

                if (now - lastClockUpdate >= 1000) {
                    if (lvgl_wrapper::lock(10)) {
                        ui::updateClock();
                        lvgl_wrapper::unlock();
                    }
                    lastClockUpdate = now;
                }

                vTaskDelay(pdMS_TO_TICKS(100));
            }
            break;
        }

        default:
            ESP_LOGE(TAG, "Unknown demo mode!");
            while (true) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
    }
}
