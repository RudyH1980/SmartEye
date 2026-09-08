/**
 * @file portal_apps.cpp
 * @brief The remaining Portal apps: speakers, weather, energy, timer, moods
 *
 * Each app is a short list of pages you swipe between sideways, matching how
 * the original stacks its detail screens next to each other rather than
 * nesting them. Swiping down, or tapping the bar at the bottom, returns to
 * the home screen.
 *
 * Everything runs on mock data until the MQTT link lands.
 */

#include "portal_apps.hpp"

#include <cstdio>

#include "board_config.hpp"
#include "board_drivers.hpp"
#include "esp_io_expander.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl_wrapper.hpp"
#include "portal_ui.hpp"

constexpr static const char *TAG = "PORTAL_APPS";

namespace portal_apps {

namespace {

constexpr int32_t CENTRE = portal_ui::SCREEN_RADIUS;
constexpr uint8_t MAX_PAGES = 4;

/** @brief One app: a handful of screens laid out side by side */
struct AppPages {
    lv_obj_t *pages[MAX_PAGES];
    uint8_t count;
    uint8_t current;
};

/** @brief What a page needs to know to move to its neighbours */
struct PageRef {
    AppPages *app;
    uint8_t index;
};

AppPages gSpeakers = {};
AppPages gWeather = {};
AppPages gEnergy = {};
AppPages gTimer = {};
AppPages gMoods = {};

// One ref per page, kept alive for the life of the UI
PageRef gRefs[5 * MAX_PAGES] = {};
uint8_t gRefCount = 0;

/** @brief Set by the owner so a page can send you home */
void (*gGoHome)() = nullptr;

// ---------------------------------------------------------------- helpers --

lv_obj_t *makeLabel(
    lv_obj_t *parent,
    const lv_font_t *font,
    uint32_t colour,
    lv_opa_t opacity,
    const char *text
) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(colour), 0);
    lv_obj_set_style_text_opa(label, opacity, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label, text);
    return label;
}

