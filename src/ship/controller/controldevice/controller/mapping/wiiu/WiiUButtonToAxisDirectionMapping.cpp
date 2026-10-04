#include "ship/controller/controldevice/controller/mapping/wiiu/WiiUButtonToAxisDirectionMapping.h"

#include "ship/utils/StringHelper.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/Context.h"

namespace Ship {
WiiUButtonToAxisDirectionMapping::WiiUButtonToAxisDirectionMapping(uint8_t portIndex, StickIndex stickIndex,
                                                                   Direction direction, int32_t deviceIndex,
                                                                   uint32_t wiiuButton)
    : ControllerInputMapping(PhysicalDeviceType::WiiUGamepad),
      ControllerAxisDirectionMapping(PhysicalDeviceType::WiiUGamepad, portIndex, stickIndex, direction),
      WiiUButtonToAnyMapping(deviceIndex, wiiuButton) {
}

float WiiUButtonToAxisDirectionMapping::GetNormalizedAxisDirectionValue() {
    if (Context::GetRawInstance()->GetControlDeck()->GamepadGameInputBlocked()) {
        return 0.0f;
    }

    return ButtonIsHeld() ? MAX_AXIS_RANGE : 0.0f;
}

std::string WiiUButtonToAxisDirectionMapping::GetAxisDirectionMappingId() {
    return StringHelper::Sprintf("P%d-S%d-D%d-WIIU%s-B%u", mPortIndex, mStickIndex, mDirection,
                                 WiiUDeviceToken(mDeviceIndex).c_str(), mButton);
}

int8_t WiiUButtonToAxisDirectionMapping::GetMappingType() {
    return MAPPING_TYPE_GAMEPAD;
}

void WiiUButtonToAxisDirectionMapping::SaveToConfig() {
    const std::string mappingCvarKey = CVAR_PREFIX_CONTROLLERS ".AxisDirectionMappings." + GetAxisDirectionMappingId();
    Context::GetRawInstance()->GetConsoleVariables()->SetString(
        StringHelper::Sprintf("%s.AxisDirectionMappingClass", mappingCvarKey.c_str()).c_str(),
        "WiiUButtonToAxisDirectionMapping");
    Context::GetRawInstance()->GetConsoleVariables()->SetInteger(
        StringHelper::Sprintf("%s.Stick", mappingCvarKey.c_str()).c_str(), mStickIndex);
    Context::GetRawInstance()->GetConsoleVariables()->SetInteger(
        StringHelper::Sprintf("%s.Direction", mappingCvarKey.c_str()).c_str(), mDirection);
    Context::GetRawInstance()->GetConsoleVariables()->SetInteger(
        StringHelper::Sprintf("%s.WiiUDeviceIndex", mappingCvarKey.c_str()).c_str(), mDeviceIndex);
    Context::GetRawInstance()->GetConsoleVariables()->SetInteger(
        StringHelper::Sprintf("%s.WiiUButton", mappingCvarKey.c_str()).c_str(), static_cast<int32_t>(mButton));
    Context::GetRawInstance()->GetConsoleVariables()->Save();
}

void WiiUButtonToAxisDirectionMapping::EraseFromConfig() {
    const std::string mappingCvarKey = CVAR_PREFIX_CONTROLLERS ".AxisDirectionMappings." + GetAxisDirectionMappingId();

    Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.AxisDirectionMappingClass", mappingCvarKey.c_str()).c_str());
    Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.Stick", mappingCvarKey.c_str()).c_str());
    Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.Direction", mappingCvarKey.c_str()).c_str());
    Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.WiiUDeviceIndex", mappingCvarKey.c_str()).c_str());
    Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.WiiUButton", mappingCvarKey.c_str()).c_str());
    Context::GetRawInstance()->GetConsoleVariables()->Save();
}

std::string WiiUButtonToAxisDirectionMapping::GetPhysicalDeviceName() {
    return WiiUButtonToAnyMapping::GetPhysicalDeviceName();
}

std::string WiiUButtonToAxisDirectionMapping::GetPhysicalInputName() {
    return WiiUButtonToAnyMapping::GetPhysicalInputName();
}
} // namespace Ship
