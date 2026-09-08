/**
 * @file portal_demo.cpp
 * @brief Portal-style app screens running on mock data
 *
 * Two apps for now, swipe left or right through the middle of the screen to
 * move between them. Dragging along the rim adjusts the value of the app you
 * are on, standing in for the rotary ring of the original.
 */

#include "portal_demo.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "board_drivers.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "ha_client.hpp"
#include "settings_screen.hpp"
#include "home_screen.hpp"
#include "lights_pages.hpp"
#include "portal_apps.hpp"
#include "test.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "imu_services.hpp"
#include "lvgl_wrapper.hpp"
#include "portal_ui.hpp"
#include "setup_screen.hpp"
#include "wifi_manager.hpp"

#if __has_include("secrets.hpp")
#include "secrets.hpp"
#else
constexpr const char *HA_HOST = "";
constexpr const char *HA_TOKEN = "";
#endif

constexpr static const char *TAG = "PORTAL_DEMO";

namespace portal_demo {

namespace {

// Only a narrow band at the very edge acts as the ring. It used to start at
// 150, which made almost the whole screen count as ring: swipes that began
// anywhere but dead centre were read as turns and silently ignored.
constexpr int32_t RING_INNER_RADIUS = 200;

// How long the brightness read-out stays up after you stop turning
constexpr uint32_t ADJUST_LINGER_MS = 2500;

// ---------------------------------------------------------------- climate --
constexpr int32_t SETPOINT_MIN_TENTHS = 50;
constexpr int32_t SETPOINT_MAX_TENTHS = 300;
constexpr int32_t MEASURED_TENTHS = 195;
constexpr int32_t HUMIDITY_PCT = 58;

// ----------------------------------------------------------------- lights --
constexpr int32_t BRIGHTNESS_MIN = 1;
constexpr int32_t BRIGHTNESS_MAX = 100;

struct App {
    lv_obj_t *screen;
    portal_ui::Screen ui;
};

App gClimate = {};
App gLights = {};

// The thermostat's mode screen, a swipe to the left of the temperature
lv_obj_t *gModeScreen = nullptr;

int32_t gSetpointTenths = 215;
int32_t gBrightness = 50;
bool gLightsOn = true;
bool gShowingBrightness = false;
lv_timer_t *gAdjustTimer = nullptr;
lv_obj_t *gPowerButton = nullptr;

// True while a drag that started on the rim is in progress, so a rim turn
// never doubles as a swipe between apps.
bool gRimDrag = false;

// True while the setup screen owns the display, so a later connection knows
// whether it still has to hand control back.
bool gSetupShowing = false;

// Which Home Assistant entities the two screens drive, empty while running on
// placeholder data
char gClimateEntity[64] = {};
char gLightEntity[64] = {};

// Kept for the backlight, which Home Assistant can set
BoardDrivers::HardwareHandles gHw = {};

/**
 * @brief Thermostat modes, which are what actually colour the screen
 * @details The background follows the mode, not how the setpoint compares to
 *          the room: Auto is blue, Heat orange, Cool blue-cyan.
 */
enum class ClimateMode { Auto, Heat, Cool, Off };

ClimateMode gClimateMode = ClimateMode::Auto;

// While the ring is being turned the screen drops to near-black with just the
// number and the word Temperature, so the value carries the whole screen.
const portal_ui::Palette CLIMATE_ADJUST = {lv_color_hex(0x121212), lv_color_hex(0x000000)};
bool gClimateAdjusting = false;
lv_timer_t *gClimateAdjustTimer = nullptr;

const portal_ui::Palette &paletteForMode() {
    switch (gClimateMode) {
        case ClimateMode::Heat:
            return portal_ui::palette::HEATING;
        case ClimateMode::Cool:
            return portal_ui::palette::COOLING;
        case ClimateMode::Off:
            return portal_ui::palette::IDLE;
        case ClimateMode::Auto:
        default:
            return portal_ui::palette::AUTO;
    }
}

/** @brief Angle of a touch around the screen centre, or -1 if off the rim */
int32_t rimAngleAt(lv_point_t point, bool &onRim) {
    const int32_t dx = point.x - portal_ui::SCREEN_RADIUS;
    const int32_t dy = point.y - portal_ui::SCREEN_RADIUS;
    onRim = ((dx * dx) + (dy * dy)) >= (RING_INNER_RADIUS * RING_INNER_RADIUS);
    return lv_atan2(dy, dx);
}

/** @brief Shortest way round between two angles, so 359 to 1 counts as +2 */
int32_t angleDelta(int32_t from, int32_t to) {
    int32_t delta = to - from;
    if (delta > 180) {
        delta -= 360;
    } else if (delta < -180) {
        delta += 360;
    }
    return delta;
}


// ---------------------------------------------------------------------------
// Home Assistant
// ---------------------------------------------------------------------------

void renderClimateIdle();
void showLightsIdle();

// The display is configured from Home Assistant itself: these are ordinary
// helper entities, so they can be changed from any dashboard, on a phone or
// in a browser, without touching the firmware.
constexpr const char *HELPER_ROOM = "input_select.smarteye_ruimte";
constexpr const char *HELPER_BRIGHTNESS = "input_number.smarteye_helderheid";

// How often to look for changes made in Home Assistant
constexpr uint32_t HA_POLL_MS = 20000;

char gCurrentRoom[40] = {};

/** @brief Take the devices Home Assistant reported and put them on screen */
void applyDevices(const ha_client::Device *devices, size_t count) {
    if (devices == nullptr) {
        ESP_LOGW(TAG, "Home Assistant unreachable, keeping what is on screen");
        return;
    }

    // The lights screens all read from the same list, so the wheel, the
    // temperature page and the zone view can never disagree about which
    // lamps exist or how they are grouped.
    lights_pages::setLamps(devices, count);

    for (size_t i = 0; i < count; i++) {
        const ha_client::Device &device = devices[i];

        if ((device.domain == ha_client::Domain::Climate) && (gClimateEntity[0] == 0)) {
            std::strncpy(gClimateEntity, device.entityId, sizeof(gClimateEntity) - 1);
            if (device.temperature > 0) {
                gSetpointTenths = device.temperature;
            }
            if (lvgl_wrapper::lock(100)) {
                portal_ui::setTitle(gClimate.ui, device.name);
                renderClimateIdle();
                lvgl_wrapper::unlock();
            }
            ESP_LOGI(TAG, "Thermostat: %s", device.name);
        }

        if ((device.domain == ha_client::Domain::Light) && (gLightEntity[0] == 0)) {
            std::strncpy(gLightEntity, device.entityId, sizeof(gLightEntity) - 1);
            gLightsOn = device.on;
            if (device.brightness >= 0) {
                gBrightness = device.brightness;
            }
            if (lvgl_wrapper::lock(100)) {
                portal_ui::setTitle(gLights.ui, device.name);
                showLightsIdle();
                lvgl_wrapper::unlock();
            }
            ESP_LOGI(TAG, "Light: %s (%s)", device.name, device.area);
        }
    }
}

/**
 * @brief Follow the settings made in Home Assistant, then refresh
 * @details Runs on its own task: reading helpers and the device list means
 *          several requests, and none of that may hold up the screen.
 */
void homeAssistantTask(void *param) {
    (void)param;

    while (true) {
        char room[40] = {};
        const esp_err_t roomResult = ha_client::readHelper(HELPER_ROOM, room, sizeof(room));

        if (roomResult == ESP_ERR_NOT_FOUND) {
            // No helper yet: show everything rather than nothing
            if (gCurrentRoom[0] != 0) {
                gCurrentRoom[0] = 0;
                ha_client::setRoom("");
            }
        } else if ((roomResult == ESP_OK) && (std::strcmp(room, gCurrentRoom) != 0)) {
            std::strncpy(gCurrentRoom, room, sizeof(gCurrentRoom) - 1);
            ha_client::setRoom(room);
            ESP_LOGI(TAG, "Room set from Home Assistant: %s", room);
        }

        char brightness[16] = {};
        if (ha_client::readHelper(HELPER_BRIGHTNESS, brightness, sizeof(brightness)) == ESP_OK) {
            const int percent = std::atoi(brightness);
            if ((percent >= 5) && (percent <= 100)) {
                tests::setBacklight(gHw, percent);
            }
        }

        ha_client::refresh(applyDevices);
        vTaskDelay(pdMS_TO_TICKS(HA_POLL_MS));
    }
}

void startHomeAssistant() {
    xTaskCreate(homeAssistantTask, "ha_poll", 6144, nullptr, 3, nullptr);
    ESP_LOGI(TAG, "Following Home Assistant for room and brightness");
}

// ---------------------------------------------------------------------------
// Climate
// ---------------------------------------------------------------------------

/** @brief The resting view: mode colour, target, and the room reading below */
void renderClimateIdle() {
    gClimateAdjusting = false;

    char whole[16];
    char decimal[16];
    std::snprintf(whole, sizeof(whole), "%ld", static_cast<long>(gSetpointTenths / 10));
    std::snprintf(decimal, sizeof(decimal), "%ld", static_cast<long>(gSetpointTenths % 10));

    char secondary[48];
    std::snprintf(
        secondary,
        sizeof(secondary),
        "%ld.%ld\xC2\xB0",
        static_cast<long>(MEASURED_TENTHS / 10),
        static_cast<long>(MEASURED_TENTHS % 10)
    );

    portal_ui::showHeader(gClimate.ui, true);
    portal_ui::setTitle(gClimate.ui, "Thermostat");
    portal_ui::setHero(gClimate.ui, whole, decimal);
    portal_ui::setSecondary(gClimate.ui, secondary);
    portal_ui::setPalette(gClimate.ui, paletteForMode());
}

void climateAdjustDoneCb(lv_timer_t *timer) {
    lv_timer_pause(timer);
    renderClimateIdle();
}

/** @brief The turning view: near-black, the number, and what it is */
void renderClimateAdjusting() {
    gClimateAdjusting = true;

    char whole[16];
    char decimal[16];
    std::snprintf(whole, sizeof(whole), "%ld", static_cast<long>(gSetpointTenths / 10));
    std::snprintf(decimal, sizeof(decimal), "%ld", static_cast<long>(gSetpointTenths % 10));

    portal_ui::showHeader(gClimate.ui, false);
    portal_ui::setPalette(gClimate.ui, CLIMATE_ADJUST);
    portal_ui::setHero(gClimate.ui, whole, decimal);
    portal_ui::setSecondary(gClimate.ui, "Temperature");

    if (gClimateAdjustTimer == nullptr) {
        gClimateAdjustTimer = lv_timer_create(climateAdjustDoneCb, 1600, nullptr);
    }
    lv_timer_reset(gClimateAdjustTimer);
    lv_timer_resume(gClimateAdjustTimer);
}

void renderClimate() {
    renderClimateAdjusting();
}

// ---------------------------------------------------------------------------
// Lights
// ---------------------------------------------------------------------------

/** @brief Back to the zone view: name, power button, state colour */
void showLightsIdle() {
    gShowingBrightness = false;

    portal_ui::showHeader(gLights.ui, true);
    portal_ui::setHero(gLights.ui, "", nullptr);
    portal_ui::setSecondary(gLights.ui, "");
    lv_obj_set_style_text_color(gLights.ui.secondary, lv_color_white(), 0);
    portal_ui::setPalette(
        gLights.ui,
        gLightsOn ? portal_ui::palette::LIGHTS_ON : portal_ui::palette::IDLE
    );

    if (gPowerButton != nullptr) {
        lv_obj_remove_flag(gPowerButton, LV_OBJ_FLAG_HIDDEN);
    }
}

/** @brief The turning view: dark glow, big percentage, what you are changing */
void showLightsBrightness() {
    gShowingBrightness = true;

    char value[16];
    std::snprintf(value, sizeof(value), "%ld", static_cast<long>(gBrightness));

    if (gPowerButton != nullptr) {
        lv_obj_add_flag(gPowerButton, LV_OBJ_FLAG_HIDDEN);
    }

    portal_ui::showHeader(gLights.ui, false);

    // Dark olive-green glow with mint accents, as the original shows while
    // the ring is held: the number carries the screen, not the colour.
    portal_ui::setGlowPalette(gLights.ui, lv_color_hex(0x2F3A16));
    portal_ui::setHero(gLights.ui, value, "%");
    portal_ui::setSecondary(gLights.ui, "Brightness");
    lv_obj_set_style_text_color(gLights.ui.secondary, lv_color_hex(0x7FE3C4), 0);
    portal_ui::setArcValue(gLights.ui, gBrightness);
    lv_obj_set_style_arc_color(gLights.ui.arc, lv_color_hex(0x7FE3C4), LV_PART_INDICATOR);
}

void adjustTimerCb(lv_timer_t *timer) {
    lv_timer_pause(timer);
    showLightsIdle();
}

void restartAdjustTimer() {
    if (gAdjustTimer == nullptr) {
        gAdjustTimer = lv_timer_create(adjustTimerCb, ADJUST_LINGER_MS, nullptr);
    }
    lv_timer_reset(gAdjustTimer);
    lv_timer_resume(gAdjustTimer);
}

void powerButtonCb(lv_event_t *event) {
    LV_UNUSED(event);
    gLightsOn = !gLightsOn;
    showLightsIdle();
    ESP_LOGI(TAG, "Lights %s", gLightsOn ? "on" : "off");

    if (gLightEntity[0] != 0) {
        ha_client::setPower(gLightEntity, gLightsOn);
    }
}

// ---------------------------------------------------------------------------
// Rim turning
// ---------------------------------------------------------------------------

void rimDragCb(lv_event_t *event) {
    static int32_t lastAngle = 0;

    lv_indev_t *indev = lv_indev_active();
    if (indev == nullptr) {
        return;
    }

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    bool onRim = false;
    const int32_t angle = rimAngleAt(point, onRim);
    const lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_PRESSED) {
        gRimDrag = onRim;
        lastAngle = angle;
        return;
    }

