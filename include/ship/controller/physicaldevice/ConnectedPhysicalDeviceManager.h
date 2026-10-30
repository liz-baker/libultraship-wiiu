#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include "ship/controller/physicaldevice/PhysicalDeviceType.h"
#ifndef __WIIU__
#include <SDL3/SDL.h>
#else
typedef struct SDL_Gamepad SDL_Gamepad;
#endif

namespace Ship {

/** @brief How a gamepad's face buttons are labelled, for showing printed labels in an editor. */
enum class GamepadFaceLayout {
    Xbox,        ///< South/East/West/North are A/B/X/Y.
    Nintendo,    ///< South/East/West/North are B/A/Y/X.
    PlayStation, ///< South/East/West/North are Cross/Circle/Square/Triangle.
};

/** @brief One attached gamepad, as listed by ConnectedPhysicalDeviceManager::GetConnectedGamepads(). */
struct ConnectedGamepad {
    /**
     * Identifier accepted by the per-port ignore functions. On SDL platforms it is the SDL joystick
     * instance ID; on the Wii U it is a WiiU device index (WIIU_DEVICE_GAMEPAD or a KPAD channel).
     */
    int32_t id;
    std::string name;              ///< Human-readable device name.
    PhysicalDeviceType deviceType; ///< PHYSICAL_DEVICE_TYPE_GAMEPAD for the current platform.
    GamepadFaceLayout faceLayout;  ///< Always Nintendo on the Wii U.
};

/**
 * @brief Tracks connected gamepads and manages per-port ignore lists.
 *
 * Maintains a live map of connected SDL gamepads (on the Wii U, the native VPAD/KPAD devices are
 * queried live instead). Per-port ignore lists allow a controller to be excluded from a specific
 * port; they apply to the Wii U's devices the same way as to SDL's.
 */
class ConnectedPhysicalDeviceManager {
  public:
    /** @brief Constructs the manager and performs an initial scan of connected gamepads. */
    ConnectedPhysicalDeviceManager();
    ~ConnectedPhysicalDeviceManager();

    /**
     * @brief Returns the connected SDL gamepads available for the given port.
     *
     * Gamepads on the port's ignore list are excluded from the result.
     *
     * @param portIndex Zero-based controller port index.
     * @return Map of SDL joystick instance ID to SDL_Gamepad pointer.
     */
    std::unordered_map<int32_t, SDL_Gamepad*> GetConnectedSDLGamepadsForPort(uint8_t portIndex);

    /**
     * @brief Returns the display names of all connected SDL gamepads.
     * @return Map of SDL joystick instance ID to human-readable gamepad name.
     */
    std::unordered_map<int32_t, std::string> GetConnectedSDLGamepadNames();

    /**
     * @brief Lists the attached gamepads on every platform.
     *
     * Ordered by ConnectedGamepad::id ascending on SDL platforms. On the Wii U the order is the
     * GamePad, then KPAD channels 0 to 3; only the GamePad, Pro Controllers and Wii Remotes with a
     * Classic Controller are listed (a bare Wii Remote or one with a Nunchuk is not supported).
     * Reflects hot-plug as soon as SDL reports it (call RefreshConnectedSDLGamepads() from the
     * add/remove handler, as the SDL event handler already does); the Wii U is queried live.
     *
     * @return The attached gamepads.
     */
    std::vector<ConnectedGamepad> GetConnectedGamepads();

    /**
     * @brief Returns whether a keyboard is attached (SDL_HasKeyboard), evaluated at call time so it
     *        follows hot-plug. Always false on the Wii U.
     */
    bool HasKeyboard();

    /**
     * @brief Returns whether a mouse is attached (SDL_HasMouse), evaluated at call time so it
     *        follows hot-plug. Always false on the Wii U.
     */
    bool HasMouse();

    /**
     * @brief Returns whether a device may drive a port, i.e. it is not on that port's ignore list.
     * @param portIndex Zero-based controller port index.
     * @param id        ConnectedGamepad::id of the device.
     */
    bool DeviceIsRoutedToPort(uint8_t portIndex, int32_t id);

    /**
     * @brief Returns the set of SDL joystick instance IDs ignored for a port.
     * @param portIndex Zero-based controller port index.
     * @return Set of ignored joystick instance IDs.
     */
    std::unordered_set<int32_t> GetIgnoredInstanceIdsForPort(uint8_t portIndex);

    /**
     * @brief Checks whether a specific joystick instance is being ignored on a port.
     * @param portIndex  Zero-based controller port index.
     * @param instanceId SDL joystick instance ID.
     * @return true if the instance is ignored for the given port.
     */
    bool PortIsIgnoringInstanceId(uint8_t portIndex, int32_t instanceId);

    /**
     * @brief Adds a joystick instance to a port's ignore list.
     * @param portIndex  Zero-based controller port index.
     * @param instanceId SDL joystick instance ID to ignore.
     */
    void IgnoreInstanceIdForPort(uint8_t portIndex, int32_t instanceId);

    /**
     * @brief Removes a joystick instance from a port's ignore list.
     * @param portIndex  Zero-based controller port index.
     * @param instanceId SDL joystick instance ID to stop ignoring.
     */
    void UnignoreInstanceIdForPort(uint8_t portIndex, int32_t instanceId);

    /**
     * @brief Handles a gamepad-added event by refreshing the connected set.
     * @param instanceId Event joystick instance ID.
     */
    void HandlePhysicalDeviceConnect(int32_t instanceId);

    /**
     * @brief Handles a gamepad-removed event by refreshing the connected set.
     * @param instanceId Event joystick instance ID.
     */
    void HandlePhysicalDeviceDisconnect(int32_t instanceId);

    /** @brief Re-scans SDL gamepads, preserving handles for devices that remain connected. */
    void RefreshConnectedSDLGamepads();

  private:
    void CloseConnectedSDLGamepads();

    std::unordered_map<int32_t, SDL_Gamepad*> mConnectedSDLGamepads;
    std::unordered_map<int32_t, std::string> mConnectedSDLGamepadNames;
    std::unordered_map<uint8_t, std::unordered_set<int32_t>> mIgnoredInstanceIds;
};
} // namespace Ship