void pageGestureCb(lv_event_t *event) {
    auto *ref = static_cast<PageRef *>(lv_event_get_user_data(event));
    if (ref == nullptr) {
        return;
    }

    const portal_ui::Swipe swipe = portal_ui::swipeFromEvent(event);
    if (swipe == portal_ui::Swipe::None) {
        return;
    }

    if (swipe == portal_ui::Swipe::Down) {
        if (gGoHome != nullptr) {
            gGoHome();
        }
        return;
    }

    AppPages *app = ref->app;
    if (app->count < 2) {
        return;
    }

    if (swipe == portal_ui::Swipe::Left && (ref->index + 1) < app->count) {
        app->current = ref->index + 1;
        lv_screen_load_anim(app->pages[app->current], LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    } else if (swipe == portal_ui::Swipe::Right && ref->index > 0) {
        app->current = ref->index - 1;
        lv_screen_load_anim(app->pages[app->current], LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    }
}

void homePillCb(lv_event_t *event) {
    LV_UNUSED(event);
    if (gGoHome != nullptr) {
        gGoHome();
    }
}

/**
 * @brief Start a page and register it with its app
 * @return The shared shell, ready to be filled in
 */
portal_ui::Screen startPage(AppPages &app, const portal_ui::Palette &colours) {
    lv_obj_t *screen = lv_obj_create(nullptr);
    portal_ui::Screen ui = portal_ui::createScreen(screen, colours);

    const uint8_t index = app.count;
    app.pages[index] = screen;
    app.count++;

    PageRef *ref = &gRefs[gRefCount++];
    ref->app = &app;
    ref->index = index;

    lv_obj_add_flag(ui.root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui.root, pageGestureCb, LV_EVENT_PRESSED, ref);
    lv_obj_add_event_cb(ui.root, pageGestureCb, LV_EVENT_RELEASED, ref);

    lv_obj_add_flag(ui.homePill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(ui.homePill, 30);
    lv_obj_add_event_cb(ui.homePill, homePillCb, LV_EVENT_CLICKED, nullptr);

    return ui;
}

/** @brief Short arc segments at the rim, the original's page hint */
void addPageHints(portal_ui::Screen &screen, bool hasPrev, bool hasNext) {
    constexpr int32_t INSET = 8;
    constexpr int32_t WIDTH = 5;

    if (hasPrev) {
        lv_obj_t *left = lv_arc_create(screen.root);
        lv_obj_remove_style_all(left);
        lv_obj_set_size(left, portal_ui::SCREEN_SIZE - (INSET * 2), portal_ui::SCREEN_SIZE - (INSET * 2));
        lv_obj_center(left);
        lv_arc_set_bg_angles(left, 160, 200);
        lv_arc_set_value(left, 0);
        lv_obj_remove_flag(left, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_width(left, WIDTH, LV_PART_MAIN);
        lv_obj_set_style_arc_color(left, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_arc_opa(left, LV_OPA_50, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(left, true, LV_PART_MAIN);
    }

    if (hasNext) {
        lv_obj_t *right = lv_arc_create(screen.root);
        lv_obj_remove_style_all(right);
        lv_obj_set_size(right, portal_ui::SCREEN_SIZE - (INSET * 2), portal_ui::SCREEN_SIZE - (INSET * 2));
        lv_obj_center(right);
        lv_arc_set_bg_angles(right, 340, 20);
        lv_arc_set_value(right, 0);
        lv_obj_remove_flag(right, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_width(right, WIDTH, LV_PART_MAIN);
        lv_obj_set_style_arc_color(right, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_arc_opa(right, LV_OPA_50, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(right, true, LV_PART_MAIN);
    }
}

/** @brief The small round icon badge the original puts at the top */
void setBadge(portal_ui::Screen &screen, const char *glyph) {
    lv_obj_set_style_text_font(screen.badgeIcon, &portal_ui::portal_font_icons_40, 0);
    lv_label_set_text(screen.badgeIcon, glyph);
}

// --------------------------------------------------------------- speakers --

int32_t gVolume = 25;
lv_obj_t *gPlayGlyph = nullptr;
bool gPlaying = true;

void playPauseCb(lv_event_t *event) {
    LV_UNUSED(event);
    gPlaying = !gPlaying;
    if (gPlayGlyph != nullptr) {
        lv_label_set_text(gPlayGlyph, gPlaying ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    }
    ESP_LOGI(TAG, "Speakers %s", gPlaying ? "playing" : "paused");
}

void buildSpeakers() {
    portal_ui::Screen page = startPage(gSpeakers, portal_ui::palette::MUSIC);
    setBadge(page, portal_ui::icon::SPEAKER);
    portal_ui::setTitle(page, "Sonos Move");

    // Big play/pause in the middle, the way the original centres its control
    lv_obj_t *button = lv_obj_create(page.root);
    lv_obj_remove_style_all(button);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(button, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(button, 140, 140);
    lv_obj_align(button, LV_ALIGN_CENTER, 0, -6);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x7FC5FF), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_20, 0);

    gPlayGlyph = makeLabel(button, &lv_font_montserrat_48, 0x9BD4FF, LV_OPA_COVER, LV_SYMBOL_PAUSE);
    lv_obj_center(gPlayGlyph);
    lv_obj_add_flag(button, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(button, playPauseCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *track = makeLabel(page.root, &portal_ui::portal_font_title_30, 0xFFFFFF, LV_OPA_COVER, "In the Zone");
    lv_obj_align(track, LV_ALIGN_CENTER, 0, 108);

    lv_obj_t *artist = makeLabel(page.root, &portal_ui::portal_font_small_22, 0xFFFFFF, LV_OPA_70, "Neil Young");
    lv_obj_align(artist, LV_ALIGN_CENTER, 0, 144);

    // Volume page, reached by turning the rim on the original
    portal_ui::Screen volume = startPage(gSpeakers, portal_ui::palette::MUSIC);
    portal_ui::setGlowPalette(volume, lv_color_hex(0x5B1B7A));
    portal_ui::showHeader(volume, false);

    char value[8];
    std::snprintf(value, sizeof(value), "%ld", static_cast<long>(gVolume));
    portal_ui::setHero(volume, value, "%");
    portal_ui::setSecondary(volume, "Volume");
    portal_ui::setArcValue(volume, gVolume);

    addPageHints(page, false, true);
    addPageHints(volume, true, false);
}

// ---------------------------------------------------------------- weather --

/** @brief One column of the hourly strip along the bottom */
void addHourColumn(lv_obj_t *parent, int32_t x, const char *hour, const char *glyph, const char *temp) {
    lv_obj_t *hourLabel = makeLabel(parent, &lv_font_montserrat_14, 0xFFFFFF, LV_OPA_70, hour);
    lv_obj_align(hourLabel, LV_ALIGN_CENTER, x, 112);

    lv_obj_t *icon = makeLabel(parent, &portal_ui::portal_font_icons_40, 0xFFE082, LV_OPA_COVER, glyph);
    lv_obj_align(icon, LV_ALIGN_CENTER, x, 144);

    lv_obj_t *tempLabel = makeLabel(parent, &lv_font_montserrat_14, 0xFFFFFF, LV_OPA_COVER, temp);
    lv_obj_align(tempLabel, LV_ALIGN_CENTER, x, 176);
}

void buildWeather() {
    const portal_ui::Palette SKY = {lv_color_hex(0x6FB4DC), lv_color_hex(0x2F6E9E)};

    portal_ui::Screen page = startPage(gWeather, SKY);
    setBadge(page, portal_ui::icon::WEATHER);
    portal_ui::setTitle(page, "Sunny");

    portal_ui::setHero(page, "24", "\xC2\xB0");
    portal_ui::setSecondary(page, LV_SYMBOL_UP " 26\xC2\xB0    " LV_SYMBOL_DOWN " 20\xC2\xB0");

    // Hourly strip along the bottom, as on the original
    addHourColumn(page.root, -132, "Now", portal_ui::icon::WEATHER, "24\xC2\xB0");
    addHourColumn(page.root, -66, "13", portal_ui::icon::WEATHER, "27\xC2\xB0");
    addHourColumn(page.root, 0, "14", portal_ui::icon::WEATHER, "26\xC2\xB0");
    addHourColumn(page.root, 66, "15", portal_ui::icon::WEATHER, "24\xC2\xB0");
    addHourColumn(page.root, 132, "16", portal_ui::icon::WEATHER, "22\xC2\xB0");

    // Forecast page
    portal_ui::Screen forecast = startPage(gWeather, SKY);
    setBadge(forecast, portal_ui::icon::WEATHER);
    portal_ui::setTitle(forecast, "Forecast");
    portal_ui::setHero(forecast, "", nullptr);

    static const char *DAYS[] = {"Mon", "Tue", "Wed", "Thu", "Fri"};
    static const char *HIGHS[] = {"26\xC2\xB0", "24\xC2\xB0", "21\xC2\xB0", "23\xC2\xB0", "25\xC2\xB0"};
    static const char *LOWS[] = {"14\xC2\xB0", "13\xC2\xB0", "12\xC2\xB0", "12\xC2\xB0", "15\xC2\xB0"};

    for (int i = 0; i < 5; i++) {
        const int32_t y = -68 + (i * 36);

        lv_obj_t *day = makeLabel(forecast.root, &portal_ui::portal_font_small_22, 0xFFFFFF, LV_OPA_90, DAYS[i]);
        lv_obj_align(day, LV_ALIGN_CENTER, -78, y);

        lv_obj_t *icon = makeLabel(forecast.root, &portal_ui::portal_font_icons_40, 0xFFE082, LV_OPA_COVER, portal_ui::icon::WEATHER);
        lv_obj_align(icon, LV_ALIGN_CENTER, -12, y);

        lv_obj_t *high = makeLabel(forecast.root, &portal_ui::portal_font_small_22, 0xFFFFFF, LV_OPA_COVER, HIGHS[i]);
        lv_obj_align(high, LV_ALIGN_CENTER, 48, y);

        lv_obj_t *low = makeLabel(forecast.root, &portal_ui::portal_font_small_22, 0xFFFFFF, LV_OPA_50, LOWS[i]);
        lv_obj_align(low, LV_ALIGN_CENTER, 100, y);
    }

    addPageHints(page, false, true);
    addPageHints(forecast, true, false);
}

// ----------------------------------------------------------------- energy --

/** @brief Line chart shaped like the original's usage graph */
lv_obj_t *addChart(lv_obj_t *parent, uint32_t lineColour, const int32_t *values, int count, int32_t y) {
    lv_obj_t *chart = lv_chart_create(parent);
    lv_obj_remove_style_all(chart);

    // A chart is scrollable and clickable by default, so it swallows every
    // swipe that starts on top of it, and it covers most of the screen. The
    // page underneath has to see those, so hand them straight through.
    lv_obj_remove_flag(chart, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(chart, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(chart, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(chart, 340, 150);
    lv_obj_align(chart, LV_ALIGN_CENTER, 0, y);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, count);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(chart, 0, 0);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(chart, 3, LV_PART_ITEMS);

    lv_chart_series_t *series = lv_chart_add_series(chart, lv_color_hex(lineColour), LV_CHART_AXIS_PRIMARY_Y);
    for (int i = 0; i < count; i++) {
        lv_chart_set_next_value(chart, series, values[i]);
    }

    return chart;
}

void buildEnergy() {
    static const int32_t LIVE[] = {18, 20, 19, 22, 21, 20, 64, 66, 65, 67, 66, 68, 67, 66, 68, 67};
    static const int32_t PRICES[] = {40, 38, 34, 30, 26, 22, 20, 24, 30, 38, 46, 56, 64, 70, 76, 82};

    static const int32_t TOTAL[] = {40, 36, 30, 26, 22, 18, 24, 34, 44, 52, 58, 62, 68, 74, 78, 82};
    static const int32_t SUN[] = {0, 2, 8, 18, 32, 48, 62, 74, 80, 76, 66, 50, 34, 18, 6, 0};

    /** @brief Every energy page has the same head: value, then what it is */
    struct EnergyPage {
        const char *value;
        uint32_t valueColour;
        const char *caption;
        const int32_t *series;
        uint32_t lineColour;
    };

    static const EnergyPage PAGES[] = {
        {"240 W", 0x4FA8FF, "Live Usage", LIVE, 0x2F8FFF},
        {"-10.1 kWh", 0x34C759, "Total Today", TOTAL, 0x34C759},
        {"19.1 kWh", 0xF2C044, "Solar Today", SUN, 0xF2C044},
        {"\xE2\x82\xAC 0.31 /kWh", 0x34C759, "Prices Today", PRICES, 0xF2704A},
    };

    constexpr int PAGE_COUNT = sizeof(PAGES) / sizeof(PAGES[0]);
    portal_ui::Screen screens[PAGE_COUNT];

    for (int i = 0; i < PAGE_COUNT; i++) {
        screens[i] = startPage(gEnergy, portal_ui::palette::ENERGY);
        portal_ui::showHeader(screens[i], false);

        lv_obj_t *value = makeLabel(
            screens[i].root,
            &portal_ui::portal_font_title_30,
            PAGES[i].valueColour,
            LV_OPA_COVER,
            PAGES[i].value
        );
        lv_obj_align(value, LV_ALIGN_TOP_MID, 0, 86);

        lv_obj_t *caption = makeLabel(
            screens[i].root,
            &portal_ui::portal_font_small_22,
            0xFFFFFF,
            LV_OPA_60,
            PAGES[i].caption
        );
        lv_obj_align(caption, LV_ALIGN_TOP_MID, 0, 124);

        addChart(screens[i].root, PAGES[i].lineColour, PAGES[i].series, 16, 60);
        addPageHints(screens[i], i > 0, i < (PAGE_COUNT - 1));
    }
}

// ------------------------------------------------------------------ timer --

int32_t gTimerSeconds = 0;
lv_obj_t *gTimerValue = nullptr;
lv_obj_t *gTimerCaption = nullptr;
lv_obj_t *gTimerCancel = nullptr;
lv_timer_t *gTimerTick = nullptr;
bool gTimerRunning = false;
esp_io_expander_handle_t gExpander = nullptr;

// The alarm keeps sounding until it is tapped away
volatile bool gAlarmActive = false;
TaskHandle_t gAlarmTask = nullptr;

/**
 * @brief Sound the buzzer a few times, off the UI thread
 * @details The pauses between beeps must not run inside an LVGL callback:
 *          that thread also drives the display, so waiting there freezes the
 *          screen for as long as the beeping lasts.
 */
void beepTask(void *param) {
    LV_UNUSED(param);

    // Keeps going until it is tapped away, with a stop after five minutes so
    // an alarm nobody is around for does not sound forever.
    constexpr int MAX_ROUNDS = 300;

    for (int round = 0; gAlarmActive && (round < MAX_ROUNDS); round++) {
        for (int i = 0; i < 3; i++) {
            if (BoardDrivers::i2cLock(50)) {
                esp_io_expander_set_level(gExpander, BoardConfig::TCA9554_BUZZER_MASK, 1);
                BoardDrivers::i2cUnlock();
            }
            vTaskDelay(pdMS_TO_TICKS(120));
            if (BoardDrivers::i2cLock(50)) {
                esp_io_expander_set_level(gExpander, BoardConfig::TCA9554_BUZZER_MASK, 0);
                BoardDrivers::i2cUnlock();
            }
            vTaskDelay(pdMS_TO_TICKS(90));

            if (!gAlarmActive) {
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(700));
    }

    // Never leave the buzzer energised on the way out
    if (BoardDrivers::i2cLock(50)) {
        esp_io_expander_set_level(gExpander, BoardConfig::TCA9554_BUZZER_MASK, 0);
        BoardDrivers::i2cUnlock();
    }
    gAlarmTask = nullptr;
    vTaskDelete(nullptr);
}

void startAlarm() {
    if (gExpander == nullptr || gAlarmTask != nullptr) {
        return;
    }
    gAlarmActive = true;
    xTaskCreate(beepTask, "portal_alarm", 2048, nullptr, 4, &gAlarmTask);
}

void stopAlarm() {
    if (!gAlarmActive) {
        return;
    }
    gAlarmActive = false;
    ESP_LOGI(TAG, "Alarm dismissed");
}

void renderTimer() {
    if (gTimerValue == nullptr) {
        return;
    }
    char text[16];
    std::snprintf(
        text,
        sizeof(text),
        "%02ld:%02ld",
        static_cast<long>(gTimerSeconds / 60),
        static_cast<long>(gTimerSeconds % 60)
    );
    lv_label_set_text(gTimerValue, text);
    lv_obj_align(gTimerValue, LV_ALIGN_CENTER, 0, -30);

    if (gTimerCaption != nullptr) {
        const char *caption = gTimerRunning ? "Tap to pause" : (gTimerSeconds > 0 ? "Tap to start" : "Set Timer");
        lv_label_set_text(gTimerCaption, caption);
        lv_obj_align(gTimerCaption, LV_ALIGN_CENTER, 0, -96);
    }
}

void timerTickCb(lv_timer_t *timer) {
    LV_UNUSED(timer);

    if (!gTimerRunning) {
        return;
    }

    if (gTimerSeconds > 0) {
        gTimerSeconds--;
        renderTimer();
    }

    if (gTimerSeconds == 0) {
        gTimerRunning = false;
        renderTimer();
        ESP_LOGI(TAG, "Timer finished");

        // Bring the timer forward: it is demanding attention, and this is
        // also where the tap that silences it has to land.
        if (gTimer.pages[0] != nullptr && lv_screen_active() != gTimer.pages[0]) {
            lv_screen_load_anim(gTimer.pages[0], LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
        }
        if (gTimerCaption != nullptr) {
            lv_label_set_text(gTimerCaption, "Tap to stop");
            lv_obj_align(gTimerCaption, LV_ALIGN_CENTER, 0, -96);
        }

        startAlarm();
    }
}

void startTimer() {
    if (gTimerSeconds <= 0) {
        return;
    }

    gTimerRunning = true;
    renderTimer();

    if (gTimerTick == nullptr) {
        gTimerTick = lv_timer_create(timerTickCb, 1000, nullptr);
    }
    lv_timer_resume(gTimerTick);
    ESP_LOGI(TAG, "Timer started at %ld s", static_cast<long>(gTimerSeconds));
}

/** @brief Tapping the time toggles between running and paused */
void timerValueCb(lv_event_t *event) {
    LV_UNUSED(event);

    // While it is ringing, any tap just silences it
    if (gAlarmActive) {
        stopAlarm();
        renderTimer();
        return;
    }

    if (gTimerRunning) {
        gTimerRunning = false;
        renderTimer();
        ESP_LOGI(TAG, "Timer paused at %ld s", static_cast<long>(gTimerSeconds));
        return;
    }

    startTimer();
}

/** @brief The X under the time: stop and clear, as on the original */
void timerCancelCb(lv_event_t *event) {
    LV_UNUSED(event);

    stopAlarm();
    gTimerRunning = false;
    gTimerSeconds = 0;
    renderTimer();
    ESP_LOGI(TAG, "Timer cancelled");
}

/** @brief A preset sets the duration and starts it straight away */
void presetCb(lv_event_t *event) {
    const auto minutes = static_cast<int32_t>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
    gTimerSeconds = minutes * 60;
    ESP_LOGI(TAG, "Timer preset %ld min", static_cast<long>(minutes));
    startTimer();
}

void buildTimer() {
    portal_ui::Screen page = startPage(gTimer, portal_ui::palette::IDLE);
    portal_ui::showTicks(page, true);
    portal_ui::showHeader(page, false);

    // A ringing alarm is silenced by tapping anywhere, not just on the digits
    lv_obj_add_event_cb(page.root, timerValueCb, LV_EVENT_CLICKED, nullptr);

    gTimerCaption = makeLabel(page.root, &portal_ui::portal_font_small_22, 0xF2A33C, LV_OPA_COVER, "Set Timer");
    lv_obj_align(gTimerCaption, LV_ALIGN_CENTER, 0, -96);

    gTimerValue = makeLabel(page.root, &portal_ui::portal_font_super_44, 0x5FE3D0, LV_OPA_COVER, "00:00");
    lv_obj_add_flag(gTimerValue, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(gTimerValue, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_ext_click_area(gTimerValue, 24);
    lv_obj_add_event_cb(gTimerValue, timerValueCb, LV_EVENT_CLICKED, nullptr);

    // Cancel sits right under the time, where the original puts it
    gTimerCancel = lv_obj_create(page.root);
    lv_obj_remove_style_all(gTimerCancel);
    lv_obj_add_flag(gTimerCancel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(gTimerCancel, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(gTimerCancel, 48, 48);
    lv_obj_align(gTimerCancel, LV_ALIGN_CENTER, 0, 34);
    lv_obj_set_style_radius(gTimerCancel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(gTimerCancel, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_bg_opa(gTimerCancel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(gTimerCancel, lv_color_hex(0x4A4A4A), LV_STATE_PRESSED);
    lv_obj_add_event_cb(gTimerCancel, timerCancelCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *cross = makeLabel(gTimerCancel, &portal_ui::portal_font_small_22, 0xFFFFFF, LV_OPA_80, LV_SYMBOL_CLOSE);
    lv_obj_center(cross);

    renderTimer();

    // Six round presets, two rows of three, as on the original
    static const int32_t MINUTES[] = {1, 5, 10, 15, 30, 60};
    for (int i = 0; i < 6; i++) {
        const int32_t column = (i % 3) - 1;
        const int32_t row = i / 3;
        const int32_t x = column * 86;
        const int32_t y = 30 + (row * 84);

        lv_obj_t *button = lv_obj_create(page.root);
        lv_obj_remove_style_all(button);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(button, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_set_size(button, 72, 72);
        lv_obj_align(button, LV_ALIGN_CENTER, x, y);
        lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x2A2A2A), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x3D3D3D), LV_STATE_PRESSED);

        char number[8];
        std::snprintf(number, sizeof(number), "%ld", static_cast<long>(MINUTES[i]));

        lv_obj_t *value = makeLabel(button, &portal_ui::portal_font_small_22, 0xF2A33C, LV_OPA_COVER, number);
        lv_obj_align(value, LV_ALIGN_CENTER, 0, -9);

        lv_obj_t *unit = makeLabel(button, &lv_font_montserrat_14, 0xFFFFFF, LV_OPA_60, "Min");
        lv_obj_align(unit, LV_ALIGN_CENTER, 0, 13);

        lv_obj_add_event_cb(
            button,
            presetCb,
            LV_EVENT_CLICKED,
            reinterpret_cast<void *>(static_cast<intptr_t>(MINUTES[i]))
        );
    }
}

// ------------------------------------------------------------------ moods --

struct MoodSpec {
    const char *label;
    uint32_t inner;
    uint32_t outer;
    int32_t x;
    int32_t y;
};

void moodCb(lv_event_t *event) {
    const auto *spec = static_cast<const MoodSpec *>(lv_event_get_user_data(event));
    if (spec != nullptr) {
        ESP_LOGI(TAG, "Mood '%s' activated", spec->label);
    }
}

void buildMoods() {
    static const MoodSpec MOODS[] = {
        {"Colorful",    0x7B2FF7, 0x2E7BF6, -96, -46},
        {"Custom Mood", 0xF2C044, 0x8B6B1F,   6, -74},
        {"Emerald",     0x22C55E, 0x0E7490,  10,  48},
        {"Monochrome",  0xF97316, 0x7C2D12, 108,  10},
        {"Sunset",      0xEC4899, 0x7C3AED, -86,  74},
    };

    portal_ui::Screen page = startPage(gMoods, portal_ui::palette::IDLE);
    setBadge(page, portal_ui::icon::MOOD);
    portal_ui::setTitle(page, "Living Room");
    portal_ui::setHero(page, "", nullptr);

    for (const MoodSpec &spec : MOODS) {
        lv_obj_t *orb = lv_obj_create(page.root);
        lv_obj_remove_style_all(orb);
        lv_obj_add_flag(orb, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(orb, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_set_size(orb, 74, 74);
        lv_obj_align(orb, LV_ALIGN_CENTER, spec.x, spec.y);
        lv_obj_set_style_radius(orb, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(orb, lv_color_hex(spec.inner), 0);
        lv_obj_set_style_bg_grad_color(orb, lv_color_hex(spec.outer), 0);
        lv_obj_set_style_bg_grad_dir(orb, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(orb, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(orb, LV_OPA_70, LV_STATE_PRESSED);

        lv_obj_t *star = makeLabel(orb, &portal_ui::portal_font_icons_40, 0xFFFFFF, LV_OPA_COVER, portal_ui::icon::MOOD);
        lv_obj_center(star);

        lv_obj_t *label = makeLabel(page.root, &lv_font_montserrat_14, 0xFFFFFF, LV_OPA_80, spec.label);
        lv_obj_align_to(label, orb, LV_ALIGN_OUT_BOTTOM_MID, 0, 3);

        lv_obj_add_event_cb(orb, moodCb, LV_EVENT_CLICKED, const_cast<MoodSpec *>(&spec));
    }
}

}  // namespace

void setHomeHandler(void (*goHome)()) {
    gGoHome = goHome;
}

void setExpander(esp_io_expander_handle_t expander) {
    gExpander = expander;
}

void buildAll() {
    // Build one app at a time and hand the lock back in between. Every label
    // queues a text-refresh callback on the display, and that queue is only
    // drained when LVGL gets to run: hold the lock through hundreds of labels
    // and the queue keeps being copied as it grows, until the watchdog bites.
    struct Step {
        const char *name;
        void (*build)();
    };

    static const Step STEPS[] = {
        {"speakers", buildSpeakers},
        {"weather", buildWeather},
        {"energy", buildEnergy},
        {"timer", buildTimer},
        {"moods", buildMoods},
    };

    for (const Step &step : STEPS) {
        if (lvgl_wrapper::lock(500)) {
            step.build();
            lvgl_wrapper::unlock();
        } else {
            ESP_LOGW(TAG, "Could not lock LVGL to build %s", step.name);
        }
        vTaskDelay(pdMS_TO_TICKS(40));
    }

    ESP_LOGI(TAG, "Built speakers, weather, energy, timer and moods");
}

lv_obj_t *entryScreen(home_screen::App app) {
    switch (app) {
        case home_screen::App::Speakers:
            return gSpeakers.pages[0];
        case home_screen::App::Weather:
            return gWeather.pages[0];
        case home_screen::App::Energy:
            return gEnergy.pages[0];
        case home_screen::App::Timer:
            return gTimer.pages[0];
        case home_screen::App::Moods:
            return gMoods.pages[0];
        default:
            return nullptr;
    }
}

}  // namespace portal_apps
