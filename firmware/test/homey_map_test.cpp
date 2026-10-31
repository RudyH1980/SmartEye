/**
 * @file homey_map_test.cpp
 * @brief Checks the Homey JSON mapping without a board or a Homey
 */

#include "homey_map.hpp"

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

const HomeyDevice *findId(const HomeyDevice *devices, int count, const char *id) {
    for (int i = 0; i < count; i++) {
        if (std::strcmp(devices[i].entityId, id) == 0) {
            return &devices[i];
        }
    }
    return nullptr;
}

}  // namespace

int main() {
    const char *zones =
        "{"
        "\"z1\":{\"id\":\"z1\",\"name\":\"Woonkamer\"},"
        "\"z2\":{\"id\":\"z2\",\"name\":\"Keuken\"}"
        "}";

    const char *devices =
        "{"
        "\"dev-lamp\":{"
        "\"name\":\"Hanglamp\",\"zone\":\"z1\",\"class\":\"light\",\"available\":true,"
        "\"capabilities\":[\"onoff\",\"dim\"],"
        "\"capabilitiesObj\":{\"onoff\":{\"value\":true},\"dim\":{\"value\":0.4}}"
        "},"
        "\"dev-keuken\":{"
        "\"name\":\"Keukenlamp\",\"zone\":\"z2\",\"class\":\"light\","
        "\"capabilities\":[\"onoff\"],"
        "\"capabilitiesObj\":{\"onoff\":{\"value\":false}}"
        "},"
        "\"dev-thermo\":{"
        "\"name\":\"Thermostaat\",\"zone\":\"z1\",\"class\":\"thermostat\","
        "\"capabilities\":[\"target_temperature\",\"measure_temperature\"],"
        "\"capabilitiesObj\":{"
        "\"target_temperature\":{\"value\":21.5},"
        "\"measure_temperature\":{\"value\":19.4}"
        "}"
        "},"
        "\"dev-lock\":{"
        "\"name\":\"Slot\",\"zone\":\"z1\",\"class\":\"lock\","
        "\"capabilities\":[\"locked\"],\"capabilitiesObj\":{}"
        "},"
        "\"dev-offline\":{"
        "\"name\":\"Uit\",\"zone\":\"z1\",\"class\":\"light\",\"available\":false,"
        "\"capabilities\":[\"onoff\"],\"capabilitiesObj\":{\"onoff\":{\"value\":true}}"
        "}"
        "}";

    const char *flows =
        "{"
        "\"f1\":{\"name\":\"Film\",\"enabled\":true,"
        "\"trigger\":{\"uri\":\"homey:manager:flow\",\"id\":\"trigger\"}},"
        "\"f2\":{\"name\":\"Beweging\",\"enabled\":true,"
        "\"trigger\":{\"uri\":\"homey:manager:presence\",\"id\":\"entered\"}},"
        "\"f3\":{\"name\":\"Uit\",\"enabled\":false,"
        "\"trigger\":{\"uri\":\"homey:manager:flow\",\"id\":\"trigger\"}}"
        "}";

    const char *advanced =
        "{"
        "\"a1\":{\"name\":\"Avond\",\"enabled\":true,\"triggerable\":true},"
        "\"a2\":{\"name\":\"Niet\",\"enabled\":true,\"triggerable\":false}"
        "}";

    HomeyZone zoneList[8] = {};
    const int zoneCount = loadHomeyZones(zones, zoneList, 8);
    expect(zoneCount == 2, "two zones");

    HomeyDevice list[16] = {};
    int count = appendHomeyDevices(devices, zoneList, static_cast<size_t>(zoneCount), "Woonkamer", list, 0, 16);
    expect(count == 2, "woonkamer keeps the lamp and the thermostat");
    count = appendHomeyFlows(flows, false, list, static_cast<size_t>(count), 16);
    count = appendHomeyFlows(advanced, true, list, static_cast<size_t>(count), 16);
    expect(count == 4, "two devices plus Film and Avond");

    const HomeyDevice *lamp = findId(list, count, "homey:dev-lamp");
    expect(lamp != nullptr, "hanglamp present");
    if (lamp != nullptr) {
        expect(lamp->domain == HomeyDomain::Light, "hanglamp is a light");
        expect(lamp->on, "hanglamp is on");
        expect(lamp->brightness == 40, "dim 0.4 is 40 percent");
        expect(std::strcmp(lamp->area, "Woonkamer") == 0, "hanglamp zone name");
        expect(std::strcmp(lamp->name, "Hanglamp") == 0, "hanglamp name");
    }

    expect(findId(list, count, "homey:dev-keuken") == nullptr, "keuken lamp filtered out");
    expect(findId(list, count, "homey:dev-lock") == nullptr, "lock is not a light");
    expect(findId(list, count, "homey:dev-offline") == nullptr, "unavailable light skipped");

    const HomeyDevice *thermo = findId(list, count, "homey:dev-thermo");
    expect(thermo != nullptr, "thermostat present");
    if (thermo != nullptr) {
        expect(thermo->domain == HomeyDomain::Climate, "thermostat domain");
        expect(thermo->temperature == 215, "target 21.5C is 215 tenths");
        expect(thermo->currentTemp == 194, "measured 19.4C is 194 tenths");
    }

    const HomeyDevice *film = findId(list, count, "homey-flow:f1");
    expect(film != nullptr && film->domain == HomeyDomain::Scene, "started flow is a scene");
    expect(findId(list, count, "homey-flow:f2") == nullptr, "presence flow is not a button");
    expect(findId(list, count, "homey-flow:f3") == nullptr, "disabled flow skipped");

    const HomeyDevice *avond = findId(list, count, "homey-aflow:a1");
    expect(avond != nullptr && std::strcmp(avond->name, "Avond") == 0, "triggerable advanced flow");
    expect(findId(list, count, "homey-aflow:a2") == nullptr, "advanced flow without start card skipped");

    HomeyDevice all[16] = {};
    const int unfiltered = appendHomeyDevices(devices, zoneList, static_cast<size_t>(zoneCount), "", all, 0, 16);
    expect(unfiltered == 3, "empty zone filter keeps both rooms and skips lock and offline");

    expect(loadHomeyZones("{", zoneList, 8) == -1, "broken zone JSON fails");
    expect(appendHomeyDevices("[]", zoneList, 0, "", all, 0, 16) == -1, "device list must be an object");
    expect(appendHomeyFlows(nullptr, false, all, 1, 16) == 1, "missing flows add nothing");

    if (gFailures == 0) {
        std::printf("ok\n");
        return 0;
    }
    std::printf("%d failed\n", gFailures);
    return 1;
}
