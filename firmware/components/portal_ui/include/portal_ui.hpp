/**
 * @file portal_ui.hpp
 * @brief Round-screen UI shell modelled on the Homey Portal layout
 *
 * Every app screen shares the same anatomy, so it lives in one place here:
 *
 *      .-""""""""-.        tick ring just inside the rim
 *    .'   (icon)   '.      round badge, top centre
 *   /     Thermostat \     title
 *  |                  |
 *  |       21 5       |    hero value with superscript
 *  |    19.5  |  58%  |    secondary row
 *   \                /
 *    '.    ____    .'      home pill, bottom centre
 *      '-.......-'
 *
 * Everything is positioned relative to the centre and kept inside the circle,
 * because on a round panel anything in the corners is simply not there.
 */

#pragma once

#include "lvgl.h"

namespace portal_ui {

// The panel is a 480px circle. A centred square only fits fully inside it up
// to 480/sqrt(2); content beyond that has to be placed radially instead.
constexpr int32_t SCREEN_SIZE = 480;
constexpr int32_t SCREEN_RADIUS = SCREEN_SIZE / 2;
constexpr int32_t SAFE_SQUARE = 339;

/** Fonts generated from Montserrat Bold (SIL OFL) */
extern "C" {
extern const lv_font_t portal_font_hero_112;
extern const lv_font_t portal_font_super_44;
extern const lv_font_t portal_font_title_30;
extern const lv_font_t portal_font_small_22;

/** App glyphs, subset of Material Icons (Apache 2.0) */
extern const lv_font_t portal_font_icons_40;
}

/** UTF-8 for the glyphs present in portal_font_icons_40 */
namespace icon {
constexpr const char *THERMOSTAT = "\xEF\x81\xB6";  // U+F076
constexpr const char *SPEAKER = "\xEE\x81\x90";     // U+E050
constexpr const char *WEATHER = "\xEE\x90\xB0";     // U+E430
constexpr const char *LIGHTBULB = "\xEE\x83\xB0";   // U+E0F0
constexpr const char *ENERGY = "\xEE\xA8\x8B";      // U+EA0B
constexpr const char *TIMER = "\xEE\x90\xA5";       // U+E425
constexpr const char *MOOD = "\xEE\x99\x9F";        // U+E65F
constexpr const char *POWER = "\xEE\xA2\xAC";       // U+E8AC
constexpr const char *HOME = "\xEE\xA2\x8A";        // U+E88A
constexpr const char *SETTINGS = "\xEE\xA2\xB8";    // U+E8B8
}  // namespace icon

/** @brief Direction of a finished swipe */
enum class Swipe {
    None,
    Left,
    Right,
    Up,
    Down,
};

/**
 * @brief Work out the swipe from where a touch started and ended
 * @details Feed this both LV_EVENT_PRESSED and LV_EVENT_RELEASED. It reads
 *          only the two end points, so a fast flick that the controller only
 *          samples a few times is recognised just as well as a slow drag.
 *          LVGL's own gesture detection adds up the steps in between, which
 *          is exactly what a fast swipe does not have enough of.
 * @param event The press or release event
 * @return The direction on release, None otherwise
 */
Swipe swipeFromEvent(lv_event_t *event);

/**
 * @brief Colour pair for the background gradient
 * @details The background carries the state: warm orange while heating, blue
 *          while cooling, magenta for lights on, near-black when idle.
 */
struct Palette {
    lv_color_t top;
    lv_color_t bottom;
};

namespace palette {
extern const Palette HEATING;
extern const Palette COOLING;
extern const Palette AUTO;
extern const Palette LIGHTS_ON;
extern const Palette MUSIC;
extern const Palette ENERGY;
extern const Palette IDLE;
}  // namespace palette

/**
 * @brief One screen built on the shared anatomy
 * @details Members are the parts you fill in per app. Anything left unset
 *          stays hidden, so a screen only shows what it actually has.
 */
struct Screen {
    lv_obj_t *root;       // Full-screen gradient background
    lv_obj_t *ticks;      // Tick ring along the rim
    lv_obj_t *arc;        // Value indicator arc along the rim
    lv_obj_t *badge;      // Round icon badge, top centre
    lv_obj_t *badgeIcon;  // Label inside the badge
    lv_obj_t *title;      // Zone or device name
    lv_obj_t *hero;       // Large central value
    lv_obj_t *superscript;  // Raised decimal next to the hero value
    lv_obj_t *secondary;  // Row of small readings under the hero
    lv_obj_t *homePill;   // Bottom affordance

    // Which gradient is currently applied, so setting the same one again is
    // free rather than a full-screen repaint
    const Palette *palette;

    // Gradient descriptors belong to the screen, not to the function that
    // sets them: LVGL keeps the pointer and reads through it while drawing,
    // and caches state inside it. Sharing one descriptor between screens, or
    // rebuilding it on every value change, corrupts that cache and hangs the
    // display thread.
    lv_grad_dsc_t *glowGradient;
    lv_grad_dsc_t *flatGradient;

    // Where the hero value sits vertically. With the badge and title above it
    // the value is nudged down to balance them; on their own, the value and
    // the line under it are centred as a pair.
    int32_t heroOffsetY;
};

/**
 * @brief Build the shared shell on a screen object
 * @param parent Screen to build on, or nullptr for the active screen
 * @param colours Background gradient
 * @return The assembled screen; parts are hidden until you set them
 */
Screen createScreen(lv_obj_t *parent, const Palette &colours);

/** @brief Swap the background gradient, e.g. when a device changes state */
void setPalette(Screen &screen, const Palette &colours);

/** @brief Set the zone or device name shown under the badge */
void setTitle(Screen &screen, const char *text);

/**
 * @brief Set the large central value
 * @param value Main part, e.g. "21" or "50"
 * @param super Raised part drawn next to it (decimal, % or °), or nullptr
 */
void setHero(Screen &screen, const char *value, const char *super);

/** @brief Set the small readings row, e.g. "19.5°  |  58%" */
void setSecondary(Screen &screen, const char *text);

/** @brief Set the arc that runs along the rim, 0-100 */
void setArcValue(Screen &screen, int32_t percent);

/** @brief Show or hide the tick ring along the rim */
void showTicks(Screen &screen, bool visible);

/** @brief Show or hide the round icon badge and title block */
void showHeader(Screen &screen, bool visible);

/**
 * @brief Switch the background to the dimmed radial glow used while adjusting
 * @details The Portal drops the bright state colour for a dark, centred glow
 *          the moment you start turning a value, so the number carries the
 *          screen instead of the colour.
 */
void setGlowPalette(Screen &screen, lv_color_t glow);

/**
 * @brief Add the large round power button used on the lights screens
 * @param screen Screen to add it to
 * @param symbolColour Colour of the glyph inside the white circle
 * @return The button, so you can hang an event on it
 */
lv_obj_t *createPowerButton(Screen &screen, lv_color_t symbolColour);

}  // namespace portal_ui
