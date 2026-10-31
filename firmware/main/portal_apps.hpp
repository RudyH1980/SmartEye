/**
 * @file portal_apps.hpp
 * @brief The remaining Portal apps: speakers, weather, energy, timer, moods
 */

#pragma once

#include "esp_io_expander.h"
#include "ha_client.hpp"
#include "home_screen.hpp"
#include "lvgl.h"

namespace portal_apps {

/** @brief Tell these apps how to get back to the home screen */
void setHomeHandler(void (*goHome)());

/** @brief Give the apps the IO expander, so the timer can sound the buzzer */
void setExpander(esp_io_expander_handle_t expander);

/** @brief Build every app's screens; call once, with the LVGL lock held */
void buildAll();

/**
 * @brief Called when a mood orb is tapped and it has a real flow or scene
 * @param entityId Home Assistant scene id, or a homey-flow / homey-aflow id
 */
void setMoodHandler(void (*activate)(const char *entityId));

/**
 * @brief Replace the placeholder moods with scenes from the house
 * @details Call with the LVGL lock held. No scenes leaves the placeholders.
 *          The first five scenes are shown. The rest wait for a later refresh.
 */
void setMoods(const ha_client::Device *devices, size_t count);

/**
 * @brief First screen of an app
 * @param app Which app
 * @return Its entry screen, or nullptr if that app is not built here
 */
lv_obj_t *entryScreen(home_screen::App app);

}  // namespace portal_apps
