/**
 * @file house_setup_test.cpp
 * @brief Checks the install-form rules for Home Assistant and Homey
 */

#include "house_setup.hpp"

#include <cstdio>
#include <cstring>

namespace {

int gFailures = 0;

void expect(bool ok, const char *message) {
    if (!ok) {
        std::printf("FAIL %s\n", message);
        gFailures++;
    }
}

}  // namespace

int main() {
    HouseChoice out = {};
    char error[80] = {};

    expect(
        acceptHouseChoice("ha", "http://192.168.1.20:8123/lovelace", "token-ha", "", nullptr, &out, error, sizeof(error)),
        "home assistant with scheme and port"
    );
    expect(out.kind == HouseKind::HomeAssistant, "kind is home assistant");
    expect(std::strcmp(out.host, "192.168.1.20") == 0, "host lost the scheme, port and path");
    expect(std::strcmp(out.token, "token-ha") == 0, "token kept");
    expect(out.zone[0] == '\0', "home assistant stores no zone");

    char longPath[240] = {};
    std::strcpy(longPath, "HTTP://192.168.1.20:8123/");
    const size_t prefix = std::strlen(longPath);
    std::memset(longPath + prefix, 'x', 160);
    longPath[prefix + 160] = '\0';
    expect(
        acceptHouseChoice("ha", longPath, "token-ha", "Woonkamer", nullptr, &out, error, sizeof(error)),
        "long path still leaves the host"
    );
    expect(std::strcmp(out.host, "192.168.1.20") == 0, "path and port dropped");
    expect(out.zone[0] == '\0', "home assistant drops a zone");

    expect(
        acceptHouseChoice("homey", "https://192.168.1.50/", "token-homey", " Woonkamer ", nullptr, &out, error, sizeof(error)),
        "homey with zone"
    );
    expect(out.kind == HouseKind::Homey, "kind is homey");
    expect(std::strcmp(out.host, "192.168.1.50") == 0, "homey host");
    expect(std::strcmp(out.zone, "Woonkamer") == 0, "zone trimmed");

    HouseChoice previous = out;
    expect(
        acceptHouseChoice("homey", "192.168.1.50", "  ", "Keuken", &previous, &out, error, sizeof(error)),
        "empty token keeps the previous key"
    );
    expect(std::strcmp(out.token, "token-homey") == 0, "previous token reused");
    expect(std::strcmp(out.zone, "Keuken") == 0, "zone can change without a new key");

    expect(
        !acceptHouseChoice("homey", "192.168.1.51", "", "Keuken", &previous, &out, error, sizeof(error)),
        "a new address needs a new key"
    );
    expect(std::strcmp(error, "Vul de toegangssleutel in") == 0, "missing key on a new address");

    expect(
        !acceptHouseChoice("ha", "192.168.1.20", "", "", &previous, &out, error, sizeof(error)),
        "switching system requires a new key"
    );
    expect(std::strcmp(error, "Vul de toegangssleutel in") == 0, "missing key is explained");

    expect(!acceptHouseChoice("mqtt", "192.168.1.20", "x", "", nullptr, &out, error, sizeof(error)), "unknown system rejected");
    expect(!acceptHouseChoice("ha", "   ", "x", "", nullptr, &out, error, sizeof(error)), "empty host rejected");
    expect(!acceptHouseChoice("ha", "192.168.1.20 huis", "x", "", nullptr, &out, error, sizeof(error)), "space in host rejected");

    char longToken[HOUSE_TOKEN_MAX + 8];
    std::memset(longToken, 'a', sizeof(longToken) - 1);
    longToken[sizeof(longToken) - 1] = '\0';
    expect(
        !acceptHouseChoice("ha", "192.168.1.20", longToken, "", nullptr, &out, error, sizeof(error)),
        "token past the buffer is rejected"
    );
    expect(std::strcmp(error, "Toegangssleutel is te lang") == 0, "long key is explained");

    char longHost[80];
    std::memset(longHost, 'a', 64);
    longHost[64] = '\0';
    expect(!acceptHouseChoice("ha", longHost, "x", "", nullptr, &out, error, sizeof(error)), "long host rejected");
    expect(std::strcmp(error, "Adres is te lang") == 0, "long host is explained");

    char longZone[40];
    std::memset(longZone, 'z', 32);
    longZone[32] = '\0';
    expect(
        !acceptHouseChoice("homey", "192.168.1.50", "x", longZone, nullptr, &out, error, sizeof(error)),
        "long zone rejected"
    );
    expect(std::strcmp(error, "Zonenaam is te lang") == 0, "long zone is explained");
    expect(out.kind == HouseKind::None, "a rejected zone stores nothing");

    if (gFailures == 0) {
        std::printf("ok\n");
        return 0;
    }
    std::printf("%d failed\n", gFailures);
    return 1;
}
