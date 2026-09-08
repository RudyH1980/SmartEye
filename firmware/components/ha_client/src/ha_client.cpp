/**
 * @file ha_client.cpp
 * @brief Reads devices from Home Assistant and sends commands back
 */

#include "ha_client.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

constexpr static const char *TAG = "HA";

namespace ha_client {

namespace {

constexpr int HTTP_TIMEOUT_MS = 8000;

// A house full of entities easily runs past 100kB of JSON, so the response is
// collected in PSRAM rather than on a task stack.
constexpr size_t MAX_RESPONSE = 192 * 1024;

char gHost[64] = {};
char gRoom[MAX_AREA] = {};
char gToken[256] = {};
bool gConnected = false;

Device gDevices[MAX_DEVICES];
size_t gDeviceCount = 0;
DevicesCallback gCallback;

void copyField(char *dest, size_t size, const char *src) {
    if (src == nullptr) {
        dest[0] = '\0';
        return;
    }
    std::strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
}

Domain domainFromId(const char *entityId) {
    if (std::strncmp(entityId, "light.", 6) == 0) {
        return Domain::Light;
    }
    if (std::strncmp(entityId, "climate.", 8) == 0) {
        return Domain::Climate;
    }
    if (std::strncmp(entityId, "media_player.", 13) == 0) {
        return Domain::MediaPlayer;
    }
    if (std::strncmp(entityId, "scene.", 6) == 0) {
        return Domain::Scene;
    }
    if (std::strncmp(entityId, "sensor.", 7) == 0) {
        return Domain::Sensor;
    }
    return Domain::Other;
}

/** @brief Read a number out of an attributes object, or a fallback */
double attrNumber(const cJSON *attrs, const char *key, double fallback) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(attrs, key);
    return cJSON_IsNumber(item) ? item->valuedouble : fallback;
}

/** @brief Collect a whole response body; returns nullptr on failure */
char *fetch(const char *path) {
    char url[128];
    std::snprintf(url, sizeof(url), "http://%s:8123%s", gHost, path);

    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = HTTP_TIMEOUT_MS;
    config.method = HTTP_METHOD_GET;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return nullptr;
    }

    char auth[300];
    std::snprintf(auth, sizeof(auth), "Bearer %s", gToken);
    esp_http_client_set_header(client, "Authorization", auth);

    char *body = nullptr;

    if (esp_http_client_open(client, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Could not reach Home Assistant at %s", gHost);
        esp_http_client_cleanup(client);
        return nullptr;
    }

    esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);

