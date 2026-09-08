/**
 * @file setup_screen.cpp
 * @brief First-run screen: scan the code, join, fill in the network
 */

#include "setup_screen.hpp"

#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "lvgl.h"
#include "portal_ui.hpp"
#include "wifi_manager.hpp"

constexpr static const char *TAG = "SETUP_SCREEN";

namespace setup_screen {

namespace {

constexpr int32_t QR_SIZE = 190;

portal_ui::Screen gScreen = {};
lv_obj_t *gQr = nullptr;
lv_obj_t *gStep = nullptr;
bool gBuilt = false;

/**
 * @brief Payload that makes a phone camera offer to join the network
 * @details The WIFI: scheme is what both iOS and Android read from a plain
 *          QR code. T:nopass says the network is open, so no password field
 *          appears; S: carries the name.
 */
void buildJoinPayload(char *out, size_t size) {
    std::snprintf(out, size, "WIFI:T:nopass;S:%s;;", wifi_manager::SETUP_AP_NAME);
}

void setStep(const char *text) {
    if (gStep != nullptr) {
        lv_label_set_text(gStep, text);
        lv_obj_align(gStep, LV_ALIGN_BOTTOM_MID, 0, -52);
    }
}

void hideQr() {
    if (gQr != nullptr) {
        lv_obj_add_flag(gQr, LV_OBJ_FLAG_HIDDEN);
    }
}

}  // namespace

void show() {
    if (gBuilt) {
        lv_screen_load(gScreen.root);
        return;
    }

    lv_obj_t *screen = lv_obj_create(nullptr);
    gScreen = portal_ui::createScreen(screen, portal_ui::palette::IDLE);

    // Only the title is wanted here; the round icon badge would just be an
    // empty circle above it.
    lv_obj_add_flag(gScreen.badge, LV_OBJ_FLAG_HIDDEN);
    portal_ui::setTitle(gScreen, "SmartEye");

    // The code sits where the big value normally goes, so the eye lands on it
    gQr = lv_qrcode_create(gScreen.root);
    lv_qrcode_set_size(gQr, QR_SIZE);
    lv_qrcode_set_dark_color(gQr, lv_color_black());
    lv_qrcode_set_light_color(gQr, lv_color_white());
    // Sits below centre so the title above it has room to breathe: at the
    // exact centre the code crowds the line of text right against it.
    lv_obj_align(gQr, LV_ALIGN_CENTER, 0, 18);
    lv_obj_set_style_border_width(gQr, 8, 0);
    lv_obj_set_style_border_color(gQr, lv_color_white(), 0);

    char payload[96];
    buildJoinPayload(payload, sizeof(payload));
    lv_qrcode_update(gQr, payload, std::strlen(payload));

    gStep = lv_label_create(gScreen.root);
    lv_obj_set_style_text_font(gStep, &portal_ui::portal_font_small_22, 0);
    lv_obj_set_style_text_color(gStep, lv_color_white(), 0);
    lv_obj_set_style_text_align(gStep, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(gStep, "");
    lv_obj_align(gStep, LV_ALIGN_BOTTOM_MID, 0, -52);

    gBuilt = true;
    lv_screen_load(gScreen.root);
}

void showJoinCode() {
    show();

    if (gQr != nullptr) {
        lv_obj_remove_flag(gQr, LV_OBJ_FLAG_HIDDEN);
    }

    portal_ui::setPalette(gScreen, portal_ui::palette::IDLE);
    portal_ui::setTitle(gScreen, "Scan om te verbinden");
    setStep("daarna: 192.168.4.1");
    ESP_LOGI(TAG, "Showing join code for '%s'", wifi_manager::SETUP_AP_NAME);
}

void showConnecting(const char *networkName) {
    show();
    hideQr();

    portal_ui::setPalette(gScreen, portal_ui::palette::IDLE);
    portal_ui::setTitle(gScreen, "Verbinden");
    setStep(networkName);
}

void showConnected(const char *ipAddress) {
    show();
    hideQr();

    portal_ui::setPalette(gScreen, portal_ui::palette::COOLING);
    portal_ui::setTitle(gScreen, "Verbonden");
    setStep(ipAddress);
}

}  // namespace setup_screen
