#include "ship/controller/controldevice/controller/mapping/ControllerDefaultMappings.h"

#ifdef __WIIU__
#include "libultraship/libultra/controller.h"
#include "ship/controller/controldevice/controller/mapping/AxisDirection.h"
#endif

namespace Ship {
ControllerDefaultMappings::ControllerDefaultMappings(
    std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<KbScancode>> defaultKeyboardKeyToButtonMappings,
    std::unordered_map<StickIndex, std::vector<std::pair<Direction, KbScancode>>>
        defaultKeyboardKeyToAxisDirectionMappings,
    std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<SDL_GamepadButton>> defaultSDLButtonToButtonMappings,
    std::unordered_map<StickIndex, std::vector<std::pair<Direction, SDL_GamepadButton>>>
        defaultSDLButtonToAxisDirectionMappings,
    std::unordered_map<CONTROLLERBUTTONS_T, std::vector<std::pair<SDL_GamepadAxis, int32_t>>>
        defaultSDLAxisDirectionToButtonMappings,
    std::unordered_map<StickIndex, std::vector<std::pair<Direction, std::pair<SDL_GamepadAxis, int32_t>>>>
        defaultSDLAxisDirectionToAxisDirectionMappings) {
    SetDefaultKeyboardKeyToButtonMappings(defaultKeyboardKeyToButtonMappings);
    SetDefaultKeyboardKeyToAxisDirectionMappings(defaultKeyboardKeyToAxisDirectionMappings);

    SetDefaultSDLButtonToButtonMappings(defaultSDLButtonToButtonMappings);
    SetDefaultSDLButtonToAxisDirectionMappings(defaultSDLButtonToAxisDirectionMappings);

    SetDefaultSDLAxisDirectionToButtonMappings(defaultSDLAxisDirectionToButtonMappings);
    SetDefaultSDLAxisDirectionToAxisDirectionMappings(defaultSDLAxisDirectionToAxisDirectionMappings);

#ifdef __WIIU__
    // The Wii U tables have no constructor parameters of their own; passing empty maps
    // installs the built-in defaults, which a consumer can still replace via the setters.
    SetDefaultWiiUButtonToButtonMappings({});
    SetDefaultWiiUAxisDirectionToButtonMappings({});
    SetDefaultWiiUAxisDirectionToAxisDirectionMappings({});
#endif
}

ControllerDefaultMappings::ControllerDefaultMappings()
    : ControllerDefaultMappings(
          std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<KbScancode>>(),
          std::unordered_map<StickIndex, std::vector<std::pair<Direction, KbScancode>>>(),
          std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<SDL_GamepadButton>>(),
          std::unordered_map<StickIndex, std::vector<std::pair<Direction, SDL_GamepadButton>>>(),
          std::unordered_map<CONTROLLERBUTTONS_T, std::vector<std::pair<SDL_GamepadAxis, int32_t>>>(),
          std::unordered_map<StickIndex, std::vector<std::pair<Direction, std::pair<SDL_GamepadAxis, int32_t>>>>()) {
}

ControllerDefaultMappings::~ControllerDefaultMappings() {
}

std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<KbScancode>>
ControllerDefaultMappings::GetDefaultKeyboardKeyToButtonMappings() {
    return mDefaultKeyboardKeyToButtonMappings;
}

void ControllerDefaultMappings::SetDefaultKeyboardKeyToButtonMappings(
    std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<KbScancode>> defaultKeyboardKeyToButtonMappings) {
    mDefaultKeyboardKeyToButtonMappings = defaultKeyboardKeyToButtonMappings;
}

std::unordered_map<StickIndex, std::vector<std::pair<Direction, KbScancode>>>
ControllerDefaultMappings::GetDefaultKeyboardKeyToAxisDirectionMappings() {
    return mDefaultKeyboardKeyToAxisDirectionMappings;
}

void ControllerDefaultMappings::SetDefaultKeyboardKeyToAxisDirectionMappings(
    std::unordered_map<StickIndex, std::vector<std::pair<Direction, KbScancode>>>
        defaultKeyboardKeyToAxisDirectionMappings) {
    if (!defaultKeyboardKeyToAxisDirectionMappings.empty()) {
        mDefaultKeyboardKeyToAxisDirectionMappings = defaultKeyboardKeyToAxisDirectionMappings;
        return;
    }

    mDefaultKeyboardKeyToAxisDirectionMappings[LEFT_STICK] = { { LEFT, KbScancode::LUS_KB_A },
                                                               { RIGHT, KbScancode::LUS_KB_D },
                                                               { UP, KbScancode::LUS_KB_W },
                                                               { DOWN, KbScancode::LUS_KB_S } };
}

std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<SDL_GamepadButton>>
ControllerDefaultMappings::GetDefaultSDLButtonToButtonMappings() {
    return mDefaultSDLButtonToButtonMappings;
}

void ControllerDefaultMappings::SetDefaultSDLButtonToButtonMappings(
    std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<SDL_GamepadButton>> defaultSDLButtonToButtonMappings) {
    mDefaultSDLButtonToButtonMappings = defaultSDLButtonToButtonMappings;
}

std::unordered_map<StickIndex, std::vector<std::pair<Direction, SDL_GamepadButton>>>
ControllerDefaultMappings::GetDefaultSDLButtonToAxisDirectionMappings() {
    return mDefaultSDLButtonToAxisDirectionMappings;
}

void ControllerDefaultMappings::SetDefaultSDLButtonToAxisDirectionMappings(
    std::unordered_map<StickIndex, std::vector<std::pair<Direction, SDL_GamepadButton>>>
        defaultSDLButtonToAxisDirectionMappings) {
    if (!defaultSDLButtonToAxisDirectionMappings.empty()) {
        mDefaultSDLButtonToAxisDirectionMappings = defaultSDLButtonToAxisDirectionMappings;
        return;
    }
}

std::unordered_map<CONTROLLERBUTTONS_T, std::vector<std::pair<SDL_GamepadAxis, int32_t>>>
ControllerDefaultMappings::GetDefaultSDLAxisDirectionToButtonMappings() {
    return mDefaultSDLAxisDirectionToButtonMappings;
}

void ControllerDefaultMappings::SetDefaultSDLAxisDirectionToButtonMappings(
    std::unordered_map<CONTROLLERBUTTONS_T, std::vector<std::pair<SDL_GamepadAxis, int32_t>>>
        defaultSDLAxisDirectionToButtonMappings) {
    mDefaultSDLAxisDirectionToButtonMappings = defaultSDLAxisDirectionToButtonMappings;
}

std::unordered_map<StickIndex, std::vector<std::pair<Direction, std::pair<SDL_GamepadAxis, int32_t>>>>
ControllerDefaultMappings::GetDefaultSDLAxisDirectionToAxisDirectionMappings() {
    return mDefaultSDLAxisDirectionToAxisDirectionMappings;
}

void ControllerDefaultMappings::SetDefaultSDLAxisDirectionToAxisDirectionMappings(
    std::unordered_map<StickIndex, std::vector<std::pair<Direction, std::pair<SDL_GamepadAxis, int32_t>>>>
        defaultSDLAxisDirectionToAxisDirectionMappings) {
    if (!defaultSDLAxisDirectionToAxisDirectionMappings.empty()) {
        mDefaultSDLAxisDirectionToAxisDirectionMappings = defaultSDLAxisDirectionToAxisDirectionMappings;
        return;
    }

#ifndef __WIIU__
    mDefaultSDLAxisDirectionToAxisDirectionMappings[LEFT_STICK] = {
        { LEFT, { SDL_GAMEPAD_AXIS_LEFTX, -1 } },
        { RIGHT, { SDL_GAMEPAD_AXIS_LEFTX, 1 } },
        { UP, { SDL_GAMEPAD_AXIS_LEFTY, -1 } },
        { DOWN, { SDL_GAMEPAD_AXIS_LEFTY, 1 } },
    };

    mDefaultSDLAxisDirectionToAxisDirectionMappings[RIGHT_STICK] = {
        { LEFT, { SDL_GAMEPAD_AXIS_RIGHTX, -1 } },
        { RIGHT, { SDL_GAMEPAD_AXIS_RIGHTX, 1 } },
        { UP, { SDL_GAMEPAD_AXIS_RIGHTY, -1 } },
        { DOWN, { SDL_GAMEPAD_AXIS_RIGHTY, 1 } },
    };
#endif
}

#ifdef __WIIU__
std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<uint32_t>>
ControllerDefaultMappings::GetDefaultWiiUButtonToButtonMappings() {
    return mDefaultWiiUButtonToButtonMappings;
}

void ControllerDefaultMappings::SetDefaultWiiUButtonToButtonMappings(
    std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<uint32_t>> defaultWiiUButtonToButtonMappings) {
    if (!defaultWiiUButtonToButtonMappings.empty()) {
        mDefaultWiiUButtonToButtonMappings = defaultWiiUButtonToButtonMappings;
        return;
    }

    // Several Wii U buttons can back one N64 button so that a single table covers every
    // device: the shoulder entries drive the GamePad and the Pro / Classic Controllers,
    // while "1" and the nunchuk's "Z" give a bare Wii Remote a working Z button. A device
    // that lacks a button simply never reports it as held.
    mDefaultWiiUButtonToButtonMappings = {
        { BTN_A, { WiiU::WIIU_BUTTON_A } },
        { BTN_B, { WiiU::WIIU_BUTTON_B } },
        { BTN_Z, { WiiU::WIIU_BUTTON_ZL, WiiU::WIIU_BUTTON_ZR, WiiU::WIIU_BUTTON_Z, WiiU::WIIU_BUTTON_ONE } },
        { BTN_L, { WiiU::WIIU_BUTTON_L } },
        { BTN_R, { WiiU::WIIU_BUTTON_R } },
        { BTN_START, { WiiU::WIIU_BUTTON_PLUS } },
        { BTN_DUP, { WiiU::WIIU_BUTTON_UP } },
        { BTN_DDOWN, { WiiU::WIIU_BUTTON_DOWN } },
        { BTN_DLEFT, { WiiU::WIIU_BUTTON_LEFT } },
        { BTN_DRIGHT, { WiiU::WIIU_BUTTON_RIGHT } },
    };
}

std::unordered_map<CONTROLLERBUTTONS_T, std::vector<std::pair<int32_t, int32_t>>>
ControllerDefaultMappings::GetDefaultWiiUAxisDirectionToButtonMappings() {
    return mDefaultWiiUAxisDirectionToButtonMappings;
}

void ControllerDefaultMappings::SetDefaultWiiUAxisDirectionToButtonMappings(
    std::unordered_map<CONTROLLERBUTTONS_T, std::vector<std::pair<int32_t, int32_t>>>
        defaultWiiUAxisDirectionToButtonMappings) {
    if (!defaultWiiUAxisDirectionToButtonMappings.empty()) {
        mDefaultWiiUAxisDirectionToButtonMappings = defaultWiiUAxisDirectionToButtonMappings;
        return;
    }

    // The N64 C buttons sit on the right stick, as they do on every modern pad. The Wii U
    // reports sticks with up positive, so C-up is the positive half of the Y axis.
    mDefaultWiiUAxisDirectionToButtonMappings = {
        { BTN_CUP, { { WiiU::WIIU_AXIS_RIGHT_Y, POSITIVE } } },
        { BTN_CDOWN, { { WiiU::WIIU_AXIS_RIGHT_Y, NEGATIVE } } },
        { BTN_CLEFT, { { WiiU::WIIU_AXIS_RIGHT_X, NEGATIVE } } },
        { BTN_CRIGHT, { { WiiU::WIIU_AXIS_RIGHT_X, POSITIVE } } },
    };
}

std::unordered_map<StickIndex, std::vector<std::pair<Direction, std::pair<int32_t, int32_t>>>>
ControllerDefaultMappings::GetDefaultWiiUAxisDirectionToAxisDirectionMappings() {
    return mDefaultWiiUAxisDirectionToAxisDirectionMappings;
}

void ControllerDefaultMappings::SetDefaultWiiUAxisDirectionToAxisDirectionMappings(
    std::unordered_map<StickIndex, std::vector<std::pair<Direction, std::pair<int32_t, int32_t>>>>
        defaultWiiUAxisDirectionToAxisDirectionMappings) {
    if (!defaultWiiUAxisDirectionToAxisDirectionMappings.empty()) {
        mDefaultWiiUAxisDirectionToAxisDirectionMappings = defaultWiiUAxisDirectionToAxisDirectionMappings;
        return;
    }

    mDefaultWiiUAxisDirectionToAxisDirectionMappings[LEFT_STICK] = {
        { LEFT, { WiiU::WIIU_AXIS_LEFT_X, NEGATIVE } },
        { RIGHT, { WiiU::WIIU_AXIS_LEFT_X, POSITIVE } },
        { UP, { WiiU::WIIU_AXIS_LEFT_Y, POSITIVE } },
        { DOWN, { WiiU::WIIU_AXIS_LEFT_Y, NEGATIVE } },
    };

    mDefaultWiiUAxisDirectionToAxisDirectionMappings[RIGHT_STICK] = {
        { LEFT, { WiiU::WIIU_AXIS_RIGHT_X, NEGATIVE } },
        { RIGHT, { WiiU::WIIU_AXIS_RIGHT_X, POSITIVE } },
        { UP, { WiiU::WIIU_AXIS_RIGHT_Y, POSITIVE } },
        { DOWN, { WiiU::WIIU_AXIS_RIGHT_Y, NEGATIVE } },
    };
}
#endif

} // namespace Ship
