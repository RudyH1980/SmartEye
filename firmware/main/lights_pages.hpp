/**
 * @file lights_pages.hpp
 * @brief The two screens behind the lights app: colour wheel and temperature
 */

#pragma once

#include "ha_client.hpp"
#include "lvgl.h"

namespace lights_pages {

/**
 * @brief Build both screens
 * @param onBack Called when the user swipes back past the first page
 * @param onHome Called on a swipe down
 */
void build(void (*onBack)(), void (*onHome)());

/**
 * @brief Replace the lamps with what Home Assistant reports
 * @details Both colour screens read from this one list, so the wheel and the
 *          temperature page can never disagree about which lamps exist or how
 *          they are grouped. Groups come from Home Assistant: a light group
 *          arrives as a single entity that knows how many lamps it holds.
 * @param devices What Home Assistant returned
 * @param count How many
 */
void setLamps(const ha_client::Device *devices, size_t count);

/** @brief Show the colour wheel */
void showWheel();

}  // namespace lights_pages
