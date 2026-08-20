#pragma once

namespace Ship {

/**
 * @brief Identifies the category of a physical input device.
 *
 * Used throughout the controller mapping system to distinguish between
 * different classes of hardware when creating or looking up input mappings.
 */
enum PhysicalDeviceType {
    Keyboard = 0,    ///< Standard keyboard.
    Mouse = 1,       ///< Mouse (buttons and motion).
    SDLGamepad = 2,  ///< SDL-managed game controller.
    WiiUGamepad = 3, ///< Wii U GamePad / Wii Remote / Pro Controller, read natively via VPAD/KPAD.
    Max = 4          ///< Sentinel / count of device types.
};

/**
 * @brief The gamepad device type available on the current platform.
 *
 * SDL3 is not available for the Wii U, so the console reads its controllers through
 * VPAD/KPAD instead. Platform-agnostic code that just means "the gamepad" should use
 * this rather than naming a backend.
 */
#ifdef __WIIU__
#define PHYSICAL_DEVICE_TYPE_GAMEPAD Ship::PhysicalDeviceType::WiiUGamepad
#else
#define PHYSICAL_DEVICE_TYPE_GAMEPAD Ship::PhysicalDeviceType::SDLGamepad
#endif

} // namespace Ship
