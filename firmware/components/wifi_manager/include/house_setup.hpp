/**
 * @file house_setup.hpp
 * @brief Check the system choice from the install form
 *
 * No ESP headers, so the rules can be checked on a desktop.
 */

#pragma once

#include <cstddef>

constexpr size_t HOUSE_HOST_MAX = 63;
constexpr size_t HOUSE_TOKEN_MAX = 511;
constexpr size_t HOUSE_ZONE_MAX = 31;

enum class HouseKind {
    None,
    HomeAssistant,
    Homey,
};

/** @brief What was stored, or what the form is allowed to store */
struct HouseChoice {
    HouseKind kind;
    char host[HOUSE_HOST_MAX + 1];
    char token[HOUSE_TOKEN_MAX + 1];
    char zone[HOUSE_ZONE_MAX + 1];
};

/**
 * @brief Accept one install form
 * @details Host may include a scheme or a port; those are removed. An empty
 *          token keeps the previous token when the system and the host did
 *          not change. Zone is kept for Homey only.
 * @param previous Earlier choice, or nullptr when nothing is stored yet
 * @param error Receives a short Dutch reason when this returns false
 * @return True when out is ready to store
 */
bool acceptHouseChoice(
    const char *system,
    const char *host,
    const char *token,
    const char *zone,
    const HouseChoice *previous,
    HouseChoice *out,
    char *error,
    size_t errorSize
);
