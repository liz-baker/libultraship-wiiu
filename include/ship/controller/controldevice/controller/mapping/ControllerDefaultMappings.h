#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#ifndef __WIIU__
#include <SDL3/SDL.h>
#else
typedef int SDL_GamepadButton;
typedef int SDL_GamepadAxis;
#endif
#include "ControllerAxisDirectionMapping.h"

#ifndef CONTROLLERBUTTONS_T
#define CONTROLLERBUTTONS_T uint16_t
#endif

#include "ship/controller/controldevice/controller/mapping/keyboard/KeyboardScancodes.h"

#ifdef __WIIU__
#include "ship/port/wiiu/WiiUInput.h"
#endif

namespace Ship {

/**
 * @brief Holds the default input-to-button and input-to-axis mappings for a controller.
 *
 * Provides separate mapping tables for keyboard-key, SDL-gamepad-button, and
 * SDL-gamepad-axis input sources. Subclasses may override the protected setters to
 * customise the defaults for a specific game or controller layout.
 */
class ControllerDefaultMappings {
  public:
    /**
     * @brief Constructs a ControllerDefaultMappings with fully specified default tables.
     * @param defaultKeyboardKeyToButtonMappings              Keyboard key to button bitmask mappings.
     * @param defaultKeyboardKeyToAxisDirectionMappings        Keyboard key to axis direction mappings.
     * @param defaultSDLButtonToButtonMappings                 SDL gamepad button to button bitmask mappings.
     * @param defaultSDLButtonToAxisDirectionMappings           SDL gamepad button to axis direction mappings.
     * @param defaultSDLAxisDirectionToButtonMappings           SDL gamepad axis direction to button bitmask mappings.
     * @param defaultSDLAxisDirectionToAxisDirectionMappings    SDL gamepad axis direction to axis direction mappings.
     */
    ControllerDefaultMappings(
        std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<KbScancode>> defaultKeyboardKeyToButtonMappings,
        std::unordered_map<StickIndex, std::vector<std::pair<Direction, KbScancode>>>
            defaultKeyboardKeyToAxisDirectionMappings,
        std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<SDL_GamepadButton>> defaultSDLButtonToButtonMappings,
        std::unordered_map<StickIndex, std::vector<std::pair<Direction, SDL_GamepadButton>>>
            defaultSDLButtonToAxisDirectionMappings,
        std::unordered_map<CONTROLLERBUTTONS_T, std::vector<std::pair<SDL_GamepadAxis, int32_t>>>
            defaultSDLAxisDirectionToButtonMappings,
        std::unordered_map<StickIndex, std::vector<std::pair<Direction, std::pair<SDL_GamepadAxis, int32_t>>>>
            defaultSDLAxisDirectionToAxisDirectionMappings);

    /** @brief Constructs a ControllerDefaultMappings with empty default tables. */
    ControllerDefaultMappings();
    ~ControllerDefaultMappings();

    /**
     * @brief Returns the default keyboard-key-to-button mappings.
     * @return Map of button bitmask to set of keyboard scancodes.
     */
    std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<KbScancode>> GetDefaultKeyboardKeyToButtonMappings();

    /**
     * @brief Returns the default keyboard-key-to-axis-direction mappings.
     * @return Map of stick index to direction/scancode pairs.
     */
    std::unordered_map<StickIndex, std::vector<std::pair<Direction, KbScancode>>>
    GetDefaultKeyboardKeyToAxisDirectionMappings();

    /**
     * @brief Returns the default SDL-button-to-button mappings.
     * @return Map of button bitmask to set of SDL gamepad buttons.
     */
    std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<SDL_GamepadButton>>
    GetDefaultSDLButtonToButtonMappings();

    /**
     * @brief Returns the default SDL-button-to-axis-direction mappings.
     * @return Map of stick index to direction/SDL-button pairs.
     */
    std::unordered_map<StickIndex, std::vector<std::pair<Direction, SDL_GamepadButton>>>
    GetDefaultSDLButtonToAxisDirectionMappings();

    /**
     * @brief Returns the default SDL-axis-direction-to-button mappings.
     * @return Map of button bitmask to axis/threshold pairs.
     */
    std::unordered_map<CONTROLLERBUTTONS_T, std::vector<std::pair<SDL_GamepadAxis, int32_t>>>
    GetDefaultSDLAxisDirectionToButtonMappings();

    /**
     * @brief Returns the default SDL-axis-direction-to-axis-direction mappings.
     * @return Map of stick index to direction/(axis, threshold) pairs.
     */
    std::unordered_map<StickIndex, std::vector<std::pair<Direction, std::pair<SDL_GamepadAxis, int32_t>>>>
    GetDefaultSDLAxisDirectionToAxisDirectionMappings();

#ifdef __WIIU__
    /**
     * @brief Returns the default normalized Wii U buttons for each N64 button.
     *
     * Keyed by N64 button bitmask; the values are WiiUButton bits. A bitmask may map
     * to several buttons so that one table covers the GamePad, the Pro and Classic
     * Controllers, and a bare Wii Remote at once.
     */
    std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<uint32_t>> GetDefaultWiiUButtonToButtonMappings();

