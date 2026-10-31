/**
 * @file homey_map.cpp
 * @brief Turn Homey Web API JSON into the short device list the screen uses
 */

#include "homey_map.hpp"

#include <cstring>

#include "cJSON.h"

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

bool prefixedId(char *dest, size_t size, const char *prefix, const char *id) {
    if ((prefix == nullptr) || (id == nullptr) || (id[0] == '\0')) {
        return false;
    }
    const size_t prefixLen = std::strlen(prefix);
    const size_t idLen = std::strlen(id);
    if ((prefixLen + idLen + 1) > size) {
        return false;
    }
    std::memcpy(dest, prefix, prefixLen);
    std::memcpy(dest + prefixLen, id, idLen + 1);
    return true;
}

bool hasCapability(const cJSON *device, const char *capability) {
    const cJSON *caps = cJSON_GetObjectItemCaseSensitive(device, "capabilities");
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, caps) {
        if (cJSON_IsString(item) && (item->valuestring != nullptr) &&
            (std::strcmp(item->valuestring, capability) == 0)) {
            return true;
        }
    }
    return false;
}

const cJSON *capabilityValue(const cJSON *device, const char *capability) {
    const cJSON *obj = cJSON_GetObjectItemCaseSensitive(device, "capabilitiesObj");
    if (!cJSON_IsObject(obj)) {
        return nullptr;
    }
    const cJSON *cap = cJSON_GetObjectItemCaseSensitive(obj, capability);
    if (!cJSON_IsObject(cap)) {
        return nullptr;
    }
    return cJSON_GetObjectItemCaseSensitive(cap, "value");
}

bool capabilityBool(const cJSON *device, const char *capability, bool fallback) {
    const cJSON *value = capabilityValue(device, capability);
    if (cJSON_IsBool(value)) {
        return cJSON_IsTrue(value);
    }
    if (cJSON_IsNumber(value)) {
        return value->valuedouble != 0.0;
    }
    return fallback;
}

/** @brief -1 when the capability is missing */
int capabilityPercent(const cJSON *device, const char *capability) {
    const cJSON *value = capabilityValue(device, capability);
    if (!cJSON_IsNumber(value)) {
        return -1;
    }
    double dim = value->valuedouble;
    if (dim < 0.0) {
        return -1;
    }
    if (dim > 1.0) {
        dim = 1.0;
    }
    return static_cast<int>(dim * 100.0 + 0.5);
}

/** @brief Tenths of a degree, or -1 when the capability is missing */
int capabilityTenths(const cJSON *device, const char *capability) {
    const cJSON *value = capabilityValue(device, capability);
    if (!cJSON_IsNumber(value)) {
        return -1;
    }
    const double degrees = value->valuedouble;
    if (degrees >= 0.0) {
        return static_cast<int>(degrees * 10.0 + 0.5);
    }
    return static_cast<int>(degrees * 10.0 - 0.5);
}

const char *classOf(const cJSON *device) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(device, "class");
    if (cJSON_IsString(item) && (item->valuestring != nullptr)) {
        return item->valuestring;
    }
    return "";
}

bool isLight(const cJSON *device) {
    if (std::strcmp(classOf(device), "light") == 0) {
        return true;
    }
    return hasCapability(device, "dim") || hasCapability(device, "light_hue") ||
           hasCapability(device, "light_temperature");
}

bool isClimate(const cJSON *device) {
    if (std::strcmp(classOf(device), "thermostat") == 0) {
        return true;
    }
    return hasCapability(device, "target_temperature");
}

bool isAvailable(const cJSON *device) {
    const cJSON *available = cJSON_GetObjectItemCaseSensitive(device, "available");
    if (cJSON_IsBool(available)) {
        return cJSON_IsTrue(available);
    }
    return true;
}

const char *zoneName(const HomeyZone *zones, size_t zoneCount, const char *zoneId) {
    if ((zoneId == nullptr) || (zoneId[0] == '\0')) {
        return "";
    }
    for (size_t i = 0; i < zoneCount; i++) {
        if (std::strcmp(zones[i].id, zoneId) == 0) {
            return zones[i].name;
        }
    }
    return "";
}

bool zoneAllowed(const char *area, const char *zoneFilter) {
    if ((zoneFilter == nullptr) || (zoneFilter[0] == '\0')) {
        return true;
    }
    return std::strcmp(area, zoneFilter) == 0;
}

void blankDevice(HomeyDevice *device) {
    device->entityId[0] = '\0';
    device->name[0] = '\0';
    device->area[0] = '\0';
    device->domain = HomeyDomain::Other;
    device->on = false;
    device->brightness = -1;
    device->temperature = -1;
    device->currentTemp = -1;
    device->memberCount = 1;
}