    if (code == LV_EVENT_RELEASED) {
        gRimDrag = false;
        return;
    }

    if (!gRimDrag) {
        return;
    }

    // A step per 3 degrees of arc keeps the value from racing away
    const int32_t step = angleDelta(lastAngle, angle) / 3;
    if (step == 0) {
        return;
    }
    lastAngle = angle;

    auto *app = static_cast<App *>(lv_event_get_user_data(event));

    if (app == &gClimate) {
        gSetpointTenths += step;
        gSetpointTenths = LV_CLAMP(SETPOINT_MIN_TENTHS, gSetpointTenths, SETPOINT_MAX_TENTHS);
        renderClimate();
        return;
    }

    gBrightness += step;
    gBrightness = LV_CLAMP(BRIGHTNESS_MIN, gBrightness, BRIGHTNESS_MAX);
    gLightsOn = true;
    showLightsBrightness();
    restartAdjustTimer();
}

// ---------------------------------------------------------------------------
// Turning the whole board
// ---------------------------------------------------------------------------

/**
 * @brief Apply a turn to whichever app is on screen
 * @param steps Signed steps; positive is clockwise, which raises the value
 */
void applyTurn(int32_t steps) {
    if (steps == 0) {
        return;
    }

    if (lv_screen_active() == gClimate.screen) {
        gSetpointTenths = LV_CLAMP(
            SETPOINT_MIN_TENTHS,
            gSetpointTenths + steps,
            SETPOINT_MAX_TENTHS
        );
        renderClimate();
        return;
    }

    gBrightness = LV_CLAMP(BRIGHTNESS_MIN, gBrightness + steps, BRIGHTNESS_MAX);
    gLightsOn = true;
    showLightsBrightness();
    restartAdjustTimer();
}

/**
 * @brief Track the board's rotation and turn it into value changes
 * @details The angle works as a throttle rather than a dial: hold the board
 *          turned and the value keeps creeping in that direction, turn it
 *          back upright and it stops where it is. That way a small wobble
 *          does nothing, and you steer by how far you hold it over.
 *
 *          Lay the board flat and there is no meaningful angle to read, so
 *          the task only engages once enough gravity falls in the screen
 *          plane. Whatever angle it engages at counts as upright, so picking
 *          the board up never jumps the value.
 */
/**
 * @brief Read the IMU from the LVGL thread, not a task of its own
 * @details Touch, IMU, RTC and the IO expander share one I2C bus. Reading it
 *          from two tasks corrupts transactions: coordinates come back as
 *          register addresses and touch dies. Running this on the same timer
 *          thread as the touch read removes the overlap entirely, which no
 *          amount of locking managed to do reliably.
 */
void imuTimerCb(lv_timer_t *timer) {
    auto *imu = static_cast<qmi8658_dev_t *>(lv_timer_get_user_data(timer));

    constexpr uint32_t POLL_MS = 150;

    // Below this the board lies too flat for the angle to mean anything
    constexpr float MIN_STRENGTH = 0.30F;
    // Hysteresis, so hovering around the threshold does not chatter
    constexpr float DROP_STRENGTH = 0.22F;

    // Nothing happens until the board is turned this far: holding it a little
    // off-square, or a hand wobbling, must not move the value.
    constexpr float DEADZONE_DEG = 30.0F;
    // A quarter turn is as fast as it goes
    constexpr float FULL_TURN_DEG = 90.0F;

    // Steps per second, from just past the deadzone up to a quarter turn.
    // On the thermostat a step is a tenth of a degree, so this runs from a
    // fifth of a degree per second up to about one degree per second.
    constexpr float SLOW_RATE = 2.0F;
    constexpr float FAST_RATE = 10.0F;

    static bool engaged = false;
    static float reference = 0.0F;
    static float pending = 0.0F;

    const ImuServices::ScreenRotation rotation = ImuServices::getScreenRotation(imu);

    if (!engaged) {
        if (rotation.strength >= MIN_STRENGTH) {
            engaged = true;
            reference = rotation.angleDeg;
            pending = 0.0F;
        }
        return;
    }

    if (rotation.strength < DROP_STRENGTH) {
        engaged = false;
        return;
    }

    // How far the board is held from where it started, shortest way round
    float offset = rotation.angleDeg - reference;
    if (offset > 180.0F) {
        offset -= 360.0F;
    } else if (offset < -180.0F) {
        offset += 360.0F;
    }

    // Turning right has to raise the value, so flip the sign of the
    // direction gravity sweeps when the board turns clockwise.
    offset = -offset;

    const float magnitude = (offset < 0.0F) ? -offset : offset;
    if (magnitude < DEADZONE_DEG) {
        pending = 0.0F;
        return;
    }

    // Ramp from slow to fast between the deadzone and a quarter turn
    float travel = (magnitude - DEADZONE_DEG) / (FULL_TURN_DEG - DEADZONE_DEG);
    if (travel > 1.0F) {
        travel = 1.0F;
    }
    const float rate = SLOW_RATE + ((FAST_RATE - SLOW_RATE) * travel);

    pending += (rate * static_cast<float>(POLL_MS)) / 1000.0F;

    auto steps = static_cast<int32_t>(pending);
    if (steps > 0) {
        pending -= static_cast<float>(steps);
        if (offset < 0.0F) {
            steps = -steps;
        }

        applyTurn(steps);
    }

}

// ---------------------------------------------------------------------------
// Moving between apps
// ---------------------------------------------------------------------------

/**
 * @brief Swiping down inside an app returns to the home screen
 * @details The original's back gesture is not visible in the keynote
 *          recording, so this is our own choice rather than a copy. The white
 *          bar at the bottom does the same, which at least matches where the
 *          original puts something you would expect to be a way back.
 */
void gestureCb(lv_event_t *event) {
    const portal_ui::Swipe swipe = portal_ui::swipeFromEvent(event);

    // A turn on the rim is not a swipe
    if (gRimDrag || swipe == portal_ui::Swipe::None) {
        return;
    }

    if (swipe == portal_ui::Swipe::Down) {
        home_screen::show();
        return;
    }

    if (swipe != portal_ui::Swipe::Left) {
        return;
    }

    // Each app has its own detail pages a swipe to the left
    auto *app = static_cast<App *>(lv_event_get_user_data(event));

    if (app == &gClimate && gModeScreen != nullptr) {
        lv_screen_load(gModeScreen);
    } else if (app == &gLights) {
        lights_pages::showWheel();
    }
}

void homePillCb(lv_event_t *event) {
    LV_UNUSED(event);
    home_screen::show();
}

void attachInput(App &app) {
    lv_obj_add_flag(app.ui.root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(app.ui.root, rimDragCb, LV_EVENT_PRESSED, &app);
    lv_obj_add_event_cb(app.ui.root, rimDragCb, LV_EVENT_PRESSING, &app);
    lv_obj_add_event_cb(app.ui.root, rimDragCb, LV_EVENT_RELEASED, &app);
    lv_obj_add_event_cb(app.ui.root, gestureCb, LV_EVENT_PRESSED, &app);
    lv_obj_add_event_cb(app.ui.root, gestureCb, LV_EVENT_RELEASED, &app);

    lv_obj_add_flag(app.ui.homePill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(app.ui.homePill, homePillCb, LV_EVENT_CLICKED, nullptr);

    // A small bar is a small target, so widen what counts as a hit
    lv_obj_set_ext_click_area(app.ui.homePill, 30);
}

// ---------------------------------------------------------------------------
// Climate: the mode screen behind a swipe
// ---------------------------------------------------------------------------

lv_obj_t *gModeButtons[4] = {};

/** @brief Light up whichever mode is active, dim the rest */
void refreshModeButtons() {
    const int active = static_cast<int>(gClimateMode);

    for (int i = 0; i < 4; i++) {
        if (gModeButtons[i] == nullptr) {
            continue;
        }
        lv_obj_set_style_bg_opa(gModeButtons[i], (i == active) ? LV_OPA_COVER : LV_OPA_30, 0);
        lv_obj_set_style_border_width(gModeButtons[i], (i == active) ? 3 : 0, 0);
    }
}

void modeButtonCb(lv_event_t *event) {
    const auto mode = static_cast<ClimateMode>(
        reinterpret_cast<intptr_t>(lv_event_get_user_data(event))
    );

    gClimateMode = mode;
    refreshModeButtons();
    renderClimateIdle();

    // The mode colours the thermostat, so show the result straight away
    lv_screen_load(gClimate.screen);
    ESP_LOGI(TAG, "Climate mode %d", static_cast<int>(mode));
}

void modeSwipeCb(lv_event_t *event) {
    const portal_ui::Swipe swipe = portal_ui::swipeFromEvent(event);

    if (swipe == portal_ui::Swipe::Down) {
        home_screen::show();
    } else if (swipe == portal_ui::Swipe::Right) {
        lv_screen_load(gClimate.screen);
    }
}

void buildClimateModes() {
    struct ModeSpec {
        ClimateMode mode;
        const char *label;
        uint32_t colour;
        int32_t x;
        int32_t y;
    };

    // Auto, Heat and Cool sit around the centre; Off gets its own pill below,
    // the way the original separates turning it off from choosing a mode.
    static const ModeSpec MODES[] = {
        {ClimateMode::Auto, "Auto", 0x3BA6F2, -96, -30},
        {ClimateMode::Heat, "Heat", 0xF97316, 0, -110},
        {ClimateMode::Cool, "Cool", 0x2FA8FF, 96, -30},
    };

    gModeScreen = lv_obj_create(nullptr);
    portal_ui::Screen ui = portal_ui::createScreen(gModeScreen, portal_ui::palette::AUTO);
    lv_obj_set_style_text_font(ui.badgeIcon, &portal_ui::portal_font_icons_40, 0);
    lv_label_set_text(ui.badgeIcon, portal_ui::icon::THERMOSTAT);
    portal_ui::setTitle(ui, "Thermostat");

    for (const ModeSpec &spec : MODES) {
        lv_obj_t *button = lv_obj_create(ui.root);
        lv_obj_remove_style_all(button);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(button, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_set_size(button, 92, 92);
        lv_obj_align(button, LV_ALIGN_CENTER, spec.x, spec.y + 40);
        lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(spec.colour), 0);
        lv_obj_set_style_border_color(button, lv_color_white(), 0);

        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, spec.label);
        lv_obj_set_style_text_font(label, &portal_ui::portal_font_small_22, 0);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_center(label);

        lv_obj_add_event_cb(
            button,
            modeButtonCb,
            LV_EVENT_CLICKED,
            reinterpret_cast<void *>(static_cast<intptr_t>(spec.mode))
        );

        gModeButtons[static_cast<int>(spec.mode)] = button;
    }

    // Off, as a pill rather than a circle
    lv_obj_t *off = lv_obj_create(ui.root);
    lv_obj_remove_style_all(off);
    lv_obj_add_flag(off, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(off, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(off, 132, 52);
    lv_obj_align(off, LV_ALIGN_CENTER, 0, 128);
    lv_obj_set_style_radius(off, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(off, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_border_color(off, lv_color_white(), 0);

    lv_obj_t *offLabel = lv_label_create(off);
    lv_label_set_text(offLabel, LV_SYMBOL_POWER "  Off");
    lv_obj_set_style_text_font(offLabel, &portal_ui::portal_font_small_22, 0);
    lv_obj_set_style_text_color(offLabel, lv_color_white(), 0);
    lv_obj_center(offLabel);

    lv_obj_add_event_cb(
        off,
        modeButtonCb,
        LV_EVENT_CLICKED,
        reinterpret_cast<void *>(static_cast<intptr_t>(ClimateMode::Off))
    );
    gModeButtons[static_cast<int>(ClimateMode::Off)] = off;

    lv_obj_add_event_cb(ui.root, modeSwipeCb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(ui.root, modeSwipeCb, LV_EVENT_RELEASED, nullptr);

    refreshModeButtons();
}

void buildClimate() {
    gClimate.screen = lv_obj_create(nullptr);
    gClimate.ui = portal_ui::createScreen(gClimate.screen, portal_ui::palette::AUTO);

    portal_ui::showTicks(gClimate.ui, true);

    // The badge above the title is the thermostat dial outline
    lv_obj_set_style_text_font(gClimate.ui.badgeIcon, &portal_ui::portal_font_icons_40, 0);
    lv_label_set_text(gClimate.ui.badgeIcon, portal_ui::icon::THERMOSTAT);

    renderClimateIdle();
    attachInput(gClimate);
}

void buildLights() {
    gLights.screen = lv_obj_create(nullptr);
    gLights.ui = portal_ui::createScreen(gLights.screen, portal_ui::palette::LIGHTS_ON);

    portal_ui::setTitle(gLights.ui, "Ground Floor");
    portal_ui::setArcValue(gLights.ui, gBrightness);

    gPowerButton = portal_ui::createPowerButton(gLights.ui, lv_color_hex(0xE0189B));
    lv_obj_add_event_cb(gPowerButton, powerButtonCb, LV_EVENT_CLICKED, nullptr);

    attachInput(gLights);
    showLightsIdle();
}

}  // namespace

[[noreturn]] void runPortal(const BoardDrivers::HardwareHandles &hw) {
    ESP_LOGI(TAG, "=== Portal UI ===");
    gHw = hw;

    if (lvgl_wrapper::lock(100)) {
        buildClimate();
        buildLights();

        settings_screen::build(hw, []() { home_screen::show(); });
        buildClimateModes();
        lights_pages::build(
            []() { lv_screen_load(gLights.screen); },
            []() { home_screen::show(); }
        );

        home_screen::build([](home_screen::App app) {
            switch (app) {
                case home_screen::App::Climate:
                    lv_screen_load_anim(gClimate.screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
                    break;
                case home_screen::App::Lights:
                    lv_screen_load_anim(gLights.screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
                    break;
                case home_screen::App::Settings:
                    settings_screen::show();
                    break;
                default: {
                    lv_obj_t *screen = portal_apps::entryScreen(app);
                    if (screen != nullptr) {
                        lv_screen_load_anim(screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
                    }
                    break;
                }
            }
        });

        portal_apps::setHomeHandler([]() { home_screen::show(); });
        portal_apps::setExpander(hw.ioExpander);
        home_screen::show();
        lvgl_wrapper::unlock();
    }

    // Builds the rest of the apps, taking the lock per app itself
    portal_apps::buildAll();

    // Once there is a network, pull the real devices in. Until that lands the
    // screens keep their placeholder values, so the interface is usable
    // whether or not Home Assistant answers.
    if ((HA_TOKEN[0] != 0) && (ha_client::begin(HA_HOST, HA_TOKEN) == ESP_OK)) {
        startHomeAssistant();
    } else {
        ESP_LOGW(TAG, "No Home Assistant token set, running on placeholder data");
    }

    if (hw.imu != nullptr) {
        if (lvgl_wrapper::lock(100)) {
            lv_timer_create(imuTimerCb, 150, hw.imu);
            lvgl_wrapper::unlock();
        }
        ESP_LOGI(TAG, "Turn-to-adjust active");
    } else {
        ESP_LOGW(TAG, "No IMU handle, turn-to-adjust unavailable");
    }

    ESP_LOGI(TAG, "Portal UI ready: turn the board or drag the rim, swipe to change app");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));

    }
}

}  // namespace portal_demo
