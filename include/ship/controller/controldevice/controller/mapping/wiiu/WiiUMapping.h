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

/**
 * @brief Returns the Wii U devices a port binds to by default.
 *
 * Port 0 drives both the GamePad and the first KPAD channel, so a single Wii Remote
 * synced as channel 0 works as player 1 without touching the input editor. Every
 * other port takes the KPAD channel of the same number, which leaves no channel
 * shared between two ports.
 */
inline std::vector<int32_t> WiiUDefaultDevicesForPort(uint8_t portIndex) {
    if (portIndex == 0) {
        return { WIIU_DEVICE_GAMEPAD, 0 };
    }

    return { static_cast<int32_t>(portIndex) };
}

} // namespace Ship
