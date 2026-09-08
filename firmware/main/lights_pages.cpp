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
#include <cstring>

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

/**
 * @brief One lamp on the colour space
 * @details Lamps dropped on top of each other become a group. The group is
 *          shown by whichever lamp leads it: that puck carries the count of
 *          how many lamps are in it, the rest sit hidden behind it.
 */
struct Lamp {
    lv_obj_t *obj;
    lv_obj_t *label;
    int group;  // -1 when on its own, otherwise the index of its leader
    char entityId[64];
    int32_t haMembers;  // How many lamps Home Assistant says this holds
};

constexpr size_t MAX_LAMPS = 5;
constexpr int32_t MERGE_DISTANCE = PUCK_SIZE - 8;
constexpr uint32_t HOLD_TO_REMOVE_MS = 3000;
constexpr int32_t HOLD_DRAG_PIXELS = 25;

Lamp gLamps[MAX_LAMPS];
size_t gLampCount = 0;

// The same lamps again on the colour temperature page
Lamp gTempLamps[MAX_LAMPS];
size_t gTempCount = 0;

// The group being looked at, and the pucks standing in for its members
int gOpenGroup = -1;
lv_obj_t *gDetailLayer = nullptr;
lv_obj_t *gDetailPucks[MAX_LAMPS] = {};
int gDetailLamp[MAX_LAMPS] = {};
size_t gDetailCount = 0;

// Long-press bookkeeping for pulling a lamp out of an opened group
uint32_t gHoldStart = 0;
lv_point_t gHoldFrom = {0, 0};
bool gHoldArmed = false;

int leaderOf(size_t index) {
    return (gLamps[index].group < 0) ? static_cast<int>(index) : gLamps[index].group;
}

/** @brief How many lamps a group holds, the leader included */
int32_t membersOf(int leader) {
    int32_t count = 1;
    for (size_t i = 0; i < gLampCount; i++) {
        if ((static_cast<int>(i) != leader) && (gLamps[i].group == leader)) {
            count++;
        }
    }
    return count;
}

