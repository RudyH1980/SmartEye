/**
 * @file homey_client.cpp
 * @brief Reads devices from a Homey Pro on the LAN and sends commands back
 */

#include "homey_client.hpp"

#include <cstdio>
#include <cstring>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "homey_map.hpp"

constexpr static const char *TAG = "HOMEY";

namespace homey_client {

namespace {

constexpr int HTTP_TIMEOUT_MS = 8000;
constexpr int COMMAND_TIMEOUT_MS = 4000;
constexpr size_t MAX_RESPONSE = 192 * 1024;

char gHost[80] = {};
char gZone[HOMEY_MAX_AREA] = {};
char gToken[512] = {};
bool gConnected = false;
bool gBusy = false;

ha_client::Device gDevices[ha_client::MAX_DEVICES];
HomeyZone gZones[HOMEY_MAX_ZONES];
HomeyDevice gMapped[HOMEY_MAX_DEVICES];
size_t gDeviceCount = 0;
ha_client::DevicesCallback gCallback;

static_assert(ha_client::MAX_DEVICES == HOMEY_MAX_DEVICES, "device lists must be the same length");
static_assert(ha_client::MAX_ID == HOMEY_MAX_ID, "entity ids must fit both clients");
static_assert(ha_client::MAX_NAME == HOMEY_MAX_NAME, "names must fit both clients");
static_assert(ha_client::MAX_AREA == HOMEY_MAX_AREA, "zone names must fit both clients");

void copyField(char *dest, size_t size, const char *src) {
    if (src == nullptr) {
        dest[0] = '\0';
        return;
    }
    std::strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
}

ha_client::Domain toHouseDomain(HomeyDomain domain) {
    switch (domain) {
        case HomeyDomain::Light:
            return ha_client::Domain::Light;
        case HomeyDomain::Climate:
            return ha_client::Domain::Climate;
        case HomeyDomain::Scene:
            return ha_client::Domain::Scene;
        case HomeyDomain::Other:
            return ha_client::Domain::Other;
    }
    return ha_client::Domain::Other;
}

/** @brief Collect a response body. Caller frees it. Null on any failure. */
char *fetch(const char *path, int timeoutMs) {
    char url[192];
    std::snprintf(url, sizeof(url), "http://%s%s", gHost, path);

    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = timeoutMs;
    config.method = HTTP_METHOD_GET;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return nullptr;
    }

    char auth[560];
    std::snprintf(auth, sizeof(auth), "Bearer %s", gToken);
    esp_http_client_set_header(client, "Authorization", auth);

