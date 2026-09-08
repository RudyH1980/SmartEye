/**
 * @file ha_client.hpp
 * @brief Reads devices from Home Assistant and sends commands back
 *
 * Home Assistant already talks to Homey, so going through it reaches both
 * without needing a second connection. Everything runs over the REST API with
 * a long-lived access token; the token lives in secrets.hpp, which is not in
 * version control.
 */

#pragma once

#include <cstdint>
#include <functional>

#include "esp_err.h"

namespace ha_client {

/** @brief The kinds of device this firmware knows how to show */
enum class Domain {
    Light,
    Climate,
    MediaPlayer,
    Scene,
    Sensor,
    Other,
};

constexpr size_t MAX_DEVICES = 40;
constexpr size_t MAX_ID = 64;
constexpr size_t MAX_NAME = 40;
constexpr size_t MAX_AREA = 32;

/** @brief One device as Home Assistant reports it */
struct Device {
    char entityId[MAX_ID];   // e.g. "light.living_room"
    char name[MAX_NAME];     // Friendly name
    char area[MAX_AREA];     // Room, empty when HA does not say
    Domain domain;

    bool on;                 // Light or media player is on
    int32_t brightness;      // 0-100, -1 when it has none
    int32_t temperature;     // Target, in tenths of a degree
    int32_t currentTemp;     // Measured, in tenths of a degree
    int32_t humidity;        // Percent, -1 when it has none

    // Home Assistant is where groups are made. A light group arrives as one
    // entity that lists its members, and that is what the screen shows: the
    // device cannot invent groups of its own that Home Assistant knows
    // nothing about.
    bool isGroup;
    int32_t memberCount;     // 1 for a plain lamp
};

/** @brief Told once a fetch finishes */
using DevicesCallback = std::function<void(const Device *devices, size_t count)>;

/**
 * @brief Start talking to Home Assistant
 * @param host Address, e.g. "192.168.1.109"
 * @param token Long-lived access token
 * @return ESP_OK when the settings are accepted; connecting happens per call
 */
esp_err_t begin(const char *host, const char *token);

/**
 * @brief Limit everything that follows to one room
 * @details SmartEye sits in a room and shows that room. Home Assistant knows
 *          which area an entity belongs to, so the screen fills itself from
 *          the area name rather than from a hand-written list.
 * @param area Area name exactly as Home Assistant spells it, or empty for all
 */
void setRoom(const char *area);

/** @brief The room currently being shown */
const char *room();

/**
 * @brief Fetch the current devices
 * @details Runs on its own task and calls back when done, so it never blocks
 *          the interface.
 * @param callback Given the devices; the array is only valid during the call
 */
esp_err_t refresh(DevicesCallback callback);

/** @brief Switch a light or a switch on or off */
esp_err_t setPower(const char *entityId, bool on);

/** @brief Set a light's brightness, 0-100 */
esp_err_t setBrightness(const char *entityId, int32_t percent);

/** @brief Activate a scene, which is what a mood is in Home Assistant */
esp_err_t activateScene(const char *entityId);

/** @brief Set a thermostat's target, in tenths of a degree */
esp_err_t setTemperature(const char *entityId, int32_t tenths);

/**
 * @brief Read one entity's state as text
 * @details Meant for the helper entities that configure this display. They
 *          are ordinary Home Assistant entities, so they can be changed from
 *          any dashboard, on a phone or in a browser.
 * @param entityId e.g. "input_select.smarteye_ruimte"
 * @param out Receives the state
 * @param size Size of out
 * @return ESP_ERR_NOT_FOUND when the helper does not exist
 */
esp_err_t readHelper(const char *entityId, char *out, size_t size);

/** @brief Whether the last exchange with Home Assistant succeeded */
bool isConnected();

}  // namespace ha_client
