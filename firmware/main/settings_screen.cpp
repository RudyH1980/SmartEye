/**
 * @file settings_screen.cpp
 * @brief Settings: brightness, network, and starting the setup over
 */

#include "settings_screen.hpp"

#include <cstdio>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "portal_ui.hpp"
#include "test.hpp"
#include "wifi_manager.hpp"

constexpr static const char *TAG = "SETTINGS";

namespace settings_screen {

namespace {

portal_ui::Screen gScreen = {};
BoardDrivers::HardwareHandles gHw = {};
void (*gOnHome)() = nullptr;

lv_obj_t *gNetworkLabel = nullptr;
lv_obj_t *gForgetButton = nullptr;
lv_obj_t *gForgetLabel = nullptr;
lv_timer_t *gConfirmTimer = nullptr;
bool gAwaitingConfirm = false;

int32_t gBrightness = 80;

void brightnessCb(lv_event_t *event) {
    auto *slider = static_cast<lv_obj_t *>(lv_event_get_target(event));
    gBrightness = lv_slider_get_value(slider);
    tests::setBacklight(gHw, gBrightness);
}

/** @brief Put the forget button back to its resting state */
void resetForgetButton() {
    gAwaitingConfirm = false;
    if (gForgetLabel != nullptr) {
        lv_label_set_text(gForgetLabel, "Wifi opnieuw instellen");
    }
    if (gForgetButton != nullptr) {
        lv_obj_set_style_bg_color(gForgetButton, lv_color_hex(0x2A2A2A), 0);
    }
}

void confirmTimeoutCb(lv_timer_t *timer) {
    lv_timer_pause(timer);
    resetForgetButton();
}

/**
 * @brief Wiping the network takes two taps
 * @details The first tap only asks. Forgetting the network cannot be undone
 *          from the device itself, so it must not happen on a stray touch.
 */
void forgetCb(lv_event_t *event) {
    LV_UNUSED(event);

    if (!gAwaitingConfirm) {
        gAwaitingConfirm = true;
        lv_label_set_text(gForgetLabel, "Zeker weten? Tik nogmaals");
        lv_obj_set_style_bg_color(gForgetButton, lv_color_hex(0xB3261E), 0);

        if (gConfirmTimer == nullptr) {
            gConfirmTimer = lv_timer_create(confirmTimeoutCb, 4000, nullptr);
        }
        lv_timer_reset(gConfirmTimer);
        lv_timer_resume(gConfirmTimer);
        return;
    }

    ESP_LOGW(TAG, "Forgetting the network and restarting");
    lv_label_set_text(gForgetLabel, "Herstarten...");

    wifi_manager::forgetCredentials();
    vTaskDelay(pdMS_TO_TICKS(400));
    esp_restart();
}

void swipeCb(lv_event_t *event) {
    if (portal_ui::swipeFromEvent(event) == portal_ui::Swipe::Down && gOnHome != nullptr) {
        gOnHome();
    }
}

void homePillCb(lv_event_t *event) {
    LV_UNUSED(event);
    if (gOnHome != nullptr) {
        gOnHome();
    }
}

lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t colour, lv_opa_t opa, const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(colour), 0);
    lv_obj_set_style_text_opa(label, opa, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label, text);
    return label;
}

}  // namespace

void build(const BoardDrivers::HardwareHandles &hw, void (*onHome)()) {
    gHw = hw;
    gOnHome = onHome;

    lv_obj_t *screen = lv_obj_create(nullptr);
    gScreen = portal_ui::createScreen(screen, portal_ui::palette::IDLE);
    portal_ui::showHeader(gScreen, false);

    lv_obj_t *title = makeLabel(gScreen.root, &portal_ui::portal_font_title_30, 0xFFFFFF, LV_OPA_COVER, "Instellingen");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 96);

    // Brightness
    lv_obj_t *brightLabel = makeLabel(gScreen.root, &portal_ui::portal_font_small_22, 0xFFFFFF, LV_OPA_70, "Helderheid");
    lv_obj_align(brightLabel, LV_ALIGN_CENTER, 0, -70);

    lv_obj_t *slider = lv_slider_create(gScreen.root);
    lv_obj_set_size(slider, 260, 14);
    lv_obj_align(slider, LV_ALIGN_CENTER, 0, -34);
    lv_slider_set_range(slider, 10, 100);
    lv_slider_set_value(slider, gBrightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x5FE3D0), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_add_event_cb(slider, brightnessCb, LV_EVENT_VALUE_CHANGED, nullptr);

    // Which network we are on
    gNetworkLabel = makeLabel(gScreen.root, &portal_ui::portal_font_small_22, 0xFFFFFF, LV_OPA_60, "");
    lv_obj_align(gNetworkLabel, LV_ALIGN_CENTER, 0, 26);

    // Start the network setup over
    gForgetButton = lv_obj_create(gScreen.root);
    lv_obj_remove_style_all(gForgetButton);
    lv_obj_add_flag(gForgetButton, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(gForgetButton, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(gForgetButton, 300, 56);
    lv_obj_align(gForgetButton, LV_ALIGN_CENTER, 0, 84);
    lv_obj_set_style_radius(gForgetButton, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(gForgetButton, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_bg_opa(gForgetButton, LV_OPA_COVER, 0);

    gForgetLabel = makeLabel(gForgetButton, &portal_ui::portal_font_small_22, 0xFFFFFF, LV_OPA_COVER, "Wifi opnieuw instellen");
    lv_obj_center(gForgetLabel);
    lv_obj_add_event_cb(gForgetButton, forgetCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_add_flag(gScreen.root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(gScreen.root, swipeCb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(gScreen.root, swipeCb, LV_EVENT_RELEASED, nullptr);

    lv_obj_add_flag(gScreen.homePill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(gScreen.homePill, 30);
    lv_obj_add_event_cb(gScreen.homePill, homePillCb, LV_EVENT_CLICKED, nullptr);

    ESP_LOGI(TAG, "Settings screen built");
}

void show() {
    if (gScreen.root == nullptr) {
        return;
    }

    resetForgetButton();

    // Refresh the network line each time, it changes as Wi-Fi comes and goes
    char text[64];
    if (wifi_manager::currentState() == wifi_manager::State::Connected) {
        std::snprintf(text, sizeof(text), "Verbonden  %s", wifi_manager::ipAddress());
    } else {
        std::snprintf(text, sizeof(text), "Geen netwerk");
    }
    lv_label_set_text(gNetworkLabel, text);
    lv_obj_align(gNetworkLabel, LV_ALIGN_CENTER, 0, 26);

    lv_screen_load(gScreen.root);
}

}  // namespace settings_screen
