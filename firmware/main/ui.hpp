/**
 * @file ui.hpp
 * @brief Simple UI demo for LVGL
 * @details Creates a basic user interface with buttons, labels, and interactive elements
 */

#pragma once

#include "lvgl.h"

namespace ui {

/**
 * @brief Create a simple demo UI with buttons and labels
 * @details Creates:
 *   - Title label
 *   - Counter display
 *   - Increment/Decrement buttons
 *   - Color toggle button
 *   - Status bar with clock
 */
void createDemoUI();

/**
 * @brief Update the status bar clock (call periodically)
 */
void updateClock();

}  // namespace ui