    if (status != 200) {
        ESP_LOGE(
            TAG,
            "Home Assistant answered %d%s",
            status,
            (status == 401) ? " (token rejected)" : ""
        );
    } else {
        body = static_cast<char *>(heap_caps_malloc(MAX_RESPONSE, MALLOC_CAP_SPIRAM));
        if (body == nullptr) {
            ESP_LOGE(TAG, "No memory for the response");
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
            ESP_LOGI(TAG, "Read %d bytes from %s", total, path);
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return body;
}

/** @brief Turn the /api/states payload into our own compact list */
void parseStates(const char *json) {
    cJSON *root = cJSON_Parse(json);
    if (root == nullptr || !cJSON_IsArray(root)) {
        ESP_LOGE(TAG, "Could not read the state list");
        cJSON_Delete(root);
        return;
    }

    const cJSON *entity = nullptr;
    cJSON_ArrayForEach(entity, root) {
        if (gDeviceCount >= MAX_DEVICES) {
            break;
        }

        const cJSON *idItem = cJSON_GetObjectItemCaseSensitive(entity, "entity_id");
        if (!cJSON_IsString(idItem)) {
            continue;
        }

        const Domain domain = domainFromId(idItem->valuestring);
        // Lights were already collected per room by the template call
        // Lights and scenes were already collected per room
        if ((domain == Domain::Other) || (domain == Domain::Sensor) ||
            (domain == Domain::Light) || (domain == Domain::Scene)) {
            continue;
        }

        Device &device = gDevices[gDeviceCount];
        device = {};
        copyField(device.entityId, MAX_ID, idItem->valuestring);
        device.domain = domain;
        device.brightness = -1;
        device.memberCount = 1;
        device.humidity = -1;
        device.temperature = -1;
        device.currentTemp = -1;

        const cJSON *stateItem = cJSON_GetObjectItemCaseSensitive(entity, "state");
        if (cJSON_IsString(stateItem)) {
            device.on = (std::strcmp(stateItem->valuestring, "on") == 0) ||
                        (std::strcmp(stateItem->valuestring, "playing") == 0) ||
                        (std::strcmp(stateItem->valuestring, "heat") == 0) ||
                        (std::strcmp(stateItem->valuestring, "cool") == 0) ||
                        (std::strcmp(stateItem->valuestring, "auto") == 0);
        }

        const cJSON *attrs = cJSON_GetObjectItemCaseSensitive(entity, "attributes");
        if (attrs != nullptr) {
            const cJSON *nameItem = cJSON_GetObjectItemCaseSensitive(attrs, "friendly_name");
            copyField(device.name, MAX_NAME, cJSON_IsString(nameItem) ? nameItem->valuestring : device.entityId);

            // Home Assistant reports brightness as 0-255
            const double raw = attrNumber(attrs, "brightness", -1);
            if (raw >= 0) {
                device.brightness = static_cast<int32_t>((raw * 100.0) / 255.0);
            }

            const double target = attrNumber(attrs, "temperature", -1000);
            if (target > -1000) {
                device.temperature = static_cast<int32_t>(target * 10.0);
            }

            const double current = attrNumber(attrs, "current_temperature", -1000);
            if (current > -1000) {
                device.currentTemp = static_cast<int32_t>(current * 10.0);
            }

            const double humidity = attrNumber(attrs, "current_humidity", -1);
            if (humidity >= 0) {
                device.humidity = static_cast<int32_t>(humidity);
            }
        } else {
            copyField(device.name, MAX_NAME, device.entityId);
        }

        gDeviceCount++;
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Found %u usable devices", static_cast<unsigned>(gDeviceCount));
}


/**
 * @brief POST a body and hand back the reply
 * @details Used for the template endpoint, which is the only way to learn
 *          which area an entity belongs to without opening a websocket.
 */
char *postForBody(const char *path, const char *payload, size_t limit) {
    char url[128];
    std::snprintf(url, sizeof(url), "http://%s:8123%s", gHost, path);

    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = HTTP_TIMEOUT_MS;
    config.method = HTTP_METHOD_POST;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return nullptr;
    }

    char auth[300];
    std::snprintf(auth, sizeof(auth), "Bearer %s", gToken);
    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    char *body = nullptr;

    if (esp_http_client_open(client, std::strlen(payload)) != ESP_OK) {
        esp_http_client_cleanup(client);
        return nullptr;
    }

    esp_http_client_write(client, payload, std::strlen(payload));
    esp_http_client_fetch_headers(client);

    if (esp_http_client_get_status_code(client) == 200) {
        body = static_cast<char *>(heap_caps_malloc(limit, MALLOC_CAP_SPIRAM));
        if (body != nullptr) {
            int total = 0;
            while (total < static_cast<int>(limit) - 1) {
                const int read = esp_http_client_read(client, body + total, limit - 1 - total);
                if (read <= 0) {
                    break;
                }
                total += read;
            }
            body[total] = '\0';
        }
    } else {
        ESP_LOGE(TAG, "Template call failed (%d)", esp_http_client_get_status_code(client));
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return body;
}

/**
 * @brief Ask Home Assistant for this room's lights, one per line
 * @details A template is far lighter than the whole state list and, unlike
 *          it, can answer which area something is in and whether a light is
 *          really a group of other lights.
 */
void fetchRoomLights() {
    char payload[640];
    std::snprintf(
        payload,
        sizeof(payload),
        "{\"template\":\""
        "{%% for e in states.light %%}"
        "{%% if not '%s' or area_name(e.entity_id) == '%s' %%}"
        "{{ e.entity_id }}|{{ e.name }}|{{ area_name(e.entity_id) or '' }}|{{ e.state }}"
        "|{{ e.attributes.brightness | default(-1) }}"
        "|{{ e.attributes.entity_id | count if e.attributes.entity_id is defined else 1 }}\\n"
        "{%% endif %%}{%% endfor %%}\"}",
        gRoom,
        gRoom
    );

    char *body = postForBody("/api/template", payload, 8192);
    if (body == nullptr) {
        return;
    }

    gDeviceCount = 0;

    char *line = std::strtok(body, "\n");
    while ((line != nullptr) && (gDeviceCount < MAX_DEVICES)) {
        // entity_id | name | area | state | brightness | members
        char *fields[6] = {};
        int found = 0;
        char *cursor = line;
        fields[found++] = cursor;
        while ((found < 6) && ((cursor = std::strchr(cursor, '|')) != nullptr)) {
            *cursor = '\0';
            cursor++;
            fields[found++] = cursor;
        }

        if (found == 6) {
            Device &device = gDevices[gDeviceCount];
            device = {};
            copyField(device.entityId, MAX_ID, fields[0]);
            copyField(device.name, MAX_NAME, fields[1]);
            copyField(device.area, MAX_AREA, fields[2]);
            device.domain = Domain::Light;
            device.on = (std::strcmp(fields[3], "on") == 0);

            const int rawBrightness = std::atoi(fields[4]);
            device.brightness = (rawBrightness >= 0) ? ((rawBrightness * 100) / 255) : -1;

            device.memberCount = std::atoi(fields[5]);
            if (device.memberCount < 1) {
                device.memberCount = 1;
            }
            device.isGroup = device.memberCount > 1;

            device.temperature = -1;
            device.currentTemp = -1;
            device.humidity = -1;
            gDeviceCount++;
        }

        line = std::strtok(nullptr, "\n");
    }

    heap_caps_free(body);
    ESP_LOGI(
        TAG,
        "Room '%s': %u lights",
        (gRoom[0] != 0) ? gRoom : "all",
        static_cast<unsigned>(gDeviceCount)
    );
}


/**
 * @brief Fetch this room's scenes, which are what the moods screen shows
 * @details A mood in the original is a scene here, so they are made and named
 *          in Home Assistant rather than on the device.
 */
void fetchRoomScenes() {
    char payload[560];
    std::snprintf(
        payload,
        sizeof(payload),
        "{\"template\":\""
        "{%% for e in states.scene %%}"
        "{%% if not '%s' or area_name(e.entity_id) == '%s' %%}"
        "{{ e.entity_id }}|{{ e.name }}|{{ area_name(e.entity_id) or '' }}\\n"
        "{%% endif %%}{%% endfor %%}\"}",
        gRoom,
        gRoom
    );

    char *body = postForBody("/api/template", payload, 4096);
    if (body == nullptr) {
        return;
    }

    char *line = std::strtok(body, "\n");
    while ((line != nullptr) && (gDeviceCount < MAX_DEVICES)) {
        char *name = std::strchr(line, '|');
        if (name != nullptr) {
            *name = '\0';
            name++;

            char *area = std::strchr(name, '|');
            if (area != nullptr) {
                *area = '\0';
                area++;
            }

            Device &device = gDevices[gDeviceCount];
            device = {};
            copyField(device.entityId, MAX_ID, line);
            copyField(device.name, MAX_NAME, name);
            copyField(device.area, MAX_AREA, (area != nullptr) ? area : "");
            device.domain = Domain::Scene;
            device.brightness = -1;
            device.temperature = -1;
            device.currentTemp = -1;
            device.humidity = -1;
            device.memberCount = 1;
            gDeviceCount++;
        }
        line = std::strtok(nullptr, "\n");
    }

    heap_caps_free(body);
    ESP_LOGI(TAG, "Scenes loaded for '%s'", (gRoom[0] != 0) ? gRoom : "all");
}

void refreshTask(void *param) {
    (void)param;

    // Lights and scenes come from template calls, which know about areas
    fetchRoomLights();
    fetchRoomScenes();

    // Everything else still comes from the plain state list
    char *json = fetch("/api/states");
    if (json != nullptr) {
        parseStates(json);
        heap_caps_free(json);
        gConnected = true;
    } else {
        gConnected = false;
    }

    if (gCallback) {
        gCallback(gConnected ? gDevices : nullptr, gConnected ? gDeviceCount : 0);
    }

    vTaskDelete(nullptr);
}

/** @brief POST to a service endpoint with a small JSON body */
esp_err_t callService(const char *domain, const char *service, const char *body) {
    char url[128];
    std::snprintf(url, sizeof(url), "http://%s:8123/api/services/%s/%s", gHost, domain, service);

    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = HTTP_TIMEOUT_MS;
    config.method = HTTP_METHOD_POST;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return ESP_FAIL;
    }

    char auth[300];
    std::snprintf(auth, sizeof(auth), "Bearer %s", gToken);
    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, std::strlen(body));

    const esp_err_t ret = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (ret != ESP_OK || status >= 300) {
        ESP_LOGE(TAG, "%s.%s failed (status %d)", domain, service, status);
        return ESP_FAIL;
    }

    return ESP_OK;
}

}  // namespace

