/**
 * @file lights_pages.cpp
 * @brief The two screens behind the lights app: colour wheel and temperature
 *
 * Both work the same way: the whole round screen is the colour space, and
 * each lamp sits on it as a draggable puck at the position matching its
 * current colour. Dragging a puck sets that lamp's colour.
 */

#include "lights_pages.hpp"

#include <cstdio>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "portal_ui.hpp"

constexpr static const char *TAG = "LIGHTS_PAGES";

namespace lights_pages {

namespace {

constexpr int32_t WHEEL_SIZE = 460;
constexpr int32_t WHEEL_RADIUS = WHEEL_SIZE / 2;
constexpr int32_t PUCK_SIZE = 56;

lv_obj_t *gWheelScreen = nullptr;
lv_obj_t *gTempScreen = nullptr;
lv_draw_buf_t *gWheelBuf = nullptr;

void (*gOnBack)() = nullptr;
void (*gOnHome)() = nullptr;

/**
 * @brief Paint the hue wheel into a canvas, once
 * @details Angle around the centre gives the hue, distance from it the
 *          saturation, so the middle washes out to white exactly as on the
 *          original. LVGL has no colour wheel widget and its gradients carry
 *          only two stops, so this is drawn pixel by pixel.
 */
void paintWheel(lv_obj_t *canvas) {
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    for (int32_t y = 0; y < WHEEL_SIZE; y++) {
        const int32_t dy = y - WHEEL_RADIUS;

        for (int32_t x = 0; x < WHEEL_SIZE; x++) {
            const int32_t dx = x - WHEEL_RADIUS;
            const int32_t distanceSq = (dx * dx) + (dy * dy);

            if (distanceSq > (WHEEL_RADIUS * WHEEL_RADIUS)) {
                lv_canvas_set_px(canvas, x, y, lv_color_black(), LV_OPA_COVER);
                continue;
            }

            // Dead centre has no angle at all, and lv_atan2 divides by zero
            // when both offsets are zero. It is white there anyway.
            const int32_t hue = ((dx == 0) && (dy == 0)) ? 0 : lv_atan2(dy, dx);
            int32_t saturation = (lv_sqrt32(static_cast<uint32_t>(distanceSq)) * 100) / WHEEL_RADIUS;
            if (saturation > 100) {
                saturation = 100;
            }

            const lv_color_t colour = lv_color_hsv_to_rgb(
                static_cast<uint16_t>(hue),
                static_cast<uint8_t>(saturation),
                100
            );
            lv_canvas_set_px(canvas, x, y, colour, LV_OPA_COVER);
        }
    }

    lv_canvas_finish_layer(canvas, &layer);
}

/** @brief One lamp, shown as a puck you can drag across the colour space */
lv_obj_t *createPuck(lv_obj_t *parent, const char *label, int32_t x, int32_t y) {
    lv_obj_t *puck = lv_obj_create(parent);
    lv_obj_remove_style_all(puck);
    lv_obj_remove_flag(puck, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(puck, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(puck, PUCK_SIZE, PUCK_SIZE);
    lv_obj_align(puck, LV_ALIGN_CENTER, x, y);
    lv_obj_set_style_radius(puck, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(puck, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(puck, LV_OPA_60, 0);
    lv_obj_set_style_border_color(puck, lv_color_white(), 0);
    lv_obj_set_style_border_opa(puck, LV_OPA_80, 0);
    lv_obj_set_style_border_width(puck, 2, 0);

    // Held pucks get a brighter ring, as the original highlights them
    lv_obj_set_style_border_width(puck, 4, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(puck, LV_OPA_80, LV_STATE_PRESSED);

    lv_obj_t *text = lv_label_create(puck);
    lv_label_set_text(text, label);
    lv_obj_set_style_text_font(text, &portal_ui::portal_font_small_22, 0);
    lv_obj_set_style_text_color(text, lv_color_black(), 0);
    lv_obj_center(text);

    return puck;
}

/** @brief Drag a puck around, keeping it inside the circle */
void puckDragCb(lv_event_t *event) {
    auto *puck = static_cast<lv_obj_t *>(lv_event_get_target(event));
    lv_indev_t *indev = lv_indev_active();
    if (indev == nullptr || puck == nullptr) {
        return;
    }

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    int32_t dx = point.x - portal_ui::SCREEN_RADIUS;
    int32_t dy = point.y - portal_ui::SCREEN_RADIUS;

    // Keep the puck on the wheel; dragging past the rim pins it to the edge
    const int32_t limit = WHEEL_RADIUS - (PUCK_SIZE / 2);
    const int32_t distance = lv_sqrt32(static_cast<uint32_t>((dx * dx) + (dy * dy)));
    if (distance > limit && distance > 0) {
        dx = (dx * limit) / distance;
        dy = (dy * limit) / distance;
    }

    lv_obj_align(puck, LV_ALIGN_CENTER, dx, dy);
}

void attachPuck(lv_obj_t *puck) {
    lv_obj_add_event_cb(puck, puckDragCb, LV_EVENT_PRESSING, nullptr);
}

/** @brief Swipes: right goes back to the lights screen, down goes home */
void pageSwipeCb(lv_event_t *event) {
    const portal_ui::Swipe swipe = portal_ui::swipeFromEvent(event);
    if (swipe == portal_ui::Swipe::None) {
        return;
    }

    lv_obj_t *screen = lv_screen_active();

    if (swipe == portal_ui::Swipe::Down) {
        if (gOnHome != nullptr) {
            gOnHome();
        }
        return;
    }

    if (swipe == portal_ui::Swipe::Left && screen == gWheelScreen) {
        lv_screen_load(gTempScreen);
        return;
    }

    if (swipe == portal_ui::Swipe::Right) {
        if (screen == gTempScreen) {
            lv_screen_load(gWheelScreen);
        } else if (gOnBack != nullptr) {
            gOnBack();
        }
    }
}

}  // namespace

void build(void (*onBack)(), void (*onHome)()) {
    gOnBack = onBack;
    gOnHome = onHome;

    // ------------------------------------------------------------- wheel --
    gWheelScreen = lv_obj_create(nullptr);
    lv_obj_remove_style_all(gWheelScreen);
    lv_obj_set_style_bg_color(gWheelScreen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(gWheelScreen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(gWheelScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(gWheelScreen, LV_OBJ_FLAG_CLICKABLE);

    gWheelBuf = lv_draw_buf_create(WHEEL_SIZE, WHEEL_SIZE, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
    if (gWheelBuf == nullptr) {
        ESP_LOGE(TAG, "No memory for the colour wheel");
        return;
    }

    lv_obj_t *canvas = lv_canvas_create(gWheelScreen);
    lv_canvas_set_draw_buf(canvas, gWheelBuf);
    lv_obj_center(canvas);
    lv_obj_set_style_radius(canvas, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(canvas, true, 0);
    paintWheel(canvas);

    // Three lamps: two on their own, one standing for a group of five
    attachPuck(createPuck(gWheelScreen, "", -110, 60));
    attachPuck(createPuck(gWheelScreen, "5", 20, 110));
    attachPuck(createPuck(gWheelScreen, "", 90, -80));

    lv_obj_add_event_cb(gWheelScreen, pageSwipeCb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(gWheelScreen, pageSwipeCb, LV_EVENT_RELEASED, nullptr);

    // ------------------------------------------------------ temperature --
    gTempScreen = lv_obj_create(nullptr);
    lv_obj_remove_style_all(gTempScreen);
    lv_obj_remove_flag(gTempScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(gTempScreen, LV_OBJ_FLAG_CLICKABLE);

    // Cool cyan at the top running to warm amber at the bottom, the range of
    // white light rather than colour
    lv_obj_set_style_bg_color(gTempScreen, lv_color_hex(0x63E3F5), 0);
    lv_obj_set_style_bg_grad_color(gTempScreen, lv_color_hex(0xF7B733), 0);
    lv_obj_set_style_bg_grad_dir(gTempScreen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(gTempScreen, LV_OPA_COVER, 0);

    attachPuck(createPuck(gTempScreen, "", -90, -90));
    attachPuck(createPuck(gTempScreen, "5", 80, -60));
    attachPuck(createPuck(gTempScreen, "", 10, 100));

    lv_obj_add_event_cb(gTempScreen, pageSwipeCb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(gTempScreen, pageSwipeCb, LV_EVENT_RELEASED, nullptr);

    ESP_LOGI(TAG, "Colour wheel and temperature built");
}

void showWheel() {
    if (gWheelScreen != nullptr) {
        lv_screen_load(gWheelScreen);
    }
}

}  // namespace lights_pages
