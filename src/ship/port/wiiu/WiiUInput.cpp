#ifdef __WIIU__
#include "ship/port/wiiu/WiiUInput.h"

#include "WiiUImpl.h"

#include <vpad/input.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>

namespace Ship {
namespace WiiU {

namespace {

// The concrete hardware behind a device index. KPAD reports this per channel and
// it decides which half of the KPADStatus union carries the buttons and sticks.
enum class DeviceKind { None, GamePad, WiiRemote, Nunchuk, Classic, Pro };

// A rumble pattern long enough to outlast one frame; UpdateRumble() re-arms it
// every poll for as long as the mapping keeps rumble enabled.
constexpr uint8_t kVpadRumblePattern[15] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                             0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

bool sRumbleEnabled[WIIU_KPAD_CHANNEL_COUNT + 1] = { false };

// Maps a device index onto its slot in sRumbleEnabled, with the GamePad last.
size_t RumbleSlot(int32_t deviceIndex) {
    return deviceIndex == WIIU_DEVICE_GAMEPAD ? WIIU_KPAD_CHANNEL_COUNT : static_cast<size_t>(deviceIndex);
}

bool IsKPADChannel(int32_t deviceIndex) {
    return deviceIndex >= 0 && deviceIndex < WIIU_KPAD_CHANNEL_COUNT;
}

KPADStatus* GetKPAD(int32_t deviceIndex) {
    if (!IsKPADChannel(deviceIndex)) {
        return nullptr;
    }

    KPADError error = KPAD_ERROR_OK;
    KPADStatus* status = GetKPADStatus(static_cast<WPADChan>(deviceIndex), &error);
    return error == KPAD_ERROR_OK ? status : nullptr;
}

VPADStatus* GetVPAD() {
    VPADReadError error = VPAD_READ_SUCCESS;
    VPADStatus* status = GetVPADStatus(&error);
    return error == VPAD_READ_SUCCESS ? status : nullptr;
}

DeviceKind GetDeviceKind(int32_t deviceIndex) {
    if (deviceIndex == WIIU_DEVICE_GAMEPAD) {
        return GetVPAD() != nullptr ? DeviceKind::GamePad : DeviceKind::None;
    }

    KPADStatus* status = GetKPAD(deviceIndex);
    if (status == nullptr) {
        return DeviceKind::None;
    }

    switch (status->extensionType) {
        case WPAD_EXT_CORE:
        case WPAD_EXT_MPLUS:
            return DeviceKind::WiiRemote;
        case WPAD_EXT_NUNCHUK:
        case WPAD_EXT_MPLUS_NUNCHUK:
            return DeviceKind::Nunchuk;
        case WPAD_EXT_CLASSIC:
        case WPAD_EXT_MPLUS_CLASSIC:
            return DeviceKind::Classic;
        case WPAD_EXT_PRO_CONTROLLER:
            return DeviceKind::Pro;
        default:
            return DeviceKind::None;
    }
}

// One entry of a device's native mask -> normalized mask translation table.
struct ButtonTranslation {
    uint32_t nativeMask;
    uint32_t normalizedMask;
};

constexpr ButtonTranslation kVpadButtons[] = {
    { VPAD_BUTTON_A, WIIU_BUTTON_A },
    { VPAD_BUTTON_B, WIIU_BUTTON_B },
    { VPAD_BUTTON_X, WIIU_BUTTON_X },
    { VPAD_BUTTON_Y, WIIU_BUTTON_Y },
    { VPAD_BUTTON_UP, WIIU_BUTTON_UP },
    { VPAD_BUTTON_DOWN, WIIU_BUTTON_DOWN },
    { VPAD_BUTTON_LEFT, WIIU_BUTTON_LEFT },
    { VPAD_BUTTON_RIGHT, WIIU_BUTTON_RIGHT },
    { VPAD_BUTTON_L, WIIU_BUTTON_L },
    { VPAD_BUTTON_R, WIIU_BUTTON_R },
    { VPAD_BUTTON_ZL, WIIU_BUTTON_ZL },
    { VPAD_BUTTON_ZR, WIIU_BUTTON_ZR },
    { VPAD_BUTTON_PLUS, WIIU_BUTTON_PLUS },
    { VPAD_BUTTON_MINUS, WIIU_BUTTON_MINUS },
    { VPAD_BUTTON_HOME, WIIU_BUTTON_HOME },
    { VPAD_BUTTON_STICK_L, WIIU_BUTTON_STICK_L },
    { VPAD_BUTTON_STICK_R, WIIU_BUTTON_STICK_R },
};

// Core Wii Remote buttons, reported in KPADStatus::hold. The d-pad is not rotated
// for sideways play: a binding always follows the labels printed on the shell.
constexpr ButtonTranslation kWiiRemoteButtons[] = {
    { WPAD_BUTTON_A, WIIU_BUTTON_A },       { WPAD_BUTTON_B, WIIU_BUTTON_B },
    { WPAD_BUTTON_1, WIIU_BUTTON_ONE },     { WPAD_BUTTON_2, WIIU_BUTTON_TWO },
    { WPAD_BUTTON_UP, WIIU_BUTTON_UP },     { WPAD_BUTTON_DOWN, WIIU_BUTTON_DOWN },
    { WPAD_BUTTON_LEFT, WIIU_BUTTON_LEFT }, { WPAD_BUTTON_RIGHT, WIIU_BUTTON_RIGHT },
    { WPAD_BUTTON_PLUS, WIIU_BUTTON_PLUS }, { WPAD_BUTTON_MINUS, WIIU_BUTTON_MINUS },
    { WPAD_BUTTON_HOME, WIIU_BUTTON_HOME },
};

// KPAD folds the nunchuk's C and Z into the core hold mask.
constexpr ButtonTranslation kNunchukButtons[] = {
    { WPAD_NUNCHUK_BUTTON_C, WIIU_BUTTON_C },
    { WPAD_NUNCHUK_BUTTON_Z, WIIU_BUTTON_Z },
};

constexpr ButtonTranslation kClassicButtons[] = {
    { WPAD_CLASSIC_BUTTON_A, WIIU_BUTTON_A },       { WPAD_CLASSIC_BUTTON_B, WIIU_BUTTON_B },
    { WPAD_CLASSIC_BUTTON_X, WIIU_BUTTON_X },       { WPAD_CLASSIC_BUTTON_Y, WIIU_BUTTON_Y },
    { WPAD_CLASSIC_BUTTON_UP, WIIU_BUTTON_UP },     { WPAD_CLASSIC_BUTTON_DOWN, WIIU_BUTTON_DOWN },
    { WPAD_CLASSIC_BUTTON_LEFT, WIIU_BUTTON_LEFT }, { WPAD_CLASSIC_BUTTON_RIGHT, WIIU_BUTTON_RIGHT },
    { WPAD_CLASSIC_BUTTON_L, WIIU_BUTTON_L },       { WPAD_CLASSIC_BUTTON_R, WIIU_BUTTON_R },
    { WPAD_CLASSIC_BUTTON_ZL, WIIU_BUTTON_ZL },     { WPAD_CLASSIC_BUTTON_ZR, WIIU_BUTTON_ZR },
    { WPAD_CLASSIC_BUTTON_PLUS, WIIU_BUTTON_PLUS }, { WPAD_CLASSIC_BUTTON_MINUS, WIIU_BUTTON_MINUS },
    { WPAD_CLASSIC_BUTTON_HOME, WIIU_BUTTON_HOME },
};

constexpr ButtonTranslation kProButtons[] = {
    { WPAD_PRO_BUTTON_A, WIIU_BUTTON_A },
    { WPAD_PRO_BUTTON_B, WIIU_BUTTON_B },
    { WPAD_PRO_BUTTON_X, WIIU_BUTTON_X },
    { WPAD_PRO_BUTTON_Y, WIIU_BUTTON_Y },
    { WPAD_PRO_BUTTON_UP, WIIU_BUTTON_UP },
    { WPAD_PRO_BUTTON_DOWN, WIIU_BUTTON_DOWN },
    { WPAD_PRO_BUTTON_LEFT, WIIU_BUTTON_LEFT },
    { WPAD_PRO_BUTTON_RIGHT, WIIU_BUTTON_RIGHT },
    { WPAD_PRO_TRIGGER_L, WIIU_BUTTON_L },
    { WPAD_PRO_TRIGGER_R, WIIU_BUTTON_R },
    { WPAD_PRO_TRIGGER_ZL, WIIU_BUTTON_ZL },
    { WPAD_PRO_TRIGGER_ZR, WIIU_BUTTON_ZR },
    { WPAD_PRO_BUTTON_PLUS, WIIU_BUTTON_PLUS },
    { WPAD_PRO_BUTTON_MINUS, WIIU_BUTTON_MINUS },
    { WPAD_PRO_BUTTON_HOME, WIIU_BUTTON_HOME },
    { WPAD_PRO_BUTTON_STICK_L, WIIU_BUTTON_STICK_L },
    { WPAD_PRO_BUTTON_STICK_R, WIIU_BUTTON_STICK_R },
};

template <size_t N> uint32_t Translate(uint32_t nativeHold, const ButtonTranslation (&table)[N]) {
    uint32_t normalized = 0;
    for (const auto& entry : table) {
        if (nativeHold & entry.nativeMask) {
            normalized |= entry.normalizedMask;
        }
    }
    return normalized;
}

} // namespace

bool DeviceIsConnected(int32_t deviceIndex) {
    return GetDeviceKind(deviceIndex) != DeviceKind::None;
}

std::vector<int32_t> GetConnectedDeviceIndices() {
    std::vector<int32_t> devices;

    if (DeviceIsConnected(WIIU_DEVICE_GAMEPAD)) {
        devices.push_back(WIIU_DEVICE_GAMEPAD);
    }

    for (int32_t chan = 0; chan < WIIU_KPAD_CHANNEL_COUNT; chan++) {
        if (DeviceIsConnected(chan)) {
            devices.push_back(chan);
        }
    }

    return devices;
}

uint32_t GetButtonsHeld(int32_t deviceIndex) {
    switch (GetDeviceKind(deviceIndex)) {
        case DeviceKind::GamePad:
            return Translate(GetVPAD()->hold, kVpadButtons);
        case DeviceKind::WiiRemote:
            return Translate(GetKPAD(deviceIndex)->hold, kWiiRemoteButtons);
        case DeviceKind::Nunchuk: {
            const uint32_t hold = GetKPAD(deviceIndex)->hold;
            return Translate(hold, kWiiRemoteButtons) | Translate(hold, kNunchukButtons);
        }
        case DeviceKind::Classic:
            return Translate(GetKPAD(deviceIndex)->classic.hold, kClassicButtons);
        case DeviceKind::Pro:
            return Translate(GetKPAD(deviceIndex)->pro.hold, kProButtons);
        case DeviceKind::None:
        default:
            return 0;
    }
}

float GetAxisValue(int32_t deviceIndex, int32_t axis) {
    if (axis < 0 || axis >= WIIU_AXIS_COUNT) {
        return 0.0f;
    }

    const DeviceKind kind = GetDeviceKind(deviceIndex);
    const bool isLeft = axis == WIIU_AXIS_LEFT_X || axis == WIIU_AXIS_LEFT_Y;
    const bool isX = axis == WIIU_AXIS_LEFT_X || axis == WIIU_AXIS_RIGHT_X;

    switch (kind) {
        case DeviceKind::GamePad: {
            const VPADVec2D stick = isLeft ? GetVPAD()->leftStick : GetVPAD()->rightStick;
            return isX ? stick.x : stick.y;
        }
        case DeviceKind::Nunchuk: {
            // A nunchuk has one stick, which stands in for the left stick.
            if (!isLeft) {
                return 0.0f;
            }
            const KPADVec2D stick = GetKPAD(deviceIndex)->nunchuck.stick;
            return isX ? stick.x : stick.y;
        }
        case DeviceKind::Classic: {
            const KPADStatus* status = GetKPAD(deviceIndex);
            const KPADVec2D stick = isLeft ? status->classic.leftStick : status->classic.rightStick;
            return isX ? stick.x : stick.y;
        }
        case DeviceKind::Pro: {
            const KPADStatus* status = GetKPAD(deviceIndex);
            const KPADVec2D stick = isLeft ? status->pro.leftStick : status->pro.rightStick;
            return isX ? stick.x : stick.y;
        }
        case DeviceKind::WiiRemote:
        case DeviceKind::None:
        default:
            // A bare Wii Remote has no analog stick.
            return 0.0f;
    }
}

std::string GetDeviceName(int32_t deviceIndex) {
    switch (GetDeviceKind(deviceIndex)) {
        case DeviceKind::GamePad:
            return "Wii U GamePad";
        case DeviceKind::WiiRemote:
            return "Wii Remote " + std::to_string(deviceIndex + 1);
        case DeviceKind::Nunchuk:
            return "Wii Remote + Nunchuk " + std::to_string(deviceIndex + 1);
        case DeviceKind::Classic:
            return "Classic Controller " + std::to_string(deviceIndex + 1);
        case DeviceKind::Pro:
            return "Wii U Pro Controller " + std::to_string(deviceIndex + 1);
        case DeviceKind::None:
        default:
            return deviceIndex == WIIU_DEVICE_GAMEPAD
                       ? "Wii U GamePad (disconnected)"
                       : "Wii U Controller " + std::to_string(deviceIndex + 1) + " (disconnected)";
    }
}

std::string GetButtonName(int32_t deviceIndex, uint32_t button) {
    const DeviceKind kind = GetDeviceKind(deviceIndex);
    const bool isWiiRemote = kind == DeviceKind::WiiRemote || kind == DeviceKind::Nunchuk;

    switch (button) {
        case WIIU_BUTTON_A:
            return "A";
        case WIIU_BUTTON_B:
            return "B";
        case WIIU_BUTTON_UP:
            return "D-Pad Up";
        case WIIU_BUTTON_DOWN:
            return "D-Pad Down";
        case WIIU_BUTTON_LEFT:
            return "D-Pad Left";
        case WIIU_BUTTON_RIGHT:
            return "D-Pad Right";
        case WIIU_BUTTON_PLUS:
            return "+";
        case WIIU_BUTTON_MINUS:
            return "-";
        case WIIU_BUTTON_HOME:
            return "HOME";
        case WIIU_BUTTON_ONE:
            return isWiiRemote ? "1" : "";
        case WIIU_BUTTON_TWO:
            return isWiiRemote ? "2" : "";
        case WIIU_BUTTON_C:
            return kind == DeviceKind::Nunchuk ? "C" : "";
        case WIIU_BUTTON_Z:
            return kind == DeviceKind::Nunchuk ? "Z" : "";
        case WIIU_BUTTON_X:
            return isWiiRemote ? "" : "X";
        case WIIU_BUTTON_Y:
            return isWiiRemote ? "" : "Y";
        case WIIU_BUTTON_L:
            return isWiiRemote ? "" : "L";
        case WIIU_BUTTON_R:
            return isWiiRemote ? "" : "R";
        case WIIU_BUTTON_ZL:
            return isWiiRemote ? "" : "ZL";
        case WIIU_BUTTON_ZR:
            return isWiiRemote ? "" : "ZR";
        case WIIU_BUTTON_STICK_L:
            return kind == DeviceKind::GamePad || kind == DeviceKind::Pro ? "Left Stick Button" : "";
        case WIIU_BUTTON_STICK_R:
            return kind == DeviceKind::GamePad || kind == DeviceKind::Pro ? "Right Stick Button" : "";
        default:
            return "";
    }
}

bool DeviceSupportsRumble(int32_t deviceIndex) {
    // Every Wii U input device has a motor, the GamePad's included.
    return DeviceIsConnected(deviceIndex);
}

void SetRumble(int32_t deviceIndex, bool enabled) {
    if (deviceIndex != WIIU_DEVICE_GAMEPAD && !IsKPADChannel(deviceIndex)) {
        return;
    }

    sRumbleEnabled[RumbleSlot(deviceIndex)] = enabled;

    if (deviceIndex == WIIU_DEVICE_GAMEPAD) {
        if (enabled) {
            VPADControlMotor(VPAD_CHAN_0, const_cast<uint8_t*>(kVpadRumblePattern), sizeof(kVpadRumblePattern));
        } else {
            VPADStopMotor(VPAD_CHAN_0);
        }
        return;
    }

    WPADControlMotor(static_cast<WPADChan>(deviceIndex), enabled ? TRUE : FALSE);
}

void UpdateRumble() {
    // VPAD motor patterns are finite, so re-arm the GamePad every poll for as long
    // as a mapping wants it running. WPAD motors latch until told to stop.
    if (sRumbleEnabled[RumbleSlot(WIIU_DEVICE_GAMEPAD)]) {
        VPADControlMotor(VPAD_CHAN_0, const_cast<uint8_t*>(kVpadRumblePattern), sizeof(kVpadRumblePattern));
    }
}

} // namespace WiiU
} // namespace Ship

#endif
