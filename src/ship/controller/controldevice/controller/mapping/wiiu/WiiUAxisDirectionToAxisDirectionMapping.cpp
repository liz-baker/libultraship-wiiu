#include "ship/controller/controldevice/controller/mapping/wiiu/WiiUAxisDirectionToAxisDirectionMapping.h"

#include "ship/utils/StringHelper.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/Context.h"

namespace Ship {
WiiUAxisDirectionToAxisDirectionMapping::WiiUAxisDirectionToAxisDirectionMapping(uint8_t portIndex,
                                                                                 StickIndex stickIndex,
                                                                                 Direction direction,
                                                                                 int32_t deviceIndex, int32_t wiiuAxis,
                                                                                 int32_t axisDirection)
    : ControllerInputMapping(PhysicalDeviceType::WiiUGamepad),
      ControllerAxisDirectionMapping(PhysicalDeviceType::WiiUGamepad, portIndex, stickIndex, direction),
      WiiUAxisDirectionToAnyMapping(deviceIndex, wiiuAxis, axisDirection) {
}

float WiiUAxisDirectionToAxisDirectionMapping::GetNormalizedAxisDirectionValue() {
    if (Context::GetRawInstance()->GetControlDeck()->GamepadGameInputBlocked()) {
        return 0.0f;
    }

    // The Wii U reports sticks already normalized to {-1.0 ... +1.0}, so this only
    // has to scale the magnitude up to the N64 range.
    return GetAxisDirectionMagnitude() * MAX_AXIS_RANGE;
}

std::string WiiUAxisDirectionToAxisDirectionMapping::GetAxisDirectionMappingId() {
    return StringHelper::Sprintf("P%d-S%d-D%d-WIIU%s-A%d-AD%s", mPortIndex, mStickIndex, mDirection,
                                 WiiUDeviceToken(mDeviceIndex).c_str(), mAxis, mAxisDirection == POSITIVE ? "P" : "N");
}

int8_t WiiUAxisDirectionToAxisDirectionMapping::GetMappingType() {
    return MAPPING_TYPE_GAMEPAD;
}

void WiiUAxisDirectionToAxisDirectionMapping::SaveToConfig() {
    const std::string mappingCvarKey = CVAR_PREFIX_CONTROLLERS ".AxisDirectionMappings." + GetAxisDirectionMappingId();
    Context::GetRawInstance()->GetConsoleVariables()->SetString(
        StringHelper::Sprintf("%s.AxisDirectionMappingClass", mappingCvarKey.c_str()).c_str(),
        "WiiUAxisDirectionToAxisDirectionMapping");
    Context::GetRawInstance()->GetConsoleVariables()->SetInteger(
        StringHelper::Sprintf("%s.Stick", mappingCvarKey.c_str()).c_str(), mStickIndex);
    Context::GetRawInstance()->GetConsoleVariables()->SetInteger(
        StringHelper::Sprintf("%s.Direction", mappingCvarKey.c_str()).c_str(), mDirection);
    Context::GetRawInstance()->GetConsoleVariables()->SetInteger(
        StringHelper::Sprintf("%s.WiiUDeviceIndex", mappingCvarKey.c_str()).c_str(), mDeviceIndex);
    Context::GetRawInstance()->GetConsoleVariables()->SetInteger(
        StringHelper::Sprintf("%s.WiiUAxis", mappingCvarKey.c_str()).c_str(), mAxis);
    Context::GetRawInstance()->GetConsoleVariables()->SetInteger(
        StringHelper::Sprintf("%s.AxisDirection", mappingCvarKey.c_str()).c_str(), mAxisDirection);
    Context::GetRawInstance()->GetConsoleVariables()->Save();
}

void WiiUAxisDirectionToAxisDirectionMapping::EraseFromConfig() {
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
        StringHelper::Sprintf("%s.WiiUAxis", mappingCvarKey.c_str()).c_str());
    Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.AxisDirection", mappingCvarKey.c_str()).c_str());
    Context::GetRawInstance()->GetConsoleVariables()->Save();
}

std::string WiiUAxisDirectionToAxisDirectionMapping::GetPhysicalDeviceName() {
    return WiiUAxisDirectionToAnyMapping::GetPhysicalDeviceName();
}

std::string WiiUAxisDirectionToAxisDirectionMapping::GetPhysicalInputName() {
    return WiiUAxisDirectionToAnyMapping::GetPhysicalInputName();
}
} // namespace Ship
