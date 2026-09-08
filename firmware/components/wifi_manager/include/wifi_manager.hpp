/**
 * @file wifi_manager.hpp
 * @brief Wi-Fi with on-device setup, no reflashing to change networks
 *
 * On boot the stored credentials are tried. If there are none, or they no
 * longer work, the board opens its own network and serves a small page where
 * you pick a network and type its password. What you enter goes to NVS, so it
 * survives a power cut and every later boot connects straight away.
 */

#pragma once

#include <functional>

#include "esp_err.h"

namespace wifi_manager {

/** @brief Name of the network the board opens for setup */
constexpr const char *SETUP_AP_NAME = "SmartEye-setup";

/** @brief Address of the setup page while that network is up */
constexpr const char *SETUP_URL = "http://192.168.4.1";

enum class State {
    Idle,          // Nothing started yet
    Connecting,    // Trying stored credentials
    Connected,     // On the network
    Provisioning,  // Own network open, waiting for someone to fill in the page
    Failed,        // Could not connect and could not open the setup network
};

/**
 * @brief Called whenever the state changes
 * @param state New state
 * @param detail Network name when connected, IP address, or an error; may be
 *               empty. Only valid during the call.
 */
using StateCallback = std::function<void(State state, const char *detail)>;

/**
 * @brief Start Wi-Fi: connect if we can, otherwise offer setup
 * @param callback Told about every state change, may be nullptr
 * @return ESP_OK once started; connecting itself happens in the background
 */
esp_err_t start(StateCallback callback);

/** @brief Whether credentials are stored */
bool hasStoredCredentials();

/** @brief Wipe stored credentials, so the next boot asks again */
esp_err_t forgetCredentials();

/** @brief Current state */
State currentState();

/** @brief IP address once connected, empty otherwise */
const char *ipAddress();

}  // namespace wifi_manager
