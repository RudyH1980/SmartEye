/**
 * @file home_screen.cpp
 * @brief Home screen: the app icons, laid out like the original
 *
 * Seven icons packed into the circle, two over three over two, each a
 * coloured disc with its name underneath. Page dots sit at the bottom: the
 * original gains a page whenever you add shortcuts from the phone app.
 */

#include "home_screen.hpp"

#include "esp_log.h"
#include "lvgl.h"
#include "portal_ui.hpp"

constexpr static const char *TAG = "HOME_SCREEN";

namespace home_screen {

namespace {

constexpr int32_t ICON_SIZE = 74;
constexpr int32_t ROW_SPACING = 104;
constexpr int32_t COL_NARROW = 62;   // Two-icon rows sit closer together
constexpr int32_t COL_WIDE = 128;    // The middle row is the widest part
constexpr int32_t GRID_OFFSET_Y = -14;

struct IconSpec {
    App app;
    const char *label;
    const char *glyph;
    uint32_t colour;
    int32_t x;
    int32_t y;
};

// Colours follow the original: warm orange for climate, purple for speakers,
// blue for weather, yellow for lights, green for energy, grey for the timer
// and magenta for moods.
const IconSpec ICONS[] = {
    {App::Climate,  "Climate",  portal_ui::icon::THERMOSTAT, 0xF97316, -COL_NARROW, -ROW_SPACING},
    {App::Speakers, "Speakers", portal_ui::icon::SPEAKER,    0x8B5CF6,  COL_NARROW, -ROW_SPACING},
    {App::Weather,  "Weather",  portal_ui::icon::WEATHER,    0x3B9EF5, -COL_WIDE,   0},
    {App::Lights,   "Lights",   portal_ui::icon::LIGHTBULB,  0xF2C044,  0,          0},
    {App::Energy,   "Energy",   portal_ui::icon::ENERGY,     0x34C759,  COL_WIDE,   0},
    {App::Timer,    "Timer",    portal_ui::icon::TIMER,      0x6B7280, -COL_NARROW, ROW_SPACING},
    {App::Moods,    "Moods",    portal_ui::icon::MOOD,       0xEC1E9C,  COL_NARROW, ROW_SPACING},
};

// Page two holds what the original keeps in its phone app. Settings live here
// rather than as an eighth icon, so the first page stays the seven the
// original shows.
const IconSpec SETTINGS_ICON = {
    App::Settings, "Instellingen", portal_ui::icon::SETTINGS, 0x4B5563, 0, -20
};

portal_ui::Screen gScreen = {};
portal_ui::Screen gPageTwo = {};
LaunchCallback gCallback;
bool gBuilt = false;
uint8_t gPage = 0;

void iconClickedCb(lv_event_t *event) {
    const auto *spec = static_cast<const IconSpec *>(lv_event_get_user_data(event));
    if (spec == nullptr) {
        return;
    }

    ESP_LOGI(TAG, "Opening %s", spec->label);
    if (gCallback) {
        gCallback(spec->app);
    }
}

void createIcon(lv_obj_t *parent, const IconSpec &spec) {
    lv_obj_t *disc = lv_obj_create(parent);
    lv_obj_remove_style_all(disc);
    lv_obj_remove_flag(disc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(disc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(disc, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(disc, ICON_SIZE, ICON_SIZE);
    lv_obj_align(disc, LV_ALIGN_CENTER, spec.x, spec.y + GRID_OFFSET_Y);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc, lv_color_hex(spec.colour), 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);

    // A pressed icon dims, so a tap is felt even without haptics
    lv_obj_set_style_bg_opa(disc, LV_OPA_70, LV_STATE_PRESSED);

    lv_obj_t *glyph = lv_label_create(disc);
    lv_label_set_text(glyph, spec.glyph);
    lv_obj_set_style_text_font(glyph, &portal_ui::portal_font_icons_40, 0);
    lv_obj_set_style_text_color(glyph, lv_color_white(), 0);
    lv_obj_center(glyph);

    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, spec.label);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_opa(label, LV_OPA_80, 0);
    lv_obj_align_to(label, disc, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

    lv_obj_add_event_cb(disc, iconClickedCb, LV_EVENT_CLICKED, const_cast<IconSpec *>(&spec));
}

/** @brief Page dots, as the original has under its icon circle */
void createPageDots(lv_obj_t *parent, int count, int active) {
    constexpr int32_t DOT_SIZE = 6;
    constexpr int32_t DOT_GAP = 12;

    const int32_t totalWidth = (count * DOT_SIZE) + ((count - 1) * (DOT_GAP - DOT_SIZE));
    int32_t x = -totalWidth / 2;

    for (int i = 0; i < count; i++) {
        lv_obj_t *dot = lv_obj_create(parent);
        lv_obj_remove_style_all(dot);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(dot, DOT_SIZE, DOT_SIZE);
        lv_obj_align(dot, LV_ALIGN_CENTER, x + (DOT_SIZE / 2), 196);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(dot, (i == active) ? LV_OPA_COVER : LV_OPA_30, 0);
        x += DOT_GAP;
    }
}

void pageSwipeCb(lv_event_t *event) {
    const portal_ui::Swipe swipe = portal_ui::swipeFromEvent(event);

    if (swipe == portal_ui::Swipe::Left && gPage == 0) {
        gPage = 1;
        lv_screen_load(gPageTwo.root);
    } else if (swipe == portal_ui::Swipe::Right && gPage == 1) {
        gPage = 0;
        lv_screen_load(gScreen.root);
    }
}

}  // namespace

void build(LaunchCallback callback) {
    gCallback = std::move(callback);

    lv_obj_t *screen = lv_obj_create(nullptr);
    gScreen = portal_ui::createScreen(screen, portal_ui::palette::IDLE);

    // The icons are the content here, so the shared badge and title stay away.
    // The bottom bar goes too: this screen already has page dots, and there is
    // nowhere further back to go from here.
    portal_ui::showHeader(gScreen, false);
    lv_obj_add_flag(gScreen.homePill, LV_OBJ_FLAG_HIDDEN);

    for (const IconSpec &spec : ICONS) {
        createIcon(gScreen.root, spec);
    }

    // One page, so one dot. A second dot would promise a page to swipe to
    // that does not exist yet.
    createPageDots(gScreen.root, 2, 0);
    lv_obj_add_flag(gScreen.root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(gScreen.root, pageSwipeCb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(gScreen.root, pageSwipeCb, LV_EVENT_RELEASED, nullptr);

    // Page two: settings
    lv_obj_t *second = lv_obj_create(nullptr);
    gPageTwo = portal_ui::createScreen(second, portal_ui::palette::IDLE);
    portal_ui::showHeader(gPageTwo, false);
    lv_obj_add_flag(gPageTwo.homePill, LV_OBJ_FLAG_HIDDEN);
    createIcon(gPageTwo.root, SETTINGS_ICON);
    createPageDots(gPageTwo.root, 2, 1);
    lv_obj_add_flag(gPageTwo.root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(gPageTwo.root, pageSwipeCb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(gPageTwo.root, pageSwipeCb, LV_EVENT_RELEASED, nullptr);

    gBuilt = true;
    ESP_LOGI(TAG, "Home screen built with %d icons", static_cast<int>(sizeof(ICONS) / sizeof(ICONS[0])));
}

void show() {
    if (!gBuilt) {
        return;
    }
    gPage = 0;
    lv_screen_load_anim(gScreen.root, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
}

}  // namespace home_screen
