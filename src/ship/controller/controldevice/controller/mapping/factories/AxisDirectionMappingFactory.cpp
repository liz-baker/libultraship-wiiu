#include "ship/controller/controldevice/controller/mapping/factories/AxisDirectionMappingFactory.h"
#include "ship/controller/controldevice/controller/mapping/keyboard/KeyboardKeyToAxisDirectionMapping.h"
#include "ship/controller/controldevice/controller/mapping/mouse/MouseButtonToAxisDirectionMapping.h"
#include "ship/controller/controldevice/controller/mapping/mouse/MouseWheelToAxisDirectionMapping.h"

#ifndef __WIIU__
#include "ship/controller/controldevice/controller/mapping/sdl/SDLButtonToAxisDirectionMapping.h"
#include "ship/controller/controldevice/controller/mapping/sdl/SDLAxisDirectionToAxisDirectionMapping.h"
#else
#include "ship/controller/controldevice/controller/mapping/wiiu/WiiUButtonToAxisDirectionMapping.h"
#include "ship/controller/controldevice/controller/mapping/wiiu/WiiUAxisDirectionToAxisDirectionMapping.h"
#endif

#include "ship/config/ConsoleVariable.h"
#include "ship/utils/StringHelper.h"
#include "ship/Context.h"

#include "ship/controller/controldevice/controller/mapping/keyboard/KeyboardScancodes.h"
#include "ship/controller/controldevice/controller/mapping/mouse/WheelHandler.h"

#include "ship/controller/controldeck/ControlDeck.h"

namespace Ship {
std::shared_ptr<ControllerAxisDirectionMapping>
AxisDirectionMappingFactory::CreateAxisDirectionMappingFromConfig(uint8_t portIndex, StickIndex stickIndex,
                                                                  std::string id) {
    const std::string mappingCvarKey = CVAR_PREFIX_CONTROLLERS ".AxisDirectionMappings." + id;
    const std::string mappingClass = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetString(
        StringHelper::Sprintf("%s.AxisDirectionMappingClass", mappingCvarKey.c_str()).c_str(), "");

#ifdef __WIIU__
    if (mappingClass == "WiiUAxisDirectionToAxisDirectionMapping") {
        int32_t direction = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
            StringHelper::Sprintf("%s.Direction", mappingCvarKey.c_str()).c_str(), -1);
        int32_t deviceIndex = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
            StringHelper::Sprintf("%s.WiiUDeviceIndex", mappingCvarKey.c_str()).c_str(), WIIU_DEVICE_GAMEPAD);
        int32_t wiiuAxis = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
            StringHelper::Sprintf("%s.WiiUAxis", mappingCvarKey.c_str()).c_str(), -1);
        int32_t axisDirection = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
            StringHelper::Sprintf("%s.AxisDirection", mappingCvarKey.c_str()).c_str(), 0);

        if ((direction != LEFT && direction != RIGHT && direction != UP && direction != DOWN) || wiiuAxis < 0 ||
            wiiuAxis >= WiiU::WIIU_AXIS_COUNT || (axisDirection != NEGATIVE && axisDirection != POSITIVE)) {
            Ship::Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(mappingCvarKey.c_str());
            Ship::Context::GetRawInstance()->GetConsoleVariables()->Save();
            return nullptr;
        }

        return std::make_shared<WiiUAxisDirectionToAxisDirectionMapping>(
            portIndex, stickIndex, static_cast<Direction>(direction), deviceIndex, wiiuAxis, axisDirection);
    }

    if (mappingClass == "WiiUButtonToAxisDirectionMapping") {
        int32_t direction = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
            StringHelper::Sprintf("%s.Direction", mappingCvarKey.c_str()).c_str(), -1);
        int32_t deviceIndex = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
            StringHelper::Sprintf("%s.WiiUDeviceIndex", mappingCvarKey.c_str()).c_str(), WIIU_DEVICE_GAMEPAD);
        int32_t wiiuButton = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
            StringHelper::Sprintf("%s.WiiUButton", mappingCvarKey.c_str()).c_str(), 0);

        if ((direction != LEFT && direction != RIGHT && direction != UP && direction != DOWN) || wiiuButton == 0) {
            Ship::Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(mappingCvarKey.c_str());
            Ship::Context::GetRawInstance()->GetConsoleVariables()->Save();
            return nullptr;
        }

        return std::make_shared<WiiUButtonToAxisDirectionMapping>(
            portIndex, stickIndex, static_cast<Direction>(direction), deviceIndex, static_cast<uint32_t>(wiiuButton));
    }
