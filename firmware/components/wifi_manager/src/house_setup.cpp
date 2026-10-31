/**
 * @file house_setup.cpp
 * @brief Check the system choice from the install form
 */

#include "house_setup.hpp"

#include <cstring>

namespace {

void copyTrunc(char *dest, size_t size, const char *src) {
    if (size == 0) {
        return;
    }
    if (src == nullptr) {
        dest[0] = '\0';
        return;
    }
    std::strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
}

void setError(char *error, size_t errorSize, const char *text) {
    copyTrunc(error, errorSize, text);
}

bool sameText(const char *left, const char *right) {
    if ((left == nullptr) || (right == nullptr)) {
        return false;
    }
    return std::strcmp(left, right) == 0;
}

void trimEnds(char *text) {
    if (text == nullptr) {
        return;
    }
    size_t start = 0;
    while ((text[start] == ' ') || (text[start] == '\t')) {
        start++;
    }
    if (start > 0) {
        std::memmove(text, text + start, std::strlen(text + start) + 1);
    }
    size_t length = std::strlen(text);
    while ((length > 0) && ((text[length - 1] == ' ') || (text[length - 1] == '\t'))) {
        text[length - 1] = '\0';
        length--;
    }
}

bool startsWithIgnoreCase(const char *text, const char *prefix) {
    for (size_t i = 0; prefix[i] != '\0'; i++) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if ((c >= 'A') && (c <= 'Z')) {
            c = static_cast<unsigned char>(c - 'A' + 'a');
        }
        if (c != static_cast<unsigned char>(prefix[i])) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Copy a bare host
 * @details Scheme, path and port are dropped. A long path does not count
 *          toward the host length. IPv4 only: the first colon ends the host.
 */
bool extractHost(const char *input, char *dest, size_t destSize, char *error, size_t errorSize) {
    if (destSize == 0) {
        return false;
    }
    dest[0] = '\0';
    if (input == nullptr) {
        input = "";
    }
    while ((*input == ' ') || (*input == '\t')) {
        input++;
    }
    if (startsWithIgnoreCase(input, "https://")) {
        input += 8;
    } else if (startsWithIgnoreCase(input, "http://")) {
        input += 7;
    }
    while ((*input == ' ') || (*input == '\t')) {
        input++;
    }

    size_t length = 0;
    while ((input[length] != '\0') && (input[length] != '/') && (input[length] != ':') &&
           (input[length] != ' ') && (input[length] != '\t')) {
        length++;
    }

    const char *rest = input + length;
    if ((input[length] == ' ') || (input[length] == '\t')) {
        while ((*rest == ' ') || (*rest == '\t')) {
            rest++;
        }
        if ((*rest != '\0') && (*rest != '/') && (*rest != ':')) {
            setError(error, errorSize, "Adres mag geen spaties hebben");
            return false;
        }
    }

    if (length == 0) {
        setError(error, errorSize, "Vul het adres in");
        return false;
    }
    if ((length > HOUSE_HOST_MAX) || (length >= destSize)) {
        setError(error, errorSize, "Adres is te lang");
        return false;
    }
    std::memcpy(dest, input, length);
    dest[length] = '\0';
    return true;
}

}  // namespace

bool acceptHouseChoice(
    const char *system,
    const char *host,
    const char *token,
    const char *zone,
    const HouseChoice *previous,
    HouseChoice *out,
    char *error,
    size_t errorSize
) {
    if ((out == nullptr) || (error == nullptr) || (errorSize == 0)) {
        return false;
    }
    error[0] = '\0';
    *out = {};

    HouseKind kind = HouseKind::None;
    if ((system != nullptr) && (std::strcmp(system, "ha") == 0)) {
        kind = HouseKind::HomeAssistant;
    } else if ((system != nullptr) && (std::strcmp(system, "homey") == 0)) {
        kind = HouseKind::Homey;
    } else {
        setError(error, errorSize, "Kies Home Assistant of Homey");
        return false;
    }

    char cleanedHost[HOUSE_HOST_MAX + 1] = {};
    if (!extractHost(host, cleanedHost, sizeof(cleanedHost), error, errorSize)) {
        return false;
    }

    char cleanedToken[HOUSE_TOKEN_MAX + 8] = {};
    copyTrunc(cleanedToken, sizeof(cleanedToken), (token != nullptr) ? token : "");
    trimEnds(cleanedToken);

    const bool sameAsPrevious =
        (previous != nullptr) && (previous->kind == kind) &&
        sameText(previous->host, cleanedHost) && (previous->token[0] != '\0');

    if (cleanedToken[0] == '\0') {
        if (!sameAsPrevious) {
            setError(error, errorSize, "Vul de toegangssleutel in");
            return false;
        }
        copyTrunc(cleanedToken, sizeof(cleanedToken), previous->token);
    }

    if (std::strlen(cleanedToken) > HOUSE_TOKEN_MAX) {
        setError(error, errorSize, "Toegangssleutel is te lang");
        return false;
    }

    out->kind = kind;
    copyTrunc(out->host, sizeof(out->host), cleanedHost);
    copyTrunc(out->token, sizeof(out->token), cleanedToken);
    if (kind == HouseKind::Homey) {
        char cleanedZone[HOUSE_ZONE_MAX + 8] = {};
        copyTrunc(cleanedZone, sizeof(cleanedZone), (zone != nullptr) ? zone : "");
        trimEnds(cleanedZone);
        if (std::strlen(cleanedZone) > HOUSE_ZONE_MAX) {
            setError(error, errorSize, "Zonenaam is te lang");
            *out = {};
            return false;
        }
        copyTrunc(out->zone, sizeof(out->zone), cleanedZone);
    }
    return true;
}
