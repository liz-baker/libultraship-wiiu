#include "ship/controller/controldevice/controller/mapping/factories/RumbleMappingFactory.h"
#include "ship/controller/controldevice/controller/mapping/sdl/SDLRumbleMapping.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/utils/StringHelper.h"
#include "ship/controller/controldeck/ControlDeck.h"

#ifdef __WIIU__
#include "ship/controller/controldevice/controller/mapping/wiiu/WiiURumbleMapping.h"
#endif

namespace Ship {
std::shared_ptr<ControllerRumbleMapping>
RumbleMappingFactory::CreateRumbleMappingFromConfig(uint8_t portIndex, std::string id,
                                                    std::shared_ptr<ConsoleVariable> consoleVariable,
                                                    std::shared_ptr<ControlDeck> controlDeck) {
    const std::string mappingCvarKey = CVAR_PREFIX_CONTROLLERS ".RumbleMappings." + id;
    const std::string mappingClass =
        consoleVariable->GetString(StringHelper::Sprintf("%s.RumbleMappingClass", mappingCvarKey.c_str()).c_str(), "");

    int32_t lowFrequencyIntensityPercentage = consoleVariable->GetInteger(
        StringHelper::Sprintf("%s.LowFrequencyIntensity", mappingCvarKey.c_str()).c_str(), -1);
    int32_t highFrequencyIntensityPercentage = consoleVariable->GetInteger(
        StringHelper::Sprintf("%s.HighFrequencyIntensity", mappingCvarKey.c_str()).c_str(), -1);

    if (lowFrequencyIntensityPercentage < 0 || lowFrequencyIntensityPercentage > 100 ||
        highFrequencyIntensityPercentage < 0 || highFrequencyIntensityPercentage > 100) {
        consoleVariable->ClearVariable(mappingCvarKey.c_str());
        consoleVariable->Save();
        return nullptr;
    }

#ifdef __WIIU__
    if (mappingClass == "WiiURumbleMapping") {
        int32_t deviceIndex = consoleVariable->GetInteger(
            StringHelper::Sprintf("%s.WiiUDeviceIndex", mappingCvarKey.c_str()).c_str(), WIIU_DEVICE_GAMEPAD);

        return std::make_shared<WiiURumbleMapping>(portIndex, deviceIndex, lowFrequencyIntensityPercentage,
                                                   highFrequencyIntensityPercentage, controlDeck, consoleVariable);
    }
#else
    if (mappingClass == "SDLRumbleMapping") {
        return std::make_shared<SDLRumbleMapping>(portIndex, lowFrequencyIntensityPercentage,
                                                  highFrequencyIntensityPercentage, controlDeck, consoleVariable);
    }
#endif

    return nullptr;
}

std::vector<std::shared_ptr<ControllerRumbleMapping>>
RumbleMappingFactory::CreateDefaultSDLRumbleMappings(PhysicalDeviceType physicalDeviceType, uint8_t portIndex,
                                                     std::shared_ptr<ConsoleVariable> consoleVariable,
                                                     std::shared_ptr<ControlDeck> controlDeck) {
    if (physicalDeviceType != PHYSICAL_DEVICE_TYPE_GAMEPAD) {
        return {};
    }

    std::vector<std::shared_ptr<ControllerRumbleMapping>> mappings;
#ifndef __WIIU__
    mappings.push_back(std::make_shared<SDLRumbleMapping>(portIndex, DEFAULT_LOW_FREQUENCY_RUMBLE_PERCENTAGE,
                                                          DEFAULT_HIGH_FREQUENCY_RUMBLE_PERCENTAGE, controlDeck,
                                                          consoleVariable));
#else
    for (const auto& deviceIndex : WiiUDefaultDevicesForPort(portIndex)) {
        mappings.push_back(std::make_shared<WiiURumbleMapping>(
            portIndex, deviceIndex, DEFAULT_LOW_FREQUENCY_RUMBLE_PERCENTAGE, DEFAULT_HIGH_FREQUENCY_RUMBLE_PERCENTAGE,
            controlDeck, consoleVariable));
    }
#endif

    return mappings;
}

std::shared_ptr<ControllerRumbleMapping> RumbleMappingFactory::CreateRumbleMappingFromSDLInput(
    uint8_t portIndex, std::shared_ptr<ConsoleVariable> consoleVariable, std::shared_ptr<ControlDeck> controlDeck) {
    std::shared_ptr<ControllerRumbleMapping> mapping = nullptr;

#ifndef __WIIU__
    for (auto [instanceId, gamepad] :
         controlDeck->GetConnectedPhysicalDeviceManager()->GetConnectedSDLGamepadsForPort(portIndex)) {
        if (!SDL_GetBooleanProperty(SDL_GetGamepadProperties(gamepad), SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false)) {
            continue;
        }

        for (int32_t button = SDL_GAMEPAD_BUTTON_SOUTH; button < SDL_GAMEPAD_BUTTON_COUNT; button++) {
            if (SDL_GetGamepadButton(gamepad, static_cast<SDL_GamepadButton>(button))) {
                mapping = std::make_shared<SDLRumbleMapping>(portIndex, DEFAULT_LOW_FREQUENCY_RUMBLE_PERCENTAGE,
                                                             DEFAULT_HIGH_FREQUENCY_RUMBLE_PERCENTAGE, controlDeck,
                                                             consoleVariable);
                break;
            }
        }

        if (mapping != nullptr) {
            break;
        }

        for (int32_t i = SDL_GAMEPAD_AXIS_LEFTX; i < SDL_GAMEPAD_AXIS_COUNT; i++) {
            const auto axis = static_cast<SDL_GamepadAxis>(i);
            const auto axisValue = SDL_GetGamepadAxis(gamepad, axis) / 32767.0f;
            int32_t axisDirection = 0;
            if (axisValue < -0.7f) {
                axisDirection = NEGATIVE;
            } else if (axisValue > 0.7f) {
                axisDirection = POSITIVE;
            }

            if (axisDirection == 0) {
                continue;
            }

            mapping = std::make_shared<SDLRumbleMapping>(portIndex, DEFAULT_LOW_FREQUENCY_RUMBLE_PERCENTAGE,
                                                         DEFAULT_HIGH_FREQUENCY_RUMBLE_PERCENTAGE, controlDeck,
                                                         consoleVariable);
            break;
        }
    }
#else
    for (const auto& deviceIndex : WiiU::GetConnectedDeviceIndices()) {
        if (!WiiU::DeviceSupportsRumble(deviceIndex) || WiiU::GetButtonsHeld(deviceIndex) == 0) {
            continue;
        }

        mapping =
            std::make_shared<WiiURumbleMapping>(portIndex, deviceIndex, DEFAULT_LOW_FREQUENCY_RUMBLE_PERCENTAGE,
                                                DEFAULT_HIGH_FREQUENCY_RUMBLE_PERCENTAGE, controlDeck, consoleVariable);
        break;
    }
#endif

    return mapping;
}
} // namespace Ship
