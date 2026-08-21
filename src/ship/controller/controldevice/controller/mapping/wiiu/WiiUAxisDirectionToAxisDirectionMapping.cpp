#include "ship/controller/controldevice/controller/mapping/wiiu/WiiUAxisDirectionToAxisDirectionMapping.h"

#include "ship/utils/StringHelper.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldeck/ControlDeck.h"

namespace Ship {
WiiUAxisDirectionToAxisDirectionMapping::WiiUAxisDirectionToAxisDirectionMapping(
    uint8_t portIndex, StickIndex stickIndex, Direction direction, int32_t deviceIndex, int32_t wiiuAxis,
    int32_t axisDirection, std::shared_ptr<ControlDeck> controlDeck, std::shared_ptr<ConsoleVariable> consoleVariable)
    : ControllerInputMapping(PhysicalDeviceType::WiiUGamepad),
      ControllerAxisDirectionMapping(PhysicalDeviceType::WiiUGamepad, portIndex, stickIndex, direction, controlDeck),
      WiiUAxisDirectionToAnyMapping(deviceIndex, wiiuAxis, axisDirection) {
    mConsoleVariable = std::move(consoleVariable);
    mControlDeck = std::move(controlDeck);
}

float WiiUAxisDirectionToAxisDirectionMapping::GetNormalizedAxisDirectionValue() {
    if (mControlDeck->GamepadGameInputBlocked()) {
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
    mConsoleVariable->SetString(StringHelper::Sprintf("%s.AxisDirectionMappingClass", mappingCvarKey.c_str()).c_str(),
                                "WiiUAxisDirectionToAxisDirectionMapping");
    mConsoleVariable->SetInteger(StringHelper::Sprintf("%s.Stick", mappingCvarKey.c_str()).c_str(), mStickIndex);
    mConsoleVariable->SetInteger(StringHelper::Sprintf("%s.Direction", mappingCvarKey.c_str()).c_str(), mDirection);
    mConsoleVariable->SetInteger(StringHelper::Sprintf("%s.WiiUDeviceIndex", mappingCvarKey.c_str()).c_str(),
                                 mDeviceIndex);
    mConsoleVariable->SetInteger(StringHelper::Sprintf("%s.WiiUAxis", mappingCvarKey.c_str()).c_str(), mAxis);
    mConsoleVariable->SetInteger(StringHelper::Sprintf("%s.AxisDirection", mappingCvarKey.c_str()).c_str(),
                                 mAxisDirection);
    mConsoleVariable->Save();
}

void WiiUAxisDirectionToAxisDirectionMapping::EraseFromConfig() {
    const std::string mappingCvarKey = CVAR_PREFIX_CONTROLLERS ".AxisDirectionMappings." + GetAxisDirectionMappingId();

    mConsoleVariable->ClearVariable(
        StringHelper::Sprintf("%s.AxisDirectionMappingClass", mappingCvarKey.c_str()).c_str());
    mConsoleVariable->ClearVariable(StringHelper::Sprintf("%s.Stick", mappingCvarKey.c_str()).c_str());
    mConsoleVariable->ClearVariable(StringHelper::Sprintf("%s.Direction", mappingCvarKey.c_str()).c_str());
    mConsoleVariable->ClearVariable(StringHelper::Sprintf("%s.WiiUDeviceIndex", mappingCvarKey.c_str()).c_str());
    mConsoleVariable->ClearVariable(StringHelper::Sprintf("%s.WiiUAxis", mappingCvarKey.c_str()).c_str());
    mConsoleVariable->ClearVariable(StringHelper::Sprintf("%s.AxisDirection", mappingCvarKey.c_str()).c_str());
    mConsoleVariable->Save();
}

std::string WiiUAxisDirectionToAxisDirectionMapping::GetPhysicalDeviceName() {
    return WiiUAxisDirectionToAnyMapping::GetPhysicalDeviceName();
}

std::string WiiUAxisDirectionToAxisDirectionMapping::GetPhysicalInputName() {
    return WiiUAxisDirectionToAnyMapping::GetPhysicalInputName();
}
} // namespace Ship