    /** @brief Returns the default Wii U (axis, direction) pairs for each N64 button. */
    std::unordered_map<CONTROLLERBUTTONS_T, std::vector<std::pair<int32_t, int32_t>>>
    GetDefaultWiiUAxisDirectionToButtonMappings();

    /** @brief Returns the default Wii U (axis, direction) pairs for each N64 stick direction. */
    std::unordered_map<StickIndex, std::vector<std::pair<Direction, std::pair<int32_t, int32_t>>>>
    GetDefaultWiiUAxisDirectionToAxisDirectionMappings();
#endif

  protected:
    /**
     * @brief Replaces the default keyboard-key-to-button mappings.
     * @param defaultKeyboardKeyToButtonMappings The new mappings.
     */
    virtual void SetDefaultKeyboardKeyToButtonMappings(
        std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<KbScancode>> defaultKeyboardKeyToButtonMappings);

    /**
     * @brief Replaces the default keyboard-key-to-axis-direction mappings.
     * @param defaultKeyboardKeyToAxisDirectionMappings The new mappings.
     */
    virtual void SetDefaultKeyboardKeyToAxisDirectionMappings(
        std::unordered_map<StickIndex, std::vector<std::pair<Direction, KbScancode>>>
            defaultKeyboardKeyToAxisDirectionMappings);

    /**
     * @brief Replaces the default SDL-button-to-button mappings.
     * @param defaultSDLButtonToButtonMappings The new mappings.
     */
    virtual void
    SetDefaultSDLButtonToButtonMappings(std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<SDL_GamepadButton>>
                                            defaultSDLButtonToButtonMappings);

    /**
     * @brief Replaces the default SDL-button-to-axis-direction mappings.
     * @param defaultSDLButtonToAxisDirectionMappings The new mappings.
     */
    virtual void SetDefaultSDLButtonToAxisDirectionMappings(
        std::unordered_map<StickIndex, std::vector<std::pair<Direction, SDL_GamepadButton>>>
            defaultSDLButtonToAxisDirectionMappings);

    /**
     * @brief Replaces the default SDL-axis-direction-to-button mappings.
     * @param defaultSDLAxisDirectionToButtonMappings The new mappings.
     */
    virtual void SetDefaultSDLAxisDirectionToButtonMappings(
        std::unordered_map<CONTROLLERBUTTONS_T, std::vector<std::pair<SDL_GamepadAxis, int32_t>>>
            defaultSDLAxisDirectionToButtonMappings);

    /**
     * @brief Replaces the default SDL-axis-direction-to-axis-direction mappings.
     * @param defaultSDLAxisDirectionToAxisDirectionMappings The new mappings.
     */
    virtual void SetDefaultSDLAxisDirectionToAxisDirectionMappings(
        std::unordered_map<StickIndex, std::vector<std::pair<Direction, std::pair<SDL_GamepadAxis, int32_t>>>>
            defaultSDLAxisDirectionToAxisDirectionMappings);

#ifdef __WIIU__
    /**
     * @brief Installs the Wii U button defaults, falling back to the built-in table.
     *
     * Unlike the SDL tables, which libultraship leaves to the consuming game, the Wii U
     * tables ship with a usable default: SDL is unavailable on the console, so nothing
     * else would provide one.
     */
    virtual void SetDefaultWiiUButtonToButtonMappings(
        std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<uint32_t>> defaultWiiUButtonToButtonMappings);

    virtual void SetDefaultWiiUAxisDirectionToButtonMappings(
        std::unordered_map<CONTROLLERBUTTONS_T, std::vector<std::pair<int32_t, int32_t>>>
            defaultWiiUAxisDirectionToButtonMappings);

    virtual void SetDefaultWiiUAxisDirectionToAxisDirectionMappings(
        std::unordered_map<StickIndex, std::vector<std::pair<Direction, std::pair<int32_t, int32_t>>>>
            defaultWiiUAxisDirectionToAxisDirectionMappings);
#endif

  private:
    std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<KbScancode>> mDefaultKeyboardKeyToButtonMappings;
    std::unordered_map<StickIndex, std::vector<std::pair<Direction, KbScancode>>>
        mDefaultKeyboardKeyToAxisDirectionMappings;
    std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<SDL_GamepadButton>> mDefaultSDLButtonToButtonMappings;
    std::unordered_map<StickIndex, std::vector<std::pair<Direction, SDL_GamepadButton>>>
        mDefaultSDLButtonToAxisDirectionMappings;
    std::unordered_map<CONTROLLERBUTTONS_T, std::vector<std::pair<SDL_GamepadAxis, int32_t>>>
        mDefaultSDLAxisDirectionToButtonMappings;
    std::unordered_map<StickIndex, std::vector<std::pair<Direction, std::pair<SDL_GamepadAxis, int32_t>>>>
        mDefaultSDLAxisDirectionToAxisDirectionMappings;

#ifdef __WIIU__
    std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<uint32_t>> mDefaultWiiUButtonToButtonMappings;
    std::unordered_map<CONTROLLERBUTTONS_T, std::vector<std::pair<int32_t, int32_t>>>
        mDefaultWiiUAxisDirectionToButtonMappings;
    std::unordered_map<StickIndex, std::vector<std::pair<Direction, std::pair<int32_t, int32_t>>>>
        mDefaultWiiUAxisDirectionToAxisDirectionMappings;
#endif
};
} // namespace Ship
