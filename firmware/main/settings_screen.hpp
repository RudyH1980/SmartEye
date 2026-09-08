/**
 * @file settings_screen.hpp
 * @brief Settings: brightness, network, and starting the setup over
 */

#pragma once

#include "board_drivers.hpp"

namespace settings_screen {

/**
 * @brief Build the settings screen
 * @param hw Hardware handles, for the backlight
 * @param onHome Called when the user leaves the screen
 */
void build(const BoardDrivers::HardwareHandles &hw, void (*onHome)());

/** @brief Make it the active screen */
void show();

}  // namespace settings_screen
