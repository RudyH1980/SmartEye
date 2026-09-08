/**
 * @file setup_screen.hpp
 * @brief First-run screen: scan the code, join, fill in the network
 */

#pragma once

namespace setup_screen {

/** @brief Build the setup screen and make it the active one */
void show();

/** @brief Board is opening its own network; show the join code */
void showJoinCode();

/** @brief Board is trying a network */
void showConnecting(const char *networkName);

/** @brief Board is on the network */
void showConnected(const char *ipAddress);

}  // namespace setup_screen
