/**
 * @file wifi_manager.cpp
 * @brief Wi-Fi with on-device setup
 */

#include "wifi_manager.hpp"

#include <cstring>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

constexpr static const char *TAG = "WIFI";

namespace wifi_manager {

namespace {

constexpr const char *NVS_NAMESPACE = "smarteye";
constexpr const char *NVS_KEY_SSID = "wifi_ssid";
constexpr const char *NVS_KEY_PASS = "wifi_pass";

// Give up on stored credentials after this many tries and offer setup instead
constexpr int MAX_CONNECT_ATTEMPTS = 5;

constexpr size_t SSID_MAX = 32;
constexpr size_t PASS_MAX = 64;

StateCallback gCallback;
State gState = State::Idle;
char gIpAddress[16] = "";
int gConnectAttempts = 0;
httpd_handle_t gServer = nullptr;
bool gProvisioning = false;

void setState(State state, const char *detail) {
    gState = state;
    if (gCallback) {
        gCallback(state, (detail != nullptr) ? detail : "");
    }
}

// ---------------------------------------------------------------- storage --

esp_err_t loadCredentials(char *ssid, size_t ssidLen, char *password, size_t passLen) {
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    size_t length = ssidLen;
    ret = nvs_get_str(handle, NVS_KEY_SSID, ssid, &length);
    if (ret == ESP_OK) {
        length = passLen;
        ret = nvs_get_str(handle, NVS_KEY_PASS, password, &length);
        // A network without a password is perfectly normal
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            password[0] = '\0';
            ret = ESP_OK;
        }
    }

    nvs_close(handle);
    return ret;
}

esp_err_t saveCredentials(const char *ssid, const char *password) {
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_str(handle, NVS_KEY_SSID, ssid);
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, NVS_KEY_PASS, password);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);
    return ret;
}

// ------------------------------------------------------------- setup page --

/**
 * @brief Percent-decoding for what the form sends back
 * @details Passwords are full of characters that have to travel encoded, so
 *          this has to run before anything is stored.
 */
void urlDecode(const char *in, char *out, size_t outSize) {
    size_t written = 0;
    for (size_t i = 0; (in[i] != '\0') && (written + 1 < outSize); i++) {
        if (in[i] == '+') {
            out[written++] = ' ';
        } else if ((in[i] == '%') && (in[i + 1] != '\0') && (in[i + 2] != '\0')) {
            const char hex[3] = {in[i + 1], in[i + 2], '\0'};
            out[written++] = static_cast<char>(strtol(hex, nullptr, 16));
            i += 2;
        } else {
            out[written++] = in[i];
        }
    }
    out[written] = '\0';
}