#else
    if (mappingClass == "SDLAxisDirectionToAxisDirectionMapping") {
        int32_t direction = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
            StringHelper::Sprintf("%s.Direction", mappingCvarKey.c_str()).c_str(), -1);
        int32_t sdlControllerAxis = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
            StringHelper::Sprintf("%s.SDLControllerAxis", mappingCvarKey.c_str()).c_str(), -1);
        int32_t axisDirection = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
            StringHelper::Sprintf("%s.AxisDirection", mappingCvarKey.c_str()).c_str(), 0);

        if ((direction != LEFT && direction != RIGHT && direction != UP && direction != DOWN) ||
            sdlControllerAxis == -1 || (axisDirection != NEGATIVE && axisDirection != POSITIVE)) {
            // something about this mapping is invalid
            Ship::Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(mappingCvarKey.c_str());
            Ship::Context::GetRawInstance()->GetConsoleVariables()->Save();
            return nullptr;
        }

        return std::make_shared<SDLAxisDirectionToAxisDirectionMapping>(
            portIndex, stickIndex, static_cast<Direction>(direction), sdlControllerAxis, axisDirection);
    }

    if (mappingClass == "SDLButtonToAxisDirectionMapping") {
        int32_t direction = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
            StringHelper::Sprintf("%s.Direction", mappingCvarKey.c_str()).c_str(), -1);
        int32_t sdlControllerButton = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
            StringHelper::Sprintf("%s.SDLControllerButton", mappingCvarKey.c_str()).c_str(), -1);

        if ((direction != LEFT && direction != RIGHT && direction != UP && direction != DOWN) ||
            sdlControllerButton == -1) {
            // something about this mapping is invalid
            Ship::Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(mappingCvarKey.c_str());
            Ship::Context::GetRawInstance()->GetConsoleVariables()->Save();
            return nullptr;
        }

        return std::make_shared<SDLButtonToAxisDirectionMapping>(
            portIndex, stickIndex, static_cast<Direction>(direction), sdlControllerButton);
    }

#endif

    if (mappingClass == "KeyboardKeyToAxisDirectionMapping") {
        int32_t direction = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
            StringHelper::Sprintf("%s.Direction", mappingCvarKey.c_str()).c_str(), -1);
        int32_t scancode = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
            StringHelper::Sprintf("%s.KeyboardScancode", mappingCvarKey.c_str()).c_str(), 0);

        if (direction != LEFT && direction != RIGHT && direction != UP && direction != DOWN) {
            // something about this mapping is invalid
            Ship::Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(mappingCvarKey.c_str());
            Ship::Context::GetRawInstance()->GetConsoleVariables()->Save();
            return nullptr;
        }

        return std::make_shared<KeyboardKeyToAxisDirectionMapping>(
            portIndex, stickIndex, static_cast<Direction>(direction), static_cast<KbScancode>(scancode));
    }

    if (mappingClass == "MouseButtonToAxisDirectionMapping") {
        int32_t direction = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
            StringHelper::Sprintf("%s.Direction", mappingCvarKey.c_str()).c_str(), -1);
        int mouseButton = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
            StringHelper::Sprintf("%s.MouseButton", mappingCvarKey.c_str()).c_str(), 0);

        if (direction != LEFT && direction != RIGHT && direction != UP && direction != DOWN) {
            // something about this mapping is invalid
            Ship::Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(mappingCvarKey.c_str());
            Ship::Context::GetRawInstance()->GetConsoleVariables()->Save();
            return nullptr;
        }

        return std::make_shared<MouseButtonToAxisDirectionMapping>(
            portIndex, stickIndex, static_cast<Direction>(direction), static_cast<MouseBtn>(mouseButton));
    }

    if (mappingClass == "MouseWheelToAxisDirectionMapping") {
        int32_t direction = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
            StringHelper::Sprintf("%s.Direction", mappingCvarKey.c_str()).c_str(), -1);
        int wheelDirection = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
            StringHelper::Sprintf("%s.WheelDirection", mappingCvarKey.c_str()).c_str(), 0);

        if (direction != LEFT && direction != RIGHT && direction != UP && direction != DOWN) {
            // something about this mapping is invalid
            Ship::Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(mappingCvarKey.c_str());
            Ship::Context::GetRawInstance()->GetConsoleVariables()->Save();
            return nullptr;
        }

        return std::make_shared<MouseWheelToAxisDirectionMapping>(
            portIndex, stickIndex, static_cast<Direction>(direction), static_cast<WheelDirection>(wheelDirection));
    }

    return nullptr;
}

