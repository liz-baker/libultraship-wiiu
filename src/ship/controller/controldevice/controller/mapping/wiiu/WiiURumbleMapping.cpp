#include "ship/controller/controldevice/controller/mapping/wiiu/WiiURumbleMapping.h"

#include "ship/utils/StringHelper.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/Context.h"

namespace Ship {
WiiURumbleMapping::WiiURumbleMapping(uint8_t portIndex, int32_t deviceIndex, uint8_t lowFrequencyIntensityPercentage,
                                     uint8_t highFrequencyIntensityPercentage)
    : ControllerRumbleMapping(PhysicalDeviceType::WiiUGamepad, portIndex, lowFrequencyIntensityPercentage,
                              highFrequencyIntensityPercentage) {
    mDeviceIndex = deviceIndex;
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
    Context::GetRawInstance()->GetConsoleVariables()->SetString(
        StringHelper::Sprintf("%s.RumbleMappingClass", mappingCvarKey.c_str()).c_str(), "WiiURumbleMapping");
    Context::GetRawInstance()->GetConsoleVariables()->SetInteger(
        StringHelper::Sprintf("%s.WiiUDeviceIndex", mappingCvarKey.c_str()).c_str(), mDeviceIndex);
    Context::GetRawInstance()->GetConsoleVariables()->SetInteger(
        StringHelper::Sprintf("%s.LowFrequencyIntensity", mappingCvarKey.c_str()).c_str(),
        mLowFrequencyIntensityPercentage);
    Context::GetRawInstance()->GetConsoleVariables()->SetInteger(
        StringHelper::Sprintf("%s.HighFrequencyIntensity", mappingCvarKey.c_str()).c_str(),
        mHighFrequencyIntensityPercentage);
    Context::GetRawInstance()->GetConsoleVariables()->Save();
}

void WiiURumbleMapping::EraseFromConfig() {
    const std::string mappingCvarKey = CVAR_PREFIX_CONTROLLERS ".RumbleMappings." + GetRumbleMappingId();

    Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.RumbleMappingClass", mappingCvarKey.c_str()).c_str());
    Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.WiiUDeviceIndex", mappingCvarKey.c_str()).c_str());
    Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.LowFrequencyIntensity", mappingCvarKey.c_str()).c_str());
    Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.HighFrequencyIntensity", mappingCvarKey.c_str()).c_str());
    Context::GetRawInstance()->GetConsoleVariables()->Save();
}

std::string WiiURumbleMapping::GetPhysicalDeviceName() {
    return WiiU::GetDeviceName(mDeviceIndex);
}
} // namespace Ship
