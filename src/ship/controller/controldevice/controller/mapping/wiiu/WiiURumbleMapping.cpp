#include "ship/controller/controldevice/controller/mapping/wiiu/WiiURumbleMapping.h"

#include "ship/utils/StringHelper.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldeck/ControlDeck.h"

namespace Ship {
WiiURumbleMapping::WiiURumbleMapping(uint8_t portIndex, int32_t deviceIndex, uint8_t lowFrequencyIntensityPercentage,
                                     uint8_t highFrequencyIntensityPercentage, std::shared_ptr<ControlDeck> controlDeck,
                                     std::shared_ptr<ConsoleVariable> consoleVariable)
    : ControllerRumbleMapping(PhysicalDeviceType::WiiUGamepad, portIndex, lowFrequencyIntensityPercentage,
                              highFrequencyIntensityPercentage) {
    mDeviceIndex = deviceIndex;
    mConsoleVariable = std::move(consoleVariable);
    mControlDeck = std::move(controlDeck);
}

void WiiURumbleMapping::StartRumble() {
    // Wii U motors have no intensity control, so the percentages only decide whether
    // the motor runs at all.
    if (mLowFrequencyIntensityPercentage == 0 && mHighFrequencyIntensityPercentage == 0) {
        return;
    }

    WiiU::SetRumble(mDeviceIndex, true);
}

void WiiURumbleMapping::StopRumble() {
    WiiU::SetRumble(mDeviceIndex, false);
}

std::string WiiURumbleMapping::GetRumbleMappingId() {
    return StringHelper::Sprintf("P%d-WIIU%s", mPortIndex, WiiUDeviceToken(mDeviceIndex).c_str());
}

void WiiURumbleMapping::SaveToConfig() {
    const std::string mappingCvarKey = CVAR_PREFIX_CONTROLLERS ".RumbleMappings." + GetRumbleMappingId();
    mConsoleVariable->SetString(StringHelper::Sprintf("%s.RumbleMappingClass", mappingCvarKey.c_str()).c_str(),
                                "WiiURumbleMapping");
    mConsoleVariable->SetInteger(StringHelper::Sprintf("%s.WiiUDeviceIndex", mappingCvarKey.c_str()).c_str(),
                                 mDeviceIndex);
    mConsoleVariable->SetInteger(StringHelper::Sprintf("%s.LowFrequencyIntensity", mappingCvarKey.c_str()).c_str(),
                                 mLowFrequencyIntensityPercentage);
    mConsoleVariable->SetInteger(StringHelper::Sprintf("%s.HighFrequencyIntensity", mappingCvarKey.c_str()).c_str(),
                                 mHighFrequencyIntensityPercentage);
    mConsoleVariable->Save();
}

void WiiURumbleMapping::EraseFromConfig() {
    const std::string mappingCvarKey = CVAR_PREFIX_CONTROLLERS ".RumbleMappings." + GetRumbleMappingId();

    mConsoleVariable->ClearVariable(StringHelper::Sprintf("%s.RumbleMappingClass", mappingCvarKey.c_str()).c_str());
    mConsoleVariable->ClearVariable(StringHelper::Sprintf("%s.WiiUDeviceIndex", mappingCvarKey.c_str()).c_str());
    mConsoleVariable->ClearVariable(StringHelper::Sprintf("%s.LowFrequencyIntensity", mappingCvarKey.c_str()).c_str());
    mConsoleVariable->ClearVariable(StringHelper::Sprintf("%s.HighFrequencyIntensity", mappingCvarKey.c_str()).c_str());
    mConsoleVariable->Save();
}

std::string WiiURumbleMapping::GetPhysicalDeviceName() {
    return WiiU::GetDeviceName(mDeviceIndex);
}
} // namespace Ship
