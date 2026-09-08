/**
 * @file portal_demo.hpp
 * @brief Portal-style app screens running on mock data
 */

#pragma once

#include "board_drivers.hpp"

namespace portal_demo {

/**
 * @brief Run the Portal-style UI on mock data
 * @details Three ways to change a value, all driving the same state: drag
 *          along the rim, or turn the whole board like a dial (the IMU reads
 *          the turn), or tap for on/off. Swipe through the middle to move
 *          between apps. Never returns.
 * @param hw Hardware handles, for the IMU
 */
[[noreturn]] void runPortal(const BoardDrivers::HardwareHandles &hw);

}  // namespace portal_demo