    char *body = nullptr;
    if (esp_http_client_open(client, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Could not reach Homey at %s", gHost);
        esp_http_client_cleanup(client);
        return nullptr;
    }

    esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE(
            TAG,
            "Homey answered %d for %s%s",
            status,
            path,
            (status == 401) ? " (API key rejected)" : ""
        );
    } else {
        body = static_cast<char *>(heap_caps_malloc(MAX_RESPONSE, MALLOC_CAP_SPIRAM));
        if (body == nullptr) {
            ESP_LOGE(TAG, "No memory for the Homey response");
        } else {
            int total = 0;
            while (total < static_cast<int>(MAX_RESPONSE) - 1) {
                const int read = esp_http_client_read(client, body + total, MAX_RESPONSE - 1 - total);
                if (read <= 0) {
                    break;
                }
                total += read;
            }
            body[total] = '\0';
            if (total >= static_cast<int>(MAX_RESPONSE) - 1) {
                ESP_LOGE(TAG, "Homey response for %s did not fit", path);
                heap_caps_free(body);
                body = nullptr;
            } else {
                ESP_LOGI(TAG, "Read %d bytes from %s", total, path);
            }
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return body;
}

esp_err_t sendJson(esp_http_client_method_t method, const char *path, const char *body) {
    char url[220];
    std::snprintf(url, sizeof(url), "http://%s%s", gHost, path);

    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = COMMAND_TIMEOUT_MS;
    config.method = method;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return ESP_FAIL;
    }

    char auth[560];
    std::snprintf(auth, sizeof(auth), "Bearer %s", gToken);
    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (body != nullptr) {
        esp_http_client_set_post_field(client, body, std::strlen(body));
    }

    const esp_err_t result = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if ((result != ESP_OK) || (status >= 300) || (status <= 0)) {
        ESP_LOGE(TAG, "Homey command %s failed (status %d)", path, status);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/** @brief Homey ids are uuids. Anything else must not reach the URL. */
bool rawIdOk(const char *id) {
    if ((id == nullptr) || (id[0] == '\0') || (std::strlen(id) > 80)) {
        return false;
    }
    for (const char *cursor = id; *cursor != '\0'; cursor++) {
        const unsigned char c = static_cast<unsigned char>(*cursor);
        const bool ok = ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) ||
                        ((c >= '0') && (c <= '9')) || (c == '-') || (c == '_');
        if (!ok) {
            return false;
        }
    }
    return true;
}

/** @brief The id after a known prefix, or null when the prefix is absent or the id is unsafe */
const char *idAfter(const char *entityId, const char *prefix) {
    const size_t length = std::strlen(prefix);
    if ((entityId == nullptr) || (std::strncmp(entityId, prefix, length) != 0)) {
        return nullptr;
    }
    if (!rawIdOk(entityId + length)) {
        return nullptr;
    }
    return entityId + length;
}

void publishMapped(size_t count) {
    gDeviceCount = count;
    for (size_t i = 0; i < count; i++) {
        ha_client::Device &dest = gDevices[i];
        const HomeyDevice &src = gMapped[i];
        dest = {};
        copyField(dest.entityId, sizeof(dest.entityId), src.entityId);
        copyField(dest.name, sizeof(dest.name), src.name);
        copyField(dest.area, sizeof(dest.area), src.area);
        dest.domain = toHouseDomain(src.domain);
        dest.on = src.on;
        dest.brightness = src.brightness;
        dest.temperature = src.temperature;
        dest.currentTemp = src.currentTemp;
        dest.humidity = -1;
        dest.isGroup = false;
        dest.memberCount = src.memberCount;
    }
}

void refreshTask(void *param) {
    (void)param;

    size_t zoneCount = 0;
    char *zonesBody = fetch("/api/manager/zones/zone/", HTTP_TIMEOUT_MS);
    if (zonesBody != nullptr) {
        const int loaded = loadHomeyZones(zonesBody, gZones, HOMEY_MAX_ZONES);
        heap_caps_free(zonesBody);
        if (loaded < 0) {
            ESP_LOGE(TAG, "Could not read Homey zones");
        } else {
            zoneCount = static_cast<size_t>(loaded);
        }
    }

    bool devicesOk = false;
    size_t count = 0;
    char *devicesBody = fetch("/api/manager/devices/device/", HTTP_TIMEOUT_MS);
    if (devicesBody != nullptr) {
        const int mapped = appendHomeyDevices(
            devicesBody,
            gZones,
            zoneCount,
            gZone,
            gMapped,
            0,
            HOMEY_MAX_DEVICES
        );
        heap_caps_free(devicesBody);
        if (mapped < 0) {
            ESP_LOGE(TAG, "Could not read Homey devices");
        } else {
            count = static_cast<size_t>(mapped);
            devicesOk = true;
        }
    }

    if (devicesOk) {
        char *flowsBody = fetch("/api/manager/flow/flow/", HTTP_TIMEOUT_MS);
        if (flowsBody != nullptr) {
            const int mapped = appendHomeyFlows(flowsBody, false, gMapped, count, HOMEY_MAX_DEVICES);
            heap_caps_free(flowsBody);
            if (mapped >= 0) {
                count = static_cast<size_t>(mapped);
            } else {
                ESP_LOGW(TAG, "Could not read Homey flows");
            }
        }

        char *advancedBody = fetch("/api/manager/flow/advancedflow/", HTTP_TIMEOUT_MS);
        if (advancedBody != nullptr) {
            const int mapped = appendHomeyFlows(advancedBody, true, gMapped, count, HOMEY_MAX_DEVICES);
            heap_caps_free(advancedBody);
            if (mapped >= 0) {
                count = static_cast<size_t>(mapped);
            } else {
                ESP_LOGW(TAG, "Could not read Homey advanced flows");
            }
        }

        publishMapped(count);
        gConnected = true;
        ESP_LOGI(
            TAG,
            "Zone '%s': %u devices and flows",
            (gZone[0] != '\0') ? gZone : "all",
            static_cast<unsigned>(count)
        );
    } else {
        gConnected = false;
    }

    ha_client::DevicesCallback callback = std::move(gCallback);
    gBusy = false;
    if (callback) {
        callback(gConnected ? gDevices : nullptr, gConnected ? gDeviceCount : 0);
    }
    vTaskDelete(nullptr);
}

}  // namespace

esp_err_t begin(const char *host, const char *token) {
    if ((host == nullptr) || (token == nullptr) || (host[0] == '\0') || (token[0] == '\0')) {
        ESP_LOGE(TAG, "No host or API key configured, see secrets.hpp");
        return ESP_ERR_INVALID_ARG;
    }
    if (std::strlen(token) >= sizeof(gToken)) {
        ESP_LOGE(TAG, "Homey API key does not fit");
        return ESP_ERR_INVALID_SIZE;
    }
    copyField(gHost, sizeof(gHost), host);
    copyField(gToken, sizeof(gToken), token);
    ESP_LOGI(TAG, "Talking to Homey at %s", gHost);
    return ESP_OK;
}

void setZone(const char *zone) {
    copyField(gZone, sizeof(gZone), (zone != nullptr) ? zone : "");
    ESP_LOGI(TAG, "Showing zone '%s'", (gZone[0] != '\0') ? gZone : "all");
}

const char *zone() {
    return gZone;
}

esp_err_t refresh(ha_client::DevicesCallback callback) {
    if (gHost[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    if (gBusy) {
        ESP_LOGW(TAG, "Previous Homey refresh still running");
        return ESP_ERR_INVALID_STATE;
    }
    gBusy = true;
    gCallback = std::move(callback);
    if (xTaskCreate(refreshTask, "homey_refresh", 12288, nullptr, 3, nullptr) != pdPASS) {
        gBusy = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool owns(const char *entityId) {
    if (entityId == nullptr) {
        return false;
    }
    return (std::strncmp(entityId, "homey:", 6) == 0) ||
           (std::strncmp(entityId, "homey-flow:", 11) == 0) ||
           (std::strncmp(entityId, "homey-aflow:", 12) == 0);
}

esp_err_t setPower(const char *entityId, bool on) {
    const char *id = idAfter(entityId, "homey:");
    if (id == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    char path[160];
    std::snprintf(path, sizeof(path), "/api/manager/devices/device/%s/capability/onoff", id);
    return sendJson(HTTP_METHOD_PUT, path, on ? "{\"value\":true}" : "{\"value\":false}");
}

esp_err_t setBrightness(const char *entityId, int32_t percent) {
    const char *id = idAfter(entityId, "homey:");
    if (id == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    char path[160];
    char body[32];
    std::snprintf(path, sizeof(path), "/api/manager/devices/device/%s/capability/dim", id);
    std::snprintf(body, sizeof(body), "{\"value\":%ld.%02ld}", static_cast<long>(percent / 100), static_cast<long>(percent % 100));
    return sendJson(HTTP_METHOD_PUT, path, body);
}

esp_err_t setTemperature(const char *entityId, int32_t tenths) {
    const char *id = idAfter(entityId, "homey:");
    if (id == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    const long sign = (tenths < 0) ? -1L : 1L;
    long absolute = tenths;
    if (absolute < 0) {
        absolute = -absolute;
    }
    char path[180];
    char body[40];
    std::snprintf(path, sizeof(path), "/api/manager/devices/device/%s/capability/target_temperature", id);
    std::snprintf(
        body,
        sizeof(body),
        "{\"value\":%s%ld.%ld}",
        (sign < 0) ? "-" : "",
        absolute / 10,
        absolute % 10
    );
    return sendJson(HTTP_METHOD_PUT, path, body);
}

esp_err_t triggerFlow(const char *entityId) {
    const char *advanced = idAfter(entityId, "homey-aflow:");
    const char *standard = idAfter(entityId, "homey-flow:");
    const char *id = (advanced != nullptr) ? advanced : standard;
    if (id == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    char path[180];
    if (advanced != nullptr) {
        std::snprintf(path, sizeof(path), "/api/manager/flow/advancedflow/%s/trigger", id);
    } else {
        std::snprintf(path, sizeof(path), "/api/manager/flow/flow/%s/trigger", id);
    }
    return sendJson(HTTP_METHOD_POST, path, "{}");
}

bool isConnected() {
    return gConnected;
}

}  // namespace homey_client
