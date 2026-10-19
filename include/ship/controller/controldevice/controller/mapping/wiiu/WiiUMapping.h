#pragma once

#include <string>
#include <vector>

#include "ship/controller/controldevice/controller/mapping/AxisDirection.h"
#include "ship/port/wiiu/WiiUInput.h"

namespace Ship {

/**
 * @brief Returns a short, stable token identifying a Wii U device in a mapping id.
 *
 * Mapping ids end up in config keys, so the GamePad gets a name rather than the
 * WIIU_DEVICE_GAMEPAD sentinel's "-1".
 */
inline std::string WiiUDeviceToken(int32_t deviceIndex) {
    return deviceIndex == WIIU_DEVICE_GAMEPAD ? "GP" : "K" + std::to_string(deviceIndex);
}

/** @brief Number of controller ports the Wii U backend routes devices to. */
#define WIIU_PORT_COUNT 4

/** @brief Returns every Wii U device id: the GamePad, then each KPAD channel. */
inline std::vector<int32_t> WiiUAllDevices() {
    std::vector<int32_t> devices = { WIIU_DEVICE_GAMEPAD };
    for (int32_t chan = 0; chan < WIIU_KPAD_CHANNEL_COUNT; chan++) {
        devices.push_back(chan);
    }
    return devices;
}

/**
 * @brief Returns the port a Wii U device drives until it is routed elsewhere.
 *
 * The GamePad and KPAD channel 0 drive port 0, so a single Wii Remote synced as channel 0 works
 * as player 1 without touching the input editor. Every other channel drives the port of the same
 * number, which leaves no channel shared between two ports.
 */
inline uint8_t WiiUDefaultPortForDevice(int32_t deviceIndex) {
    return deviceIndex == WIIU_DEVICE_GAMEPAD ? 0 : static_cast<uint8_t>(deviceIndex);
}

/**
 * @brief Returns the Wii U devices a port gets default mappings for.
 *
 * Every port gets mappings for every device so that a device can be routed to any port with
 * ConnectedPhysicalDeviceManager::UnignoreInstanceIdForPort(). The manager's ignore lists start
 * out routing each device to WiiUDefaultPortForDevice() only.
 */
inline std::vector<int32_t> WiiUDefaultDevicesForPort(uint8_t) {
    return WiiUAllDevices();
}

} // namespace Ship
