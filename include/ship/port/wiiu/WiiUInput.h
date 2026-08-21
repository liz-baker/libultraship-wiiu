#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Ship {
namespace WiiU {

/**
 * @brief Identifies a physical Wii U input device.
 *
 * The GamePad (DRC) is read through VPAD and has no channel of its own, so it is
 * addressed by the sentinel WIIU_DEVICE_GAMEPAD. Wii Remotes, Classic Controllers
 * and Pro Controllers are read through KPAD and are addressed by their channel,
 * 0 through WIIU_KPAD_CHANNEL_COUNT - 1.
 */
#define WIIU_DEVICE_GAMEPAD (-1)
#define WIIU_KPAD_CHANNEL_COUNT 4

/**
 * @brief Normalized button set shared by every Wii U input device.
 *
 * VPAD and the various KPAD extensions each use their own, mutually incompatible
 * button masks. The mapping layer stores buttons in this normalized set instead so
 * that a saved binding stays meaningful when the player swaps a Wii Remote for a
 * Pro Controller, and so that one button-name table covers every device.
 *
 * Buttons a given device does not have are simply never reported as held by
 * GetButtonsHeld(), and GetButtonName() returns an empty string for them.
 */
enum WiiUButton : uint32_t {
    WIIU_BUTTON_A = 1u << 0,
    WIIU_BUTTON_B = 1u << 1,
    WIIU_BUTTON_X = 1u << 2,
    WIIU_BUTTON_Y = 1u << 3,
    WIIU_BUTTON_UP = 1u << 4,
    WIIU_BUTTON_DOWN = 1u << 5,
    WIIU_BUTTON_LEFT = 1u << 6,
    WIIU_BUTTON_RIGHT = 1u << 7,
    WIIU_BUTTON_L = 1u << 8,
    WIIU_BUTTON_R = 1u << 9,
    WIIU_BUTTON_ZL = 1u << 10,
    WIIU_BUTTON_ZR = 1u << 11,
    WIIU_BUTTON_PLUS = 1u << 12,
    WIIU_BUTTON_MINUS = 1u << 13,
    WIIU_BUTTON_HOME = 1u << 14,
    WIIU_BUTTON_STICK_L = 1u << 15,
    WIIU_BUTTON_STICK_R = 1u << 16,
    WIIU_BUTTON_ONE = 1u << 17, ///< Wii Remote "1".
    WIIU_BUTTON_TWO = 1u << 18, ///< Wii Remote "2".
    WIIU_BUTTON_C = 1u << 19,   ///< Nunchuk "C".
    WIIU_BUTTON_Z = 1u << 20,   ///< Nunchuk "Z".
    WIIU_BUTTON_COUNT = 21,     ///< Number of buttons in the normalized set.
};

/** @brief Normalized analog stick axes shared by every Wii U input device. */
enum WiiUAxis : int32_t {
    WIIU_AXIS_LEFT_X = 0,
    WIIU_AXIS_LEFT_Y = 1,
    WIIU_AXIS_RIGHT_X = 2,
    WIIU_AXIS_RIGHT_Y = 3,
    WIIU_AXIS_COUNT = 4,
};

/**
 * @brief Returns true if @p deviceIndex was reporting valid data on the last Update().
 */
bool DeviceIsConnected(int32_t deviceIndex);

/**
 * @brief Returns the device indices that were connected on the last Update().
 *
 * The GamePad, when present, is always first.
 */
std::vector<int32_t> GetConnectedDeviceIndices();

/**
 * @brief Returns the normalized WiiUButton mask currently held on @p deviceIndex.
 * @return 0 if the device is disconnected.
 */
uint32_t GetButtonsHeld(int32_t deviceIndex);

/**
 * @brief Returns the value of one normalized stick axis on @p deviceIndex.
 * @return A value in [-1.0, 1.0], where up and right are positive. 0.0 if the
 *         device is disconnected or lacks that axis.
 */
float GetAxisValue(int32_t deviceIndex, int32_t axis);

/** @brief Returns a human-readable name for @p deviceIndex, e.g. "Wii U GamePad". */
std::string GetDeviceName(int32_t deviceIndex);

/**
 * @brief Returns a human-readable name for one normalized button on @p deviceIndex.
 *
 * The name is device-aware where the hardware labels differ, so WIIU_BUTTON_ZL is
 * "ZL" on a Pro Controller but has no name on a bare Wii Remote.
 *
 * @return The button label, or an empty string if the device has no such button.
 */
std::string GetButtonName(int32_t deviceIndex, uint32_t button);

/** @brief Returns true if @p deviceIndex has a motor that SetRumble() can drive. */
bool DeviceSupportsRumble(int32_t deviceIndex);

/**
 * @brief Starts or stops the rumble motor on @p deviceIndex.
 *
 * Wii U motors are on/off only — neither VPAD nor WPAD exposes an intensity — so
 * the mapping layer's low/high frequency percentages only decide whether the motor
 * runs at all.
 */
void SetRumble(int32_t deviceIndex, bool enabled);

} // namespace WiiU
} // namespace Ship