std::vector<std::shared_ptr<ControllerAxisDirectionMapping>>
AxisDirectionMappingFactory::CreateDefaultKeyboardAxisDirectionMappings(uint8_t portIndex, StickIndex stickIndex) {
    std::vector<std::shared_ptr<ControllerAxisDirectionMapping>> mappings;

    auto defaultsForStick = Context::GetRawInstance()
                                ->GetControlDeck()
                                ->GetControllerDefaultMappings()
                                ->GetDefaultKeyboardKeyToAxisDirectionMappings()[stickIndex];

    for (const auto& [direction, scancode] : defaultsForStick) {
        mappings.push_back(
            std::make_shared<KeyboardKeyToAxisDirectionMapping>(portIndex, stickIndex, direction, scancode));
    }

    return mappings;
}

std::vector<std::shared_ptr<ControllerAxisDirectionMapping>>
AxisDirectionMappingFactory::CreateDefaultSDLAxisDirectionMappings(uint8_t portIndex, StickIndex stickIndex) {
    std::vector<std::shared_ptr<ControllerAxisDirectionMapping>> mappings;

#ifdef __WIIU__
    auto defaultWiiUAxisDirectionsForStick = Context::GetRawInstance()
                                                 ->GetControlDeck()
                                                 ->GetControllerDefaultMappings()
                                                 ->GetDefaultWiiUAxisDirectionToAxisDirectionMappings()[stickIndex];

    for (const auto& deviceIndex : WiiUDefaultDevicesForPort(portIndex)) {
        for (const auto& [direction, wiiuAxisDirection] : defaultWiiUAxisDirectionsForStick) {
            auto [wiiuAxis, wiiuDirection] = wiiuAxisDirection;
            mappings.push_back(std::make_shared<WiiUAxisDirectionToAxisDirectionMapping>(
                portIndex, stickIndex, direction, deviceIndex, wiiuAxis, wiiuDirection));
        }
    }
#else
    auto defaultButtonsForStick = Context::GetRawInstance()
                                      ->GetControlDeck()
                                      ->GetControllerDefaultMappings()
                                      ->GetDefaultSDLButtonToAxisDirectionMappings()[stickIndex];

    for (const auto& [direction, sdlGamepadButton] : defaultButtonsForStick) {
        mappings.push_back(
            std::make_shared<SDLButtonToAxisDirectionMapping>(portIndex, stickIndex, direction, sdlGamepadButton));
    }

    auto defaultAxisDirectionsForStick = Context::GetRawInstance()
                                             ->GetControlDeck()
                                             ->GetControllerDefaultMappings()
                                             ->GetDefaultSDLAxisDirectionToAxisDirectionMappings()[stickIndex];

    for (const auto& [direction, sdlGamepadAxisDirection] : defaultAxisDirectionsForStick) {
        auto [sdlGamepadAxis, sdlGamepadDirection] = sdlGamepadAxisDirection;
        mappings.push_back(std::make_shared<SDLAxisDirectionToAxisDirectionMapping>(
            portIndex, stickIndex, direction, sdlGamepadAxis, sdlGamepadDirection));
    }
#endif

    return mappings;
}

