/**
 * @file homey_client.hpp
 * @brief Reads devices from a Homey Pro on the LAN and sends commands back
 *
 * Local Web API, the same one the Homey Web App uses:
 *   GET  /api/manager/devices/device/
 *   PUT  /api/manager/devices/device/<id>/capability/<capability>
 *   POST /api/manager/flow/flow/<id>/trigger
 *   POST /api/manager/flow/advancedflow/<id>/trigger
 * Authorization is a bearer API key from Homey, Settings, API Keys.
 * The key lives in secrets.hpp and is not committed.
 *
 * Entity ids are prefixed so the screen can tell Homey and Home Assistant
 * apart: homey:<device>, homey-flow:<flow>, homey-aflow:<advanced flow>.
 */

#pragma once

#include "ha_client.hpp"

namespace homey_client {

/**
 * @brief Remember the Homey address and API key
 * @param host IP or hostname, no scheme and no port. The call uses port 80.
 * @param token API key from Homey, Settings, API Keys
 * @return ESP_OK when the settings are accepted. Connecting happens per call.
 */
esp_err_t begin(const char *host, const char *token);

/**
 * @brief Limit devices to one zone
 * @param zone Zone name exactly as Homey spells it, or empty for every zone.
 *             Flows are not zoned, so startable flows stay in the list.
 */
void setZone(const char *zone);

/** @brief The zone currently being shown */
const char *zone();

/**
 * @brief Fetch lights, thermostats and startable flows
 * @details Runs on its own task. The callback receives ha_client::Device
 *          values so the screens that already understand Home Assistant can
 *          show Homey without a second UI path. The array is only valid
 *          during the callback. On failure the callback gets a null pointer.
 */
esp_err_t refresh(ha_client::DevicesCallback callback);

/** @brief True when the id belongs to this client */
bool owns(const char *entityId);

/** @brief Switch a Homey light on or off. entityId includes the homey: prefix. */
esp_err_t setPower(const char *entityId, bool on);

/** @brief Set a Homey light's dim level, 0-100 */
esp_err_t setBrightness(const char *entityId, int32_t percent);

/** @brief Set a Homey thermostat target, in tenths of a degree */
esp_err_t setTemperature(const char *entityId, int32_t tenths);

/**
 * @brief Start a flow
 * @param entityId homey-flow: or homey-aflow: id from the last refresh
 */
esp_err_t triggerFlow(const char *entityId);

/** @brief Whether the last refresh reached Homey */
bool isConnected();

}  // namespace homey_client
