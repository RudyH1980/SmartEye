/**
 * @file homey_map.hpp
 * @brief Turn Homey Web API JSON into the short device list the screen uses
 *
 * No network and no ESP headers, so the mapping can be checked on a desktop.
 */

#pragma once

#include <cstddef>
#include <cstdint>

constexpr size_t HOMEY_MAX_DEVICES = 40;
constexpr size_t HOMEY_MAX_ZONES = 48;
constexpr size_t HOMEY_MAX_ID = 64;
constexpr size_t HOMEY_MAX_NAME = 40;
constexpr size_t HOMEY_MAX_AREA = 32;
constexpr size_t HOMEY_MAX_ZONE_ID = 40;

enum class HomeyDomain : uint8_t {
    Light,
    Climate,
    Scene,
    Other,
};

/** @brief One zone id and the name a person actually sees */
struct HomeyZone {
    char id[HOMEY_MAX_ZONE_ID];
    char name[HOMEY_MAX_AREA];
};

/**
 * @brief One device or one startable flow
 * @details entityId is already prefixed: homey:, homey-flow: or homey-aflow:.
 *          brightness, temperature and currentTemp are -1 when Homey has no
 *          such capability. Temperatures are tenths of a degree.
 */
struct HomeyDevice {
    char entityId[HOMEY_MAX_ID];
    char name[HOMEY_MAX_NAME];
    char area[HOMEY_MAX_AREA];
    HomeyDomain domain;
    bool on;
    int32_t brightness;
    int32_t temperature;
    int32_t currentTemp;
    int32_t memberCount;
};

/**
 * @brief Read the zone object from GET /api/manager/zones/zone/
 * @return How many zones were stored, or -1 when the JSON is not an object
 */
int loadHomeyZones(const char *json, HomeyZone *out, size_t max);

/**
 * @brief Append lights and thermostats from GET /api/manager/devices/device/
 * @details A light is class "light", or anything with dim / light_hue /
 *          light_temperature. A thermostat is class "thermostat" or anything
 *          with target_temperature. An unavailable device is skipped.
 *          zoneFilter empty keeps every zone. Otherwise the zone name must
 *          match exactly.
 * @param filled How many entries out already holds
 * @return The new total, or -1 when the device list cannot be read
 */
int appendHomeyDevices(
    const char *json,
    const HomeyZone *zones,
    size_t zoneCount,
    const char *zoneFilter,
    HomeyDevice *out,
    size_t filled,
    size_t max
);

/**
 * @brief Append flows the screen may start
 * @details Standard flows count only when they are enabled and their trigger
 *          is the card "this flow is started" (uri homey:manager:flow, id
 *          trigger or programmatic_trigger). Advanced flows count when they
 *          are enabled and triggerable. A missing or empty body adds nothing.
 * @param advanced True for /api/manager/flow/advancedflow/
 * @return The new total, or -1 when a non-empty body is not JSON
 */
int appendHomeyFlows(
    const char *json,
    bool advanced,
    HomeyDevice *out,
    size_t filled,
    size_t max
);
