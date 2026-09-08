/**
 * @file portal_ui.cpp
 * @brief Round-screen UI shell implementation
 */

#include "portal_ui.hpp"

#include "esp_log.h"

namespace portal_ui {

namespace palette {
const Palette HEATING = {lv_color_hex(0xFF9500), lv_color_hex(0xC24A08)};
const Palette COOLING = {lv_color_hex(0x2FA8FF), lv_color_hex(0x0A72E8)};
const Palette AUTO = {lv_color_hex(0x3BA6F2), lv_color_hex(0x0E63C4)};
// Lights on shows the colour the lamps are actually at: warm yellow to lime
const Palette LIGHTS_ON = {lv_color_hex(0xF7D64A), lv_color_hex(0x9BC63B)};
const Palette MUSIC = {lv_color_hex(0x8B1FBF), lv_color_hex(0x3A0A52)};
const Palette ENERGY = {lv_color_hex(0x0B2036), lv_color_hex(0x000000)};
const Palette IDLE = {lv_color_hex(0x1A1A1A), lv_color_hex(0x000000)};
}  // namespace palette

namespace {

constexpr int32_t BADGE_SIZE = 54;
constexpr int32_t BADGE_TOP = 34;
constexpr int32_t TITLE_TOP = 96;
constexpr int32_t HERO_OFFSET = 8;       // Nudged down to balance badge and title
constexpr int32_t HERO_OFFSET_BARE = -18;  // Value and its caption centred as a pair
constexpr int32_t SUPER_GAP = 4;           // Between the value and its raised suffix
constexpr int32_t SECONDARY_TOP = 96;  // Below the hero value
constexpr int32_t HOME_PILL_BOTTOM = 22;
constexpr int32_t HOME_PILL_WIDTH = 108;
constexpr int32_t HOME_PILL_HEIGHT = 6;
constexpr int32_t ARC_WIDTH = 6;
constexpr int32_t ARC_INSET = 6;

/** @brief Strip an object of every default decoration LVGL gives it */
void makeBare(lv_obj_t *obj) {
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}

lv_obj_t *createLabel(lv_obj_t *parent, const lv_font_t *font, lv_opa_t opacity) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_opa(label, opacity, 0);
    lv_label_set_text(label, "");
    return label;
}

}  // namespace

Swipe swipeFromEvent(lv_event_t *event) {
    // How far the finger has to travel before it counts as a swipe. The time
    // limit is generous on purpose: a calm, deliberate swipe easily takes
    // over a second, and rejecting those made long swipes look broken. It
    // only exists to stop a finger resting on the screen and then wandering
    // off from counting as one.
    constexpr int32_t MIN_TRAVEL = 45;
    constexpr uint32_t MAX_DURATION_MS = 2500;

    static lv_point_t start = {0, 0};
    static uint32_t startTick = 0;

    lv_indev_t *indev = lv_indev_active();
    if (indev == nullptr) {
        return Swipe::None;
    }

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    const lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_PRESSED) {
        start = point;
        startTick = lv_tick_get();
        return Swipe::None;
    }

    if (code != LV_EVENT_RELEASED) {
        return Swipe::None;
    }


    if (lv_tick_elaps(startTick) > MAX_DURATION_MS) {
        return Swipe::None;
    }

    const int32_t dx = point.x - start.x;
    const int32_t dy = point.y - start.y;
    const int32_t absX = (dx < 0) ? -dx : dx;
    const int32_t absY = (dy < 0) ? -dy : dy;

    if (absX >= absY) {
        if (absX < MIN_TRAVEL) {
            return Swipe::None;
        }
        return (dx < 0) ? Swipe::Left : Swipe::Right;
    }

    if (absY < MIN_TRAVEL) {
        return Swipe::None;
    }
    return (dy < 0) ? Swipe::Up : Swipe::Down;
}