esp_err_t rootHandler(httpd_req_t *request) {
    static const char *PAGE_HEAD =
        "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>SmartEye</title><style>"
        "body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:24px}"
        "h1{font-size:20px;margin:0 0 4px}p{color:#999;margin:0 0 20px;font-size:14px}"
        "label{display:block;margin:14px 0 6px;font-size:14px}"
        "select,input{width:100%;box-sizing:border-box;padding:12px;border-radius:8px;"
        "border:1px solid #444;background:#1c1c1c;color:#eee;font-size:16px}"
        "button{width:100%;margin-top:22px;padding:14px;border:0;border-radius:8px;"
        "background:#e0189b;color:#fff;font-size:16px;font-weight:600}"
        "</style><h1>SmartEye</h1><p>Kies je wifi-netwerk</p>"
        "<form method=POST action=/save><label>Netwerk</label><select name=ssid>";

    static const char *PAGE_TAIL =
        "</select>"
        "<label>Of typ de naam zelf (bij een verborgen netwerk)</label>"
        "<input name=manual placeholder='netwerknaam' autocomplete=off autocapitalize=off>"
        "<label>Wachtwoord</label>"
        "<input name=pass type=password autocomplete=off>"
        "<button type=submit>Verbinden</button></form>"
        "<p style='margin-top:18px;font-size:13px'>Alleen 2,4 GHz-netwerken "
        "verschijnen hier; dit apparaat heeft geen 5 GHz.</p>";

    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(request, PAGE_HEAD);

    // Scan from AP mode so the list shows what is actually in range here.
    // The dwell time is set explicitly: too short and networks that answer
    // slowly simply never show up.
    uint16_t found = 0;
    wifi_scan_config_t scanConfig = {};
    scanConfig.show_hidden = true;
    scanConfig.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    scanConfig.scan_time.active.min = 120;
    scanConfig.scan_time.active.max = 400;

    if (esp_wifi_scan_start(&scanConfig, true) == ESP_OK) {
        esp_wifi_scan_get_ap_num(&found);
    }
    ESP_LOGI(TAG, "Scan found %u networks (2.4GHz only, this radio has no 5GHz)", found);

    constexpr uint16_t MAX_LISTED = 20;
    if (found > MAX_LISTED) {
        found = MAX_LISTED;
    }

    if (found > 0) {
        auto *records = static_cast<wifi_ap_record_t *>(
            calloc(found, sizeof(wifi_ap_record_t))
        );
        if (records != nullptr) {
            if (esp_wifi_scan_get_ap_records(&found, records) == ESP_OK) {
                for (uint16_t i = 0; i < found; i++) {
                    const char *name = reinterpret_cast<const char *>(records[i].ssid);

                    ESP_LOGI(
                        TAG,
                        "  %-32s ch%-3d %d dBm",
                        (name[0] != '\0') ? name : "<hidden>",
                        records[i].primary,
                        records[i].rssi
                    );

                    if (name[0] == '\0') {
                        continue;
                    }
                    char option[128];
                    snprintf(
                        option,
                        sizeof(option),
                        "<option value=\"%s\">%s (%d dBm)</option>",
                        name,
                        name,
                        records[i].rssi
                    );
                    httpd_resp_sendstr_chunk(request, option);
                }
            }
            free(records);
        }
    }

    httpd_resp_sendstr_chunk(request, PAGE_TAIL);
    httpd_resp_sendstr_chunk(request, nullptr);
    return ESP_OK;
}

esp_err_t saveHandler(httpd_req_t *request) {
    char body[256] = {};
    const int toRead = (request->content_len < static_cast<int>(sizeof(body) - 1))
                           ? static_cast<int>(request->content_len)
                           : static_cast<int>(sizeof(body) - 1);

    const int received = httpd_req_recv(request, body, toRead);
    if (received <= 0) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Leeg formulier");
        return ESP_FAIL;
    }
    body[received] = '\0';

    char rawSsid[SSID_MAX * 3] = {};
    char rawPass[PASS_MAX * 3] = {};
    char ssid[SSID_MAX + 1] = {};
    char password[PASS_MAX + 1] = {};

    // A typed-in name wins over the picked one, so a hidden network can be
    // reached even though it never appears in the list.
    char rawManual[SSID_MAX * 3] = {};
    const bool hasManual =
        (httpd_query_key_value(body, "manual", rawManual, sizeof(rawManual)) == ESP_OK) &&
        (rawManual[0] != '\0');

    if (hasManual) {
        std::strncpy(rawSsid, rawManual, sizeof(rawSsid) - 1);
    } else if (httpd_query_key_value(body, "ssid", rawSsid, sizeof(rawSsid)) != ESP_OK) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Geen netwerk gekozen");
        return ESP_FAIL;
    }
    httpd_query_key_value(body, "pass", rawPass, sizeof(rawPass));

    urlDecode(rawSsid, ssid, sizeof(ssid));
    urlDecode(rawPass, password, sizeof(password));

    if (saveCredentials(ssid, password) != ESP_OK) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Opslaan mislukt");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Credentials stored for '%s', restarting to connect", ssid);

    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_sendstr(
        request,
        "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<style>body{font-family:system-ui,sans-serif;background:#111;color:#eee;padding:24px}"
        "</style><h1>Opgeslagen</h1>"
        "<p>SmartEye verbindt nu met je netwerk. Dit setup-netwerk verdwijnt.</p>"
    );

    // Let the reply reach the phone before the radio is torn down
    vTaskDelay(pdMS_TO_TICKS(600));
    esp_restart();

    return ESP_OK;
}

/** @brief Anything else also gets the setup page, so any URL works */
esp_err_t catchAllHandler(httpd_req_t *request) {
    return rootHandler(request);
}

void startWebServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.lru_purge_enable = true;

    if (httpd_start(&gServer, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Could not start setup web server");
        return;
    }

    httpd_uri_t root = {};
    root.uri = "/";
    root.method = HTTP_GET;
    root.handler = rootHandler;
    httpd_register_uri_handler(gServer, &root);

    httpd_uri_t save = {};
    save.uri = "/save";
    save.method = HTTP_POST;
    save.handler = saveHandler;
    httpd_register_uri_handler(gServer, &save);

    httpd_uri_t catchAll = {};
    catchAll.uri = "/*";
    catchAll.method = HTTP_GET;
    catchAll.handler = catchAllHandler;
    httpd_register_uri_handler(gServer, &catchAll);
}

// ------------------------------------------------------------ radio modes --

void startProvisioningMode() {
    if (gProvisioning) {
        return;
    }
    gProvisioning = true;

    ESP_LOGI(TAG, "Opening setup network '%s'", SETUP_AP_NAME);

    esp_wifi_disconnect();

    // AP plus STA: the AP carries the setup page, the STA half is what makes
    // scanning for nearby networks possible while it is up.
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    wifi_config_t apConfig = {};
    std::strncpy(
        reinterpret_cast<char *>(apConfig.ap.ssid),
        SETUP_AP_NAME,
        sizeof(apConfig.ap.ssid) - 1
    );
    apConfig.ap.ssid_len = std::strlen(SETUP_AP_NAME);
    apConfig.ap.channel = 1;
    apConfig.ap.max_connection = 4;
    apConfig.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &apConfig));

    startWebServer();
    setState(State::Provisioning, SETUP_AP_NAME);
}

void connectWithStoredCredentials() {
    char ssid[SSID_MAX + 1] = {};
    char password[PASS_MAX + 1] = {};

    if (loadCredentials(ssid, sizeof(ssid), password, sizeof(password)) != ESP_OK) {
        ESP_LOGI(TAG, "No stored credentials");
        startProvisioningMode();
        return;
    }

    ESP_LOGI(TAG, "Connecting to '%s'", ssid);

    wifi_config_t staConfig = {};
    std::strncpy(
        reinterpret_cast<char *>(staConfig.sta.ssid),
        ssid,
        sizeof(staConfig.sta.ssid) - 1
    );
    std::strncpy(
        reinterpret_cast<char *>(staConfig.sta.password),
        password,
        sizeof(staConfig.sta.password) - 1
    );

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &staConfig));

    gConnectAttempts = 0;
    setState(State::Connecting, ssid);
    esp_wifi_connect();
}

// ----------------------------------------------------------------- events --

void eventHandler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (gProvisioning) {
            return;
        }

        gConnectAttempts++;
        if (gConnectAttempts < MAX_CONNECT_ATTEMPTS) {
            ESP_LOGW(TAG, "Connect failed, retry %d/%d", gConnectAttempts, MAX_CONNECT_ATTEMPTS);
            esp_wifi_connect();
            return;
        }

        // The stored network is gone or the password changed: rather than
        // retrying forever, offer setup so it can be corrected on the spot.
        ESP_LOGW(TAG, "Giving up on stored network, offering setup");
        startProvisioningMode();
        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto *event = static_cast<ip_event_got_ip_t *>(data);
        snprintf(gIpAddress, sizeof(gIpAddress), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Connected, IP %s", gIpAddress);
        gConnectAttempts = 0;
        setState(State::Connected, gIpAddress);
    }
}

}  // namespace

esp_err_t start(StateCallback callback) {
    gCallback = std::move(callback);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t initConfig = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&initConfig));

    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, eventHandler, nullptr, nullptr)
    );
    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, eventHandler, nullptr, nullptr)
    );

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    connectWithStoredCredentials();
    return ESP_OK;
}

bool hasStoredCredentials() {
    char ssid[SSID_MAX + 1] = {};
    char password[PASS_MAX + 1] = {};
    return loadCredentials(ssid, sizeof(ssid), password, sizeof(password)) == ESP_OK;
}

esp_err_t forgetCredentials() {
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    nvs_erase_key(handle, NVS_KEY_SSID);
    nvs_erase_key(handle, NVS_KEY_PASS);
    ret = nvs_commit(handle);
    nvs_close(handle);
    return ret;
}

State currentState() {
    return gState;
}

const char *ipAddress() {
    return gIpAddress;
}

}  // namespace wifi_manager