std::shared_ptr<ControllerAxisDirectionMapping>
AxisDirectionMappingFactory::CreateAxisDirectionMappingFromSDLInput(uint8_t portIndex, StickIndex stickIndex,
                                                                    Direction direction) {
    std::shared_ptr<ControllerAxisDirectionMapping> mapping = nullptr;

#ifdef __WIIU__
    for (const auto& deviceIndex : WiiU::GetConnectedDeviceIndices()) {
        const uint32_t held = WiiU::GetButtonsHeld(deviceIndex);
        if (held != 0) {
            // Take the lowest held bit so a single press yields a single mapping.
            const uint32_t button = held & (~held + 1);
            mapping = std::make_shared<WiiUButtonToAxisDirectionMapping>(portIndex, stickIndex, direction, deviceIndex,
                                                                         button);
            break;
        }

        for (int32_t axis = 0; axis < WiiU::WIIU_AXIS_COUNT; axis++) {
            const float axisValue = WiiU::GetAxisValue(deviceIndex, axis);
            int32_t axisDirection = 0;
            if (axisValue < -0.7f) {
                axisDirection = NEGATIVE;
            } else if (axisValue > 0.7f) {
                axisDirection = POSITIVE;
            }

            if (axisDirection == 0) {
                continue;
            }

            mapping = std::make_shared<WiiUAxisDirectionToAxisDirectionMapping>(portIndex, stickIndex, direction,
                                                                                deviceIndex, axis, axisDirection);
            break;
        }

        if (mapping != nullptr) {
            break;
        }
    }
#else
    for (auto [instanceId, gamepad] : Context::GetRawInstance()
                                          ->GetControlDeck()
                                          ->GetConnectedPhysicalDeviceManager()
                                          ->GetConnectedSDLGamepadsForPort(portIndex)) {
        for (int32_t button = SDL_CONTROLLER_BUTTON_A; button < SDL_CONTROLLER_BUTTON_MAX; button++) {
            if (SDL_GameControllerGetButton(gamepad, static_cast<SDL_GameControllerButton>(button))) {
                mapping = std::make_shared<SDLButtonToAxisDirectionMapping>(portIndex, stickIndex, direction, button);
                break;
            }
        }

        if (mapping != nullptr) {
            break;
        }

        for (int32_t i = SDL_CONTROLLER_AXIS_LEFTX; i < SDL_CONTROLLER_AXIS_MAX; i++) {
            const auto axis = static_cast<SDL_GameControllerAxis>(i);
            const auto axisValue = SDL_GameControllerGetAxis(gamepad, axis) / 32767.0f;
            int32_t axisDirection = 0;
            if (axisValue < -0.7f) {
                axisDirection = NEGATIVE;
            } else if (axisValue > 0.7f) {
                axisDirection = POSITIVE;
            }

            if (axisDirection == 0) {
                continue;
            }

            mapping = std::make_shared<SDLAxisDirectionToAxisDirectionMapping>(portIndex, stickIndex, direction, axis,
                                                                               axisDirection);
            break;
        }
    }
#endif

    return mapping;
}

std::shared_ptr<ControllerAxisDirectionMapping>
AxisDirectionMappingFactory::CreateAxisDirectionMappingFromMouseWheelInput(uint8_t portIndex, StickIndex stickIndex,
                                                                           Direction direction) {
    WheelDirections wheelDirections = WheelHandler::GetInstance()->GetDirections();
    WheelDirection wheelDirection;
    if (wheelDirections.X != LUS_WHEEL_NONE) {
        wheelDirection = wheelDirections.X;
    } else if (wheelDirections.Y != LUS_WHEEL_NONE) {
        wheelDirection = wheelDirections.Y;
    } else {
        return nullptr;
    }

    return std::make_shared<MouseWheelToAxisDirectionMapping>(portIndex, stickIndex, direction, wheelDirection);
}
} // namespace Ship
