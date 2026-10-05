#include "ship/controller/controldevice/controller/mapping/factories/RumbleMappingFactory.h"
#ifndef __WIIU__
#include "ship/controller/controldevice/controller/mapping/sdl/SDLRumbleMapping.h"
#else
#include "ship/controller/controldevice/controller/mapping/wiiu/WiiURumbleMapping.h"
#endif
#include "ship/config/ConsoleVariable.h"
#include "ship/utils/StringHelper.h"
#include "ship/Context.h"
#include "ship/controller/controldeck/ControlDeck.h"

namespace Ship {
std::shared_ptr<ControllerRumbleMapping> RumbleMappingFactory::CreateRumbleMappingFromConfig(uint8_t portIndex,
                                                                                             std::string id) {
    const std::string mappingCvarKey = CVAR_PREFIX_CONTROLLERS ".RumbleMappings." + id;
    const std::string mappingClass = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetString(
        StringHelper::Sprintf("%s.RumbleMappingClass", mappingCvarKey.c_str()).c_str(), "");

    int32_t lowFrequencyIntensityPercentage = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
        StringHelper::Sprintf("%s.LowFrequencyIntensity", mappingCvarKey.c_str()).c_str(), -1);
    int32_t highFrequencyIntensityPercentage = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
        StringHelper::Sprintf("%s.HighFrequencyIntensity", mappingCvarKey.c_str()).c_str(), -1);

    if (lowFrequencyIntensityPercentage < 0 || lowFrequencyIntensityPercentage > 100 ||
        highFrequencyIntensityPercentage < 0 || highFrequencyIntensityPercentage > 100) {
        // something about this mapping is invalid
        Ship::Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(mappingCvarKey.c_str());
        Ship::Context::GetRawInstance()->GetConsoleVariables()->Save();
        return nullptr;
    }

#ifdef __WIIU__
    if (mappingClass == "WiiURumbleMapping") {
        int32_t deviceIndex = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
            StringHelper::Sprintf("%s.WiiUDeviceIndex", mappingCvarKey.c_str()).c_str(), WIIU_DEVICE_GAMEPAD);

        return std::make_shared<WiiURumbleMapping>(portIndex, deviceIndex, lowFrequencyIntensityPercentage,
                                                   highFrequencyIntensityPercentage);
    }
#else
    if (mappingClass == "SDLRumbleMapping") {
        return std::make_shared<SDLRumbleMapping>(portIndex, lowFrequencyIntensityPercentage,
                                                  highFrequencyIntensityPercentage);
    }
#endif

    return nullptr;
}

std::vector<std::shared_ptr<ControllerRumbleMapping>>
RumbleMappingFactory::CreateDefaultSDLRumbleMappings(PhysicalDeviceType physicalDeviceType, uint8_t portIndex) {
    std::vector<std::shared_ptr<ControllerRumbleMapping>> mappings;
#ifndef __WIIU__
    mappings.push_back(std::make_shared<SDLRumbleMapping>(portIndex, DEFAULT_LOW_FREQUENCY_RUMBLE_PERCENTAGE,
                                                          DEFAULT_HIGH_FREQUENCY_RUMBLE_PERCENTAGE));
#else
    for (const auto& deviceIndex : WiiUDefaultDevicesForPort(portIndex)) {
        mappings.push_back(std::make_shared<WiiURumbleMapping>(
            portIndex, deviceIndex, DEFAULT_LOW_FREQUENCY_RUMBLE_PERCENTAGE, DEFAULT_HIGH_FREQUENCY_RUMBLE_PERCENTAGE));
    }
#endif

    return mappings;
}

std::shared_ptr<ControllerRumbleMapping> RumbleMappingFactory::CreateRumbleMappingFromSDLInput(uint8_t portIndex) {
    std::shared_ptr<ControllerRumbleMapping> mapping = nullptr;

#ifdef __WIIU__
    for (const auto& deviceIndex : WiiU::GetConnectedDeviceIndices()) {
        if (!WiiU::DeviceSupportsRumble(deviceIndex) || WiiU::GetButtonsHeld(deviceIndex) == 0) {
            continue;
        }

        mapping = std::make_shared<WiiURumbleMapping>(portIndex, deviceIndex, DEFAULT_LOW_FREQUENCY_RUMBLE_PERCENTAGE,
                                                      DEFAULT_HIGH_FREQUENCY_RUMBLE_PERCENTAGE);
        break;
    }
#else
    for (auto [instanceId, gamepad] : Context::GetRawInstance()
                                          ->GetControlDeck()
                                          ->GetConnectedPhysicalDeviceManager()
                                          ->GetConnectedSDLGamepadsForPort(portIndex)) {
        if (!SDL_GameControllerHasRumble(gamepad)) {
            continue;
        }

        for (int32_t button = SDL_CONTROLLER_BUTTON_A; button < SDL_CONTROLLER_BUTTON_MAX; button++) {
            if (SDL_GameControllerGetButton(gamepad, static_cast<SDL_GameControllerButton>(button))) {
                mapping = std::make_shared<SDLRumbleMapping>(portIndex, DEFAULT_LOW_FREQUENCY_RUMBLE_PERCENTAGE,
                                                             DEFAULT_HIGH_FREQUENCY_RUMBLE_PERCENTAGE);
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

            mapping = std::make_shared<SDLRumbleMapping>(portIndex, DEFAULT_LOW_FREQUENCY_RUMBLE_PERCENTAGE,
                                                         DEFAULT_HIGH_FREQUENCY_RUMBLE_PERCENTAGE);
            break;
        }
    }
#endif

    return mapping;
}
} // namespace Ship