bool isStartedFlow(const cJSON *flow) {
    const cJSON *trigger = cJSON_GetObjectItemCaseSensitive(flow, "trigger");
    if (!cJSON_IsObject(trigger)) {
        return false;
    }
    const cJSON *uri = cJSON_GetObjectItemCaseSensitive(trigger, "uri");
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(trigger, "id");
    if (!cJSON_IsString(uri) || (uri->valuestring == nullptr) ||
        (std::strcmp(uri->valuestring, "homey:manager:flow") != 0)) {
        return false;
    }
    if (!cJSON_IsString(id) || (id->valuestring == nullptr)) {
        return false;
    }
    return (std::strcmp(id->valuestring, "trigger") == 0) ||
           (std::strcmp(id->valuestring, "programmatic_trigger") == 0);
}

bool isEnabled(const cJSON *flow) {
    const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(flow, "enabled");
    return cJSON_IsBool(enabled) && cJSON_IsTrue(enabled);
}

}  // namespace

int loadHomeyZones(const char *json, HomeyZone *out, size_t max) {
    if ((json == nullptr) || (json[0] == '\0')) {
        return 0;
    }
    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return -1;
    }

    size_t count = 0;
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, root) {
        if ((count >= max) || (item->string == nullptr)) {
            continue;
        }
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
        if (!cJSON_IsString(name) || (name->valuestring == nullptr)) {
            continue;
        }
        if (std::strlen(item->string) >= HOMEY_MAX_ZONE_ID) {
            continue;
        }
        copyTrunc(out[count].id, sizeof(out[count].id), item->string);
        copyTrunc(out[count].name, sizeof(out[count].name), name->valuestring);
        count++;
    }

    cJSON_Delete(root);
    return static_cast<int>(count);
}

int appendHomeyDevices(
    const char *json,
    const HomeyZone *zones,
    size_t zoneCount,
    const char *zoneFilter,
    HomeyDevice *out,
    size_t filled,
    size_t max
) {
    if ((json == nullptr) || (json[0] == '\0') || (out == nullptr)) {
        return -1;
    }
    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return -1;
    }

    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, root) {
        if (filled >= max) {
            break;
        }
        if (!cJSON_IsObject(item) || (item->string == nullptr) || !isAvailable(item)) {
            continue;
        }

        const bool light = isLight(item);
        const bool climate = !light && isClimate(item);
        if (!light && !climate) {
            continue;
        }

        const cJSON *zoneItem = cJSON_GetObjectItemCaseSensitive(item, "zone");
        const char *zoneId = (cJSON_IsString(zoneItem) && (zoneItem->valuestring != nullptr))
                                 ? zoneItem->valuestring
                                 : "";
        const char *area = zoneName(zones, zoneCount, zoneId);
        if (!zoneAllowed(area, zoneFilter)) {
            continue;
        }

        HomeyDevice &device = out[filled];
        blankDevice(&device);
        if (!prefixedId(device.entityId, sizeof(device.entityId), "homey:", item->string)) {
            continue;
        }

        const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
        if (cJSON_IsString(name) && (name->valuestring != nullptr)) {
            copyTrunc(device.name, sizeof(device.name), name->valuestring);
        } else {
            copyTrunc(device.name, sizeof(device.name), item->string);
        }
        copyTrunc(device.area, sizeof(device.area), area);

        if (light) {
            device.domain = HomeyDomain::Light;
            device.on = capabilityBool(item, "onoff", false);
            device.brightness = capabilityPercent(item, "dim");
        } else {
            device.domain = HomeyDomain::Climate;
            device.temperature = capabilityTenths(item, "target_temperature");
            device.currentTemp = capabilityTenths(item, "measure_temperature");
        }
        filled++;
    }

    cJSON_Delete(root);
    return static_cast<int>(filled);
}

int appendHomeyFlows(
    const char *json,
    bool advanced,
    HomeyDevice *out,
    size_t filled,
    size_t max
) {
    if ((json == nullptr) || (json[0] == '\0')) {
        return static_cast<int>(filled);
    }
    if (out == nullptr) {
        return -1;
    }
    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return -1;
    }

    const char *prefix = advanced ? "homey-aflow:" : "homey-flow:";
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, root) {
        if (filled >= max) {
            break;
        }
        if (!cJSON_IsObject(item) || (item->string == nullptr) || !isEnabled(item)) {
            continue;
        }
        if (advanced) {
            const cJSON *triggerable = cJSON_GetObjectItemCaseSensitive(item, "triggerable");
            if (!cJSON_IsTrue(triggerable)) {
                continue;
            }
        } else if (!isStartedFlow(item)) {
            continue;
        }

        HomeyDevice &device = out[filled];
        blankDevice(&device);
        if (!prefixedId(device.entityId, sizeof(device.entityId), prefix, item->string)) {
            continue;
        }
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
        if (cJSON_IsString(name) && (name->valuestring != nullptr)) {
            copyTrunc(device.name, sizeof(device.name), name->valuestring);
        } else {
            copyTrunc(device.name, sizeof(device.name), item->string);
        }
        device.domain = HomeyDomain::Scene;
        filled++;
    }

    cJSON_Delete(root);
    return static_cast<int>(filled);
}