Screen createScreen(lv_obj_t *parent, const Palette &colours) {
    Screen screen = {};

    screen.heroOffsetY = HERO_OFFSET;
    screen.root = (parent != nullptr) ? parent : lv_screen_active();
    screen.glowGradient = static_cast<lv_grad_dsc_t *>(lv_calloc(1, sizeof(lv_grad_dsc_t)));
    screen.flatGradient = static_cast<lv_grad_dsc_t *>(lv_calloc(1, sizeof(lv_grad_dsc_t)));
    makeBare(screen.root);
    lv_obj_set_size(screen.root, SCREEN_SIZE, SCREEN_SIZE);
    setPalette(screen, colours);

    // Tick ring, drawn just inside the rim. A scale with no labels gives the
    // fine radial marks the Portal has around its climate and timer screens.
    screen.ticks = lv_scale_create(screen.root);
    makeBare(screen.ticks);
    lv_scale_set_mode(screen.ticks, LV_SCALE_MODE_ROUND_INNER);
    lv_obj_set_size(screen.ticks, SCREEN_SIZE - 8, SCREEN_SIZE - 8);
    lv_obj_center(screen.ticks);
    lv_scale_set_label_show(screen.ticks, false);

    // A closed ring of fine radial graduations all the way round, subtle in
    // the normal view and clearly visible once the screen dims for adjusting.
    lv_scale_set_rotation(screen.ticks, 0);
    lv_scale_set_angle_range(screen.ticks, 360);
    lv_scale_set_total_tick_count(screen.ticks, 72);
    lv_scale_set_major_tick_every(screen.ticks, 6);
    lv_scale_set_range(screen.ticks, 0, 72);
    lv_obj_set_style_length(screen.ticks, 6, LV_PART_ITEMS);
    lv_obj_set_style_length(screen.ticks, 12, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(screen.ticks, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_line_color(screen.ticks, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_line_opa(screen.ticks, LV_OPA_30, LV_PART_ITEMS);
    lv_obj_set_style_line_opa(screen.ticks, LV_OPA_50, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(screen.ticks, 2, LV_PART_ITEMS);
    lv_obj_set_style_line_width(screen.ticks, 2, LV_PART_INDICATOR);
    lv_obj_add_flag(screen.ticks, LV_OBJ_FLAG_HIDDEN);

    // Value arc along the rim
    screen.arc = lv_arc_create(screen.root);
    makeBare(screen.arc);
    lv_obj_set_size(screen.arc, SCREEN_SIZE - ARC_INSET * 2, SCREEN_SIZE - ARC_INSET * 2);
    lv_obj_center(screen.arc);
    lv_arc_set_rotation(screen.arc, 135);
    lv_arc_set_bg_angles(screen.arc, 0, 270);
    lv_arc_set_range(screen.arc, 0, 100);
    lv_arc_set_value(screen.arc, 0);
    lv_obj_remove_flag(screen.arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(screen.arc, ARC_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_width(screen.arc, ARC_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(screen.arc, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(screen.arc, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_arc_color(screen.arc, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(screen.arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(screen.arc, true, LV_PART_INDICATOR);
    lv_obj_add_flag(screen.arc, LV_OBJ_FLAG_HIDDEN);

    // Round icon badge, top centre
    screen.badge = lv_obj_create(screen.root);
    makeBare(screen.badge);
    lv_obj_set_size(screen.badge, BADGE_SIZE, BADGE_SIZE);
    lv_obj_align(screen.badge, LV_ALIGN_TOP_MID, 0, BADGE_TOP);
    lv_obj_set_style_radius(screen.badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(screen.badge, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen.badge, LV_OPA_20, 0);
    lv_obj_set_style_border_color(screen.badge, lv_color_white(), 0);
    lv_obj_set_style_border_opa(screen.badge, LV_OPA_40, 0);
    lv_obj_set_style_border_width(screen.badge, 2, 0);

    screen.badgeIcon = createLabel(screen.badge, &portal_font_small_22, LV_OPA_COVER);
    lv_obj_center(screen.badgeIcon);

    // Title under the badge
    screen.title = createLabel(screen.root, &portal_font_title_30, LV_OPA_COVER);
    lv_obj_align(screen.title, LV_ALIGN_TOP_MID, 0, TITLE_TOP);

    // Hero value, optically centred
    screen.hero = createLabel(screen.root, &portal_font_hero_112, LV_OPA_COVER);
    lv_obj_align(screen.hero, LV_ALIGN_CENTER, 0, screen.heroOffsetY);

    // Superscript rides on the top right of the hero value
    screen.superscript = createLabel(screen.root, &portal_font_super_44, LV_OPA_COVER);
    lv_obj_align_to(screen.superscript, screen.hero, LV_ALIGN_OUT_RIGHT_TOP, 4, 14);

    // Small readings row
    screen.secondary = createLabel(screen.root, &portal_font_small_22, LV_OPA_90);
    lv_obj_align_to(screen.secondary, screen.hero, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);

    // Home affordance
    screen.homePill = lv_obj_create(screen.root);
    makeBare(screen.homePill);
    lv_obj_set_size(screen.homePill, HOME_PILL_WIDTH, HOME_PILL_HEIGHT);
    lv_obj_align(screen.homePill, LV_ALIGN_BOTTOM_MID, 0, -HOME_PILL_BOTTOM);
    lv_obj_set_style_radius(screen.homePill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(screen.homePill, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen.homePill, LV_OPA_80, 0);

    return screen;
}

void setPalette(Screen &screen, const Palette &colours) {
    // Re-applying the same gradient still invalidates the whole 480x480
    // background, and a full redraw on every step of a value is what makes
    // stripes flick across the screen. Only touch it when it truly changes.
    if (screen.palette == &colours) {
        return;
    }
    screen.palette = &colours;

    // Drop any radial descriptor left over from the adjusting view, otherwise
    // it keeps winning over the plain two-colour gradient set below.
    if (screen.flatGradient != nullptr) {
        screen.flatGradient->dir = LV_GRAD_DIR_NONE;
        lv_obj_set_style_bg_grad(screen.root, screen.flatGradient, 0);
    }

    lv_obj_set_style_bg_color(screen.root, colours.top, 0);
    lv_obj_set_style_bg_grad_color(screen.root, colours.bottom, 0);
    lv_obj_set_style_bg_grad_dir(screen.root, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(screen.root, LV_OPA_COVER, 0);
}

void setTitle(Screen &screen, const char *text) {
    lv_label_set_text(screen.title, text);
    lv_obj_align(screen.title, LV_ALIGN_TOP_MID, 0, TITLE_TOP);
}

void setHero(Screen &screen, const char *value, const char *super) {
    lv_label_set_text(screen.hero, value);

    if (super == nullptr) {
        lv_obj_add_flag(screen.superscript, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(screen.hero, LV_ALIGN_CENTER, 0, screen.heroOffsetY);
        return;
    }

    lv_label_set_text(screen.superscript, super);
    lv_obj_remove_flag(screen.superscript, LV_OBJ_FLAG_HIDDEN);

    // Shift the pair left by half the superscript so the whole thing, not just
    // the numerals, sits centred on the screen.
    lv_obj_update_layout(screen.root);
    const int32_t superWidth = lv_obj_get_width(screen.superscript);
    lv_obj_align(screen.hero, LV_ALIGN_CENTER, -(superWidth + SUPER_GAP) / 2, screen.heroOffsetY);
    lv_obj_align_to(screen.superscript, screen.hero, LV_ALIGN_OUT_RIGHT_TOP, SUPER_GAP, 14);
}

void setSecondary(Screen &screen, const char *text) {
    lv_label_set_text(screen.secondary, text);

    // Sit under the hero, but centred on the screen rather than on the hero:
    // the hero is deliberately shifted left to make room for its superscript,
    // and following that shift would leave this line visibly off-centre.
    lv_obj_align_to(screen.secondary, screen.hero, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);
    lv_obj_update_layout(screen.root);
    lv_obj_align(screen.secondary, LV_ALIGN_TOP_MID, 0, lv_obj_get_y(screen.secondary));
}

void setArcValue(Screen &screen, int32_t percent) {
    lv_arc_set_value(screen.arc, percent);
    lv_obj_remove_flag(screen.arc, LV_OBJ_FLAG_HIDDEN);
}

void showHeader(Screen &screen, bool visible) {
    if (visible) {
        lv_obj_remove_flag(screen.badge, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(screen.title, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(screen.badge, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(screen.title, LV_OBJ_FLAG_HIDDEN);
    }

    screen.heroOffsetY = visible ? HERO_OFFSET : HERO_OFFSET_BARE;
    lv_obj_align(screen.hero, LV_ALIGN_CENTER, lv_obj_get_x_aligned(screen.hero), screen.heroOffsetY);
    lv_obj_align_to(screen.secondary, screen.hero, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);
    lv_obj_update_layout(screen.root);
    lv_obj_align(screen.secondary, LV_ALIGN_TOP_MID, 0, lv_obj_get_y(screen.secondary));
}

void setGlowPalette(Screen &screen, lv_color_t glow) {
    lv_grad_dsc_t *gradient = screen.glowGradient;
    if (gradient == nullptr) {
        return;
    }

    // Build it once. LVGL caches drawing state inside the descriptor, so
    // rebuilding it while it is in use invalidates that cache under the
    // renderer's feet, which hangs the display thread.
    if (gradient->stops_count == 0) {
        const lv_color_t stops[2] = {glow, lv_color_black()};
        const uint8_t positions[2] = {0, 255};
        lv_grad_init_stops(gradient, stops, nullptr, positions, 2);

        // The glow sits a little above centre, behind the value, and has
        // faded to black before the rim so the arc stays readable.
        constexpr int32_t CENTRE_X = SCREEN_RADIUS;
        constexpr int32_t CENTRE_Y = SCREEN_RADIUS - 30;
        constexpr int32_t GLOW_RADIUS = 250;
        lv_grad_radial_init(
            gradient,
            CENTRE_X,
            CENTRE_Y,
            CENTRE_X + GLOW_RADIUS,
            CENTRE_Y,
            LV_GRAD_EXTEND_PAD
        );
    }

    screen.palette = nullptr;
    lv_obj_set_style_bg_grad(screen.root, gradient, 0);
    lv_obj_set_style_bg_opa(screen.root, LV_OPA_COVER, 0);
}

lv_obj_t *createPowerButton(Screen &screen, lv_color_t symbolColour) {
    constexpr int32_t BUTTON_SIZE = 148;

    lv_obj_t *button = lv_obj_create(screen.root);
    makeBare(button);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(button, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(button, BUTTON_SIZE, BUTTON_SIZE);
    lv_obj_align(button, LV_ALIGN_CENTER, 0, 24);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);

    lv_obj_add_flag(button, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *glyph = lv_label_create(button);
    lv_label_set_text(glyph, LV_SYMBOL_POWER);
    lv_obj_set_style_text_font(glyph, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(glyph, symbolColour, 0);
    lv_obj_center(glyph);

    return button;
}

void showTicks(Screen &screen, bool visible) {
    if (visible) {
        lv_obj_remove_flag(screen.ticks, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(screen.ticks, LV_OBJ_FLAG_HIDDEN);
    }
}

}  // namespace portal_ui