/** @brief Show the count on each leader and hide the lamps behind it */
void refreshLamps() {
    for (size_t i = 0; i < gLampCount; i++) {
        Lamp &lamp = gLamps[i];
        const bool isLeader = (lamp.group < 0) || (lamp.group == static_cast<int>(i));

        if (!isLeader) {
            lv_obj_add_flag(lamp.obj, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        lv_obj_remove_flag(lamp.obj, LV_OBJ_FLAG_HIDDEN);

        const int32_t count = membersOf(static_cast<int>(i));
        char text[8];
        std::snprintf(text, sizeof(text), "%ld", static_cast<long>(count));
        lv_label_set_text(lamp.label, (count > 1) ? text : "");
        lv_obj_set_style_border_width(lamp.obj, (count > 1) ? 4 : 2, 0);
    }
}

Lamp *findLamp(const lv_obj_t *obj, size_t *indexOut) {
    for (size_t i = 0; i < gLampCount; i++) {
        if (gLamps[i].obj == obj) {
            if (indexOut != nullptr) {
                *indexOut = i;
            }
            return &gLamps[i];
        }
    }
    return nullptr;
}

int32_t distanceBetween(const lv_obj_t *a, const lv_obj_t *b) {
    const int32_t dx = lv_obj_get_x_aligned(a) - lv_obj_get_x_aligned(b);
    const int32_t dy = lv_obj_get_y_aligned(a) - lv_obj_get_y_aligned(b);
    return lv_sqrt32(static_cast<uint32_t>((dx * dx) + (dy * dy)));
}

/** @brief Keep a position inside the wheel */
void clampToWheel(int32_t &dx, int32_t &dy) {
    const int32_t limit = WHEEL_RADIUS - (PUCK_SIZE / 2);
    const int32_t distance = lv_sqrt32(static_cast<uint32_t>((dx * dx) + (dy * dy)));
    if ((distance > limit) && (distance > 0)) {
        dx = (dx * limit) / distance;
        dy = (dy * limit) / distance;
    }
}

void closeDetail();

// --------------------------------------------------------- the main wheel --

/**
 * @brief Drag a lamp, or a whole group, across the colour space
 * @details A group moves as one piece: its members sit behind the leader, so
 *          moving the leader recolours all of them together. Dragging never
 *          breaks a group up; that is what opening it is for.
 */
void lampDragCb(lv_event_t *event) {
    auto *puck = static_cast<lv_obj_t *>(lv_event_get_target(event));
    lv_indev_t *indev = lv_indev_active();
    if ((indev == nullptr) || (puck == nullptr)) {
        return;
    }

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    int32_t dx = point.x - portal_ui::SCREEN_RADIUS;
    int32_t dy = point.y - portal_ui::SCREEN_RADIUS;
    clampToWheel(dx, dy);

    lv_obj_align(puck, LV_ALIGN_CENTER, dx, dy);

    // Members ride along with their leader
    size_t index = 0;
    if (findLamp(puck, &index) != nullptr) {
        for (size_t i = 0; i < gLampCount; i++) {
            if ((i != index) && (gLamps[i].group == static_cast<int>(index))) {
                lv_obj_align(gLamps[i].obj, LV_ALIGN_CENTER, dx, dy);
            }
        }
    }
}

/** @brief Dropping one puck on another merges the two into a group */
void lampReleaseCb(lv_event_t *event) {
    auto *puck = static_cast<lv_obj_t *>(lv_event_get_target(event));

    size_t index = 0;
    if (findLamp(puck, &index) == nullptr) {
        return;
    }

    for (size_t other = 0; other < gLampCount; other++) {
        if ((other == index) || lv_obj_has_flag(gLamps[other].obj, LV_OBJ_FLAG_HIDDEN)) {
            continue;
        }
        if (distanceBetween(puck, gLamps[other].obj) > MERGE_DISTANCE) {
            continue;
        }

        // Everything this puck leads joins the puck it was dropped on
        const int newLeader = leaderOf(other);
        const int32_t x = lv_obj_get_x_aligned(gLamps[newLeader].obj);
        const int32_t y = lv_obj_get_y_aligned(gLamps[newLeader].obj);

        for (size_t i = 0; i < gLampCount; i++) {
            if ((i == index) || (gLamps[i].group == static_cast<int>(index))) {
                gLamps[i].group = newLeader;
                lv_obj_align(gLamps[i].obj, LV_ALIGN_CENTER, x, y);
            }
        }
        gLamps[newLeader].group = newLeader;

        refreshLamps();
        ESP_LOGI(TAG, "Group now holds %ld lamps", static_cast<long>(membersOf(newLeader)));
        return;
    }
}

// ------------------------------------------------------- the opened group --

/**
 * @brief Pull a lamp out of the opened group
 * @details Only after holding it still for three seconds, so a tap or a
 *          careless brush never breaks a group up. The view closes as soon as
 *          a lamp is taken out, which is what makes the change visible.
 */
void detailHoldCb(lv_event_t *event) {
    auto *puck = static_cast<lv_obj_t *>(lv_event_get_target(event));
    const lv_event_code_t code = lv_event_get_code(event);

    lv_indev_t *indev = lv_indev_active();
    if (indev == nullptr) {
        return;
    }

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    if (code == LV_EVENT_PRESSED) {
        gHoldStart = lv_tick_get();
        gHoldFrom = point;
        gHoldArmed = false;
        return;
    }

    if (code == LV_EVENT_RELEASED) {
        gHoldArmed = false;
        lv_obj_set_style_border_color(puck, lv_color_white(), 0);
        return;
    }

    if (!gHoldArmed) {
        if (lv_tick_elaps(gHoldStart) >= HOLD_TO_REMOVE_MS) {
            gHoldArmed = true;
            // Say that the hold registered, rather than leaving the user
            // wondering whether three seconds have passed
            lv_obj_set_style_border_color(puck, lv_color_hex(0x5FE3D0), 0);
        }
        return;
    }

    const int32_t moved = lv_sqrt32(static_cast<uint32_t>(
        ((point.x - gHoldFrom.x) * (point.x - gHoldFrom.x)) +
        ((point.y - gHoldFrom.y) * (point.y - gHoldFrom.y))
    ));
    if (moved < HOLD_DRAG_PIXELS) {
        return;
    }

    for (size_t i = 0; i < gDetailCount; i++) {
        if (gDetailPucks[i] != puck) {
            continue;
        }

        const int lampIndex = gDetailLamp[i];
        gLamps[lampIndex].group = -1;

        // Drop it where the finger is, so it lands on its own colour
        int32_t dx = point.x - portal_ui::SCREEN_RADIUS;
        int32_t dy = point.y - portal_ui::SCREEN_RADIUS;
        clampToWheel(dx, dy);
        lv_obj_align(gLamps[lampIndex].obj, LV_ALIGN_CENTER, dx, dy);

        ESP_LOGI(TAG, "Lamp taken out of the group");
        closeDetail();
        refreshLamps();
        return;
    }
}

void closeDetail() {
    if (gDetailLayer != nullptr) {
        lv_obj_delete(gDetailLayer);
        gDetailLayer = nullptr;
    }
    gDetailCount = 0;
    gOpenGroup = -1;
    gHoldArmed = false;
}

void detailBackdropCb(lv_event_t *event) {
    LV_UNUSED(event);
    closeDetail();
}

/**
 * @brief Open a group and fan its lamps out
 * @details The wheel dims behind them so the members stand out, the way the
 *          original shows an expanded group.
 */
void openDetail(int leader) {
    closeDetail();
    gOpenGroup = leader;

    gDetailLayer = lv_obj_create(gWheelScreen);
    lv_obj_remove_style_all(gDetailLayer);
    lv_obj_remove_flag(gDetailLayer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(gDetailLayer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(gDetailLayer, portal_ui::SCREEN_SIZE, portal_ui::SCREEN_SIZE);
    lv_obj_center(gDetailLayer);
    lv_obj_set_style_bg_color(gDetailLayer, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(gDetailLayer, LV_OPA_80, 0);
    lv_obj_add_event_cb(gDetailLayer, detailBackdropCb, LV_EVENT_CLICKED, nullptr);

    int members[MAX_LAMPS];
    size_t count = 0;
    for (size_t i = 0; i < gLampCount; i++) {
        if (leaderOf(i) == leader) {
            members[count] = static_cast<int>(i);
            count++;
        }
    }

    constexpr int32_t RING = 110;
    for (size_t i = 0; i < count; i++) {
        const int32_t angle = static_cast<int32_t>((360 * i) / count);
        const int32_t x = (RING * lv_trigo_cos(static_cast<int16_t>(angle))) / 32767;
        const int32_t y = (RING * lv_trigo_sin(static_cast<int16_t>(angle))) / 32767;

        lv_obj_t *puck = lv_obj_create(gDetailLayer);
        lv_obj_remove_style_all(puck);
        lv_obj_remove_flag(puck, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(puck, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(puck, PUCK_SIZE, PUCK_SIZE);
        lv_obj_align(puck, LV_ALIGN_CENTER, x, y);
        lv_obj_set_style_radius(puck, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(puck, lv_color_hex(0xC9E265), 0);
        lv_obj_set_style_bg_opa(puck, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(puck, lv_color_white(), 0);
        lv_obj_set_style_border_width(puck, 2, 0);

        lv_obj_add_event_cb(puck, detailHoldCb, LV_EVENT_PRESSED, nullptr);
        lv_obj_add_event_cb(puck, detailHoldCb, LV_EVENT_PRESSING, nullptr);
        lv_obj_add_event_cb(puck, detailHoldCb, LV_EVENT_RELEASED, nullptr);

        gDetailPucks[i] = puck;
        gDetailLamp[i] = members[i];
    }
    gDetailCount = count;

    lv_obj_t *hint = lv_label_create(gDetailLayer);
    lv_label_set_text(hint, "Houd 3 sec vast en sleep\nom er een uit te halen");
    lv_obj_set_style_text_font(hint, &portal_ui::portal_font_small_22, 0);
    lv_obj_set_style_text_color(hint, lv_color_white(), 0);
    lv_obj_set_style_text_opa(hint, LV_OPA_70, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -54);

    ESP_LOGI(TAG, "Group opened, %u lamps", static_cast<unsigned>(count));
}

/** @brief Tapping a group opens it */
void lampTapCb(lv_event_t *event) {
    auto *puck = static_cast<lv_obj_t *>(lv_event_get_target(event));

    size_t index = 0;
    if (findLamp(puck, &index) == nullptr) {
        return;
    }

    const int leader = leaderOf(index);
    if (membersOf(leader) > 1) {
        openDetail(leader);
    }
}

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
    lv_obj_set_style_bg_opa(puck, LV_OPA_80, LV_STATE_PRESSED);

    lv_obj_t *text = lv_label_create(puck);
    lv_label_set_text(text, label);
    lv_obj_set_style_text_font(text, &portal_ui::portal_font_small_22, 0);
    lv_obj_set_style_text_color(text, lv_color_black(), 0);
    lv_obj_center(text);

    if (gLampCount < MAX_LAMPS) {
        gLamps[gLampCount] = {puck, text, -1, "", 1};
        gLampCount++;
    }

    return puck;
}


/** @brief The same lamp on the colour temperature page */
lv_obj_t *createTempPuck(lv_obj_t *parent, const char *label, int32_t x, int32_t y) {
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
    lv_obj_set_style_border_width(puck, (label[0] != 0) ? 4 : 2, 0);

    lv_obj_t *text = lv_label_create(puck);
    lv_label_set_text(text, label);
    lv_obj_set_style_text_font(text, &portal_ui::portal_font_small_22, 0);
    lv_obj_set_style_text_color(text, lv_color_black(), 0);
    lv_obj_center(text);

    lv_obj_add_event_cb(puck, lampDragCb, LV_EVENT_PRESSING, nullptr);
    return puck;
}

void attachPuck(lv_obj_t *puck) {
    lv_obj_add_event_cb(puck, lampDragCb, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(puck, lampReleaseCb, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(puck, lampTapCb, LV_EVENT_CLICKED, nullptr);
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

    // No lamps yet: they arrive from Home Assistant, per room

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

    // Its lamps are created alongside the wheel's, from the same list

    lv_obj_add_event_cb(gTempScreen, pageSwipeCb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(gTempScreen, pageSwipeCb, LV_EVENT_RELEASED, nullptr);

    ESP_LOGI(TAG, "Colour wheel and temperature built");
}


void setLamps(const ha_client::Device *devices, size_t count) {
    if (devices == nullptr) {
        return;
    }

    // Start over: which lamps exist, and which of them are groups, is decided
    // in Home Assistant, so the screen mirrors it rather than keeping its own
    // idea from a previous fetch.
    for (size_t i = 0; i < gLampCount; i++) {
        if (gLamps[i].obj != nullptr) {
            lv_obj_delete(gLamps[i].obj);
        }
        if (gTempLamps[i].obj != nullptr) {
            lv_obj_delete(gTempLamps[i].obj);
        }
    }
    gLampCount = 0;
    gTempCount = 0;
    closeDetail();

    size_t placed = 0;
    for (size_t i = 0; (i < count) && (placed < MAX_LAMPS); i++) {
        if (devices[i].domain != ha_client::Domain::Light) {
            continue;
        }

        // Spread them around the wheel until their real colour is read
        const int32_t angle = static_cast<int32_t>((360 * placed) / MAX_LAMPS);
        const int32_t x = (120 * lv_trigo_cos(static_cast<int16_t>(angle))) / 32767;
        const int32_t y = (120 * lv_trigo_sin(static_cast<int16_t>(angle))) / 32767;

        char label[16] = "";
        if (devices[i].memberCount > 1) {
            std::snprintf(label, sizeof(label), "%ld", static_cast<long>(devices[i].memberCount));
        }

        attachPuck(createPuck(gWheelScreen, label, x, y));
        std::strncpy(gLamps[placed].entityId, devices[i].entityId, sizeof(gLamps[placed].entityId) - 1);
        gLamps[placed].haMembers = devices[i].memberCount;

        // The temperature page shows exactly the same lamps
        lv_obj_t *twin = createTempPuck(gTempScreen, label, x, y);
        gTempLamps[gTempCount] = {twin, nullptr, -1, "", devices[i].memberCount};
        gTempCount++;

        placed++;
    }

    refreshLamps();
    ESP_LOGI(TAG, "Loaded %u lamps from Home Assistant", static_cast<unsigned>(placed));
}

void showWheel() {
    if (gWheelScreen != nullptr) {
        lv_screen_load(gWheelScreen);
    }
}

}  // namespace lights_pages
