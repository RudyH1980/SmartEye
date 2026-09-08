/**
 * @file ui.cpp
 * @brief Simple UI implementation
 */

#include "ui.hpp"

#include <cinttypes>
#include <cstdio>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

constexpr static const char *TAG = "UI";

namespace ui {

// UI elements (static to persist across function calls)
static lv_obj_t *sLabelTitle = nullptr;
static lv_obj_t *sLabelCounter = nullptr;
static lv_obj_t *sLabelClock = nullptr;
static lv_obj_t *sBtnIncrement = nullptr;
static lv_obj_t *sBtnDecrement = nullptr;
static lv_obj_t *sBtnReset = nullptr;
static lv_obj_t *sBtnColorToggle = nullptr;

static int sCounter = 0;
static bool sDarkMode = true;

// Forward declarations
static void btnIncrementCb(lv_event_t *event);
static void btnDecrementCb(lv_event_t *event);
static void btnResetCb(lv_event_t *event);
static void btnColorToggleCb(lv_event_t *event);

void createDemoUI() {
    ESP_LOGI(TAG, "Creating demo UI...");

    // Get the default screen
    lv_obj_t *screen = lv_screen_active();

    // Set background color
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);

    // ========================================
    // Status Bar (Top)
    // ========================================
    lv_obj_t *statusBar = lv_obj_create(screen);
    lv_obj_set_size(statusBar, LV_PCT(100), 40);
    lv_obj_align(statusBar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(statusBar, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_border_width(statusBar, 0, 0);
    lv_obj_set_style_radius(statusBar, 0, 0);

    // Clock label
    sLabelClock = lv_label_create(statusBar);
    lv_label_set_text(sLabelClock, "00:00:00");
    lv_obj_set_style_text_color(sLabelClock, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(sLabelClock, LV_ALIGN_CENTER, 0, 0);

    // ========================================
    // Title Label
    // ========================================
    sLabelTitle = lv_label_create(screen);
    lv_label_set_text(sLabelTitle, "ESP32-S3 Touch LCD Demo");
    lv_obj_set_style_text_color(sLabelTitle, lv_color_hex(0x00FFFF), 0);
    lv_obj_set_style_text_font(sLabelTitle, &lv_font_montserrat_24, 0);
    lv_obj_align(sLabelTitle, LV_ALIGN_TOP_MID, 0, 60);

    // ========================================
    // Counter Display
    // ========================================
    lv_obj_t *counterPanel = lv_obj_create(screen);
    lv_obj_set_size(counterPanel, 300, 120);
    lv_obj_align(counterPanel, LV_ALIGN_CENTER, 0, -40);
    lv_obj_set_style_bg_color(counterPanel, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_border_color(counterPanel, lv_color_hex(0x00FFFF), 0);
    lv_obj_set_style_border_width(counterPanel, 2, 0);
    lv_obj_set_style_radius(counterPanel, 10, 0);

    lv_obj_t *labelCounterText = lv_label_create(counterPanel);
    lv_label_set_text(labelCounterText, "Counter:");
    lv_obj_set_style_text_color(labelCounterText, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(labelCounterText, LV_ALIGN_TOP_MID, 0, 10);

    sLabelCounter = lv_label_create(counterPanel);
    lv_label_set_text(sLabelCounter, "0");
    lv_obj_set_style_text_color(sLabelCounter, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(sLabelCounter, &lv_font_montserrat_48, 0);
    lv_obj_align(sLabelCounter, LV_ALIGN_CENTER, 0, 10);

    // ========================================
    // Control Buttons (Row 1)
    // ========================================
    lv_obj_t *btnRow1 = lv_obj_create(screen);
    lv_obj_set_size(btnRow1, 400, 60);
    lv_obj_align(btnRow1, LV_ALIGN_CENTER, 0, 80);
    lv_obj_set_style_bg_opa(btnRow1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow1, 0, 0);
    lv_obj_set_style_pad_all(btnRow1, 0, 0);
    lv_obj_set_flex_flow(btnRow1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        btnRow1,
        LV_FLEX_ALIGN_SPACE_EVENLY,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );

    // Decrement button
    sBtnDecrement = lv_button_create(btnRow1);
    lv_obj_set_size(sBtnDecrement, 120, 60);
    lv_obj_add_event_cb(sBtnDecrement, btnDecrementCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_style_bg_color(sBtnDecrement, lv_color_hex(0xFF6666), 0);

    lv_obj_t *labelDec = lv_label_create(sBtnDecrement);
    lv_label_set_text(labelDec, "-");
    lv_obj_set_style_text_font(labelDec, &lv_font_montserrat_32, 0);
    lv_obj_center(labelDec);

    // Reset button
    sBtnReset = lv_button_create(btnRow1);
    lv_obj_set_size(sBtnReset, 120, 60);
    lv_obj_add_event_cb(sBtnReset, btnResetCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_style_bg_color(sBtnReset, lv_color_hex(0xFFAA00), 0);

    lv_obj_t *labelReset = lv_label_create(sBtnReset);
    lv_label_set_text(labelReset, "Reset");
    lv_obj_center(labelReset);

    // Increment button
    sBtnIncrement = lv_button_create(btnRow1);
    lv_obj_set_size(sBtnIncrement, 120, 60);
    lv_obj_add_event_cb(sBtnIncrement, btnIncrementCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_style_bg_color(sBtnIncrement, lv_color_hex(0x66FF66), 0);

    lv_obj_t *labelInc = lv_label_create(sBtnIncrement);
    lv_label_set_text(labelInc, "+");
    lv_obj_set_style_text_font(labelInc, &lv_font_montserrat_32, 0);
    lv_obj_center(labelInc);

    // ========================================
    // Color Toggle Button (Row 2)
    // ========================================
    sBtnColorToggle = lv_button_create(screen);
    lv_obj_set_size(sBtnColorToggle, 320, 60);
    lv_obj_align(sBtnColorToggle, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(sBtnColorToggle, btnColorToggleCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_style_bg_color(sBtnColorToggle, lv_color_hex(0x0088FF), 0);

    lv_obj_t *labelToggle = lv_label_create(sBtnColorToggle);
    lv_label_set_text(labelToggle, "Toggle Light Mode");
    lv_obj_center(labelToggle);

    // Debug: Log button positions after layout is complete
    lv_obj_update_layout(screen);
    ESP_LOGI(TAG, "Button positions:");
    ESP_LOGI(
        TAG,
        "  Decrement: x=%d, y=%d, w=%d, h=%d",
        lv_obj_get_x(sBtnDecrement),
        lv_obj_get_y(sBtnDecrement),
        lv_obj_get_width(sBtnDecrement),
        lv_obj_get_height(sBtnDecrement)
    );
    ESP_LOGI(
        TAG,
        "  Reset: x=%d, y=%d, w=%d, h=%d",
        lv_obj_get_x(sBtnReset),
        lv_obj_get_y(sBtnReset),
        lv_obj_get_width(sBtnReset),
        lv_obj_get_height(sBtnReset)
    );
    ESP_LOGI(
        TAG,
        "  Increment: x=%d, y=%d, w=%d, h=%d",
        lv_obj_get_x(sBtnIncrement),
        lv_obj_get_y(sBtnIncrement),
        lv_obj_get_width(sBtnIncrement),
        lv_obj_get_height(sBtnIncrement)
    );
    ESP_LOGI(
        TAG,
        "  Toggle: x=%d, y=%d, w=%d, h=%d",
        lv_obj_get_x(sBtnColorToggle),
        lv_obj_get_y(sBtnColorToggle),
        lv_obj_get_width(sBtnColorToggle),
        lv_obj_get_height(sBtnColorToggle)
    );

    ESP_LOGI(TAG, "Demo UI created successfully!");
}

void updateClock() {
    if (sLabelClock == nullptr) {
        return;
    }

    // Get system uptime in seconds
    uint32_t uptimeSec = xTaskGetTickCount() / configTICK_RATE_HZ;
    uint32_t hours = (uptimeSec / 3600) % 24;
    uint32_t minutes = (uptimeSec / 60) % 60;
    uint32_t seconds = uptimeSec % 60;

    char timeStr[16];

    snprintf(timeStr, sizeof(timeStr), "%02" PRIu32 ":%02" PRIu32 ":%02" PRIu32, hours, minutes, seconds);
    lv_label_set_text(sLabelClock, timeStr);
}

// ============================================================================
// Button callbacks
// ============================================================================

static void btnIncrementCb(lv_event_t *event) {
    sCounter++;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", sCounter);
    lv_label_set_text(sLabelCounter, buf);
    ESP_LOGI(TAG, "Counter incremented to %d", sCounter);
}

static void btnDecrementCb(lv_event_t *event) {
    sCounter--;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", sCounter);
    lv_label_set_text(sLabelCounter, buf);
    ESP_LOGI(TAG, "Counter decremented to %d", sCounter);
}

static void btnResetCb(lv_event_t *event) {
    sCounter = 0;
    lv_label_set_text(sLabelCounter, "0");
    ESP_LOGI(TAG, "Counter reset to 0");
}

static void btnColorToggleCb(lv_event_t *event) {
    sDarkMode = !sDarkMode;

    lv_obj_t *screen = lv_screen_active();

    if (sDarkMode) {
        // Dark mode
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_color(sLabelTitle, lv_color_hex(0x00FFFF), 0);
        ESP_LOGI(TAG, "Switched to dark mode");
    } else {
        // Light mode
        lv_obj_set_style_bg_color(screen, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_color(sLabelTitle, lv_color_hex(0x0000FF), 0);
        ESP_LOGI(TAG, "Switched to light mode");
    }
}

}  // namespace ui