esp_err_t begin(const char *host, const char *token) {
    if (host == nullptr || token == nullptr || host[0] == '\0' || token[0] == '\0') {
        ESP_LOGE(TAG, "No host or token configured, see secrets.hpp");
        return ESP_ERR_INVALID_ARG;
    }

    copyField(gHost, sizeof(gHost), host);
    copyField(gToken, sizeof(gToken), token);
    ESP_LOGI(TAG, "Talking to Home Assistant at %s", gHost);
    return ESP_OK;
}

esp_err_t refresh(DevicesCallback callback) {
    if (gHost[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    gCallback = std::move(callback);

    // Its own task: fetching and parsing a large payload must not stall the UI
    if (xTaskCreate(refreshTask, "ha_refresh", 8192, nullptr, 3, nullptr) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t setPower(const char *entityId, bool on) {
    char body[128];
    std::snprintf(body, sizeof(body), "{\"entity_id\":\"%s\"}", entityId);

    const char *domain = (std::strncmp(entityId, "light.", 6) == 0) ? "light" : "homeassistant";
    return callService(domain, on ? "turn_on" : "turn_off", body);
}

esp_err_t setBrightness(const char *entityId, int32_t percent) {
    char body[160];
    std::snprintf(
        body,
        sizeof(body),
        "{\"entity_id\":\"%s\",\"brightness_pct\":%ld}",
        entityId,
        static_cast<long>(percent)
    );
    return callService("light", "turn_on", body);
}

esp_err_t activateScene(const char *entityId) {
    char body[128];
    std::snprintf(body, sizeof(body), "{\"entity_id\":\"%s\"}", entityId);
    return callService("scene", "turn_on", body);
}

esp_err_t setTemperature(const char *entityId, int32_t tenths) {
    char body[160];
    std::snprintf(
        body,
        sizeof(body),
        "{\"entity_id\":\"%s\",\"temperature\":%ld.%ld}",
        entityId,
        static_cast<long>(tenths / 10),
        static_cast<long>(tenths % 10)
    );
    return callService("climate", "set_temperature", body);
}


void setRoom(const char *area) {
    copyField(gRoom, sizeof(gRoom), (area != nullptr) ? area : "");
    ESP_LOGI(TAG, "Showing room '%s'", (gRoom[0] != 0) ? gRoom : "all");
}

const char *room() {
    return gRoom;
}

esp_err_t readHelper(const char *entityId, char *out, size_t size) {
    if ((gHost[0] == 0) || (entityId == nullptr)) {
        return ESP_ERR_INVALID_STATE;
    }

    char payload[192];
    std::snprintf(payload, sizeof(payload), "{\"template\":\"{{ states('%s') }}\"}", entityId);

    char *body = postForBody("/api/template", payload, 256);
    if (body == nullptr) {
        return ESP_FAIL;
    }

    // Home Assistant answers "unknown" for a helper that does not exist
    if ((std::strcmp(body, "unknown") == 0) || (std::strcmp(body, "unavailable") == 0)) {
        heap_caps_free(body);
        return ESP_ERR_NOT_FOUND;
    }

    copyField(out, size, body);
    heap_caps_free(body);
    return ESP_OK;
}

bool isConnected() {
    return gConnected;
}

}  // namespace ha_client
