/**
 * @file lights_pages.hpp
 * @brief The two screens behind the lights app: colour wheel and temperature
 */

#pragma once

#include "lvgl.h"

namespace lights_pages {

/**
 * @brief Build both screens
 * @param onBack Called when the user swipes back past the first page
 * @param onHome Called on a swipe down
 */
void build(void (*onBack)(), void (*onHome)());

/** @brief Show the colour wheel */
void showWheel();

}  // namespace lights_pages
