#include "ship/controller/controldevice/controller/mapping/wiiu/WiiUButtonToAxisDirectionMapping.h"

#include "ship/utils/StringHelper.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldeck/ControlDeck.h"

namespace Ship {
WiiUButtonToAxisDirectionMapping::WiiUButtonToAxisDirectionMapping(uint8_t portIndex, StickIndex stickIndex,
                                                                   Direction direction, int32_t deviceIndex,
                                                                   uint32_t wiiuButton,
                                                                   std::shared_ptr<ControlDeck> controlDeck,
                                                                   std::shared_ptr<ConsoleVariable> consoleVariable)
    : ControllerInputMapping(PhysicalDeviceType::WiiUGamepad),
      ControllerAxisDirectionMapping(PhysicalDeviceType::WiiUGamepad, portIndex, stickIndex, direction, controlDeck),
      WiiUButtonToAnyMapping(deviceIndex, wiiuButton) {
    mConsoleVariable = std::move(consoleVariable);
    mControlDeck = std::move(controlDeck);
}

float WiiUButtonToAxisDirectionMapping::GetNormalizedAxisDirectionValue() {
    if (mControlDeck->GamepadGameInputBlocked()) {
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
    mConsoleVariable->SetString(StringHelper::Sprintf("%s.AxisDirectionMappingClass", mappingCvarKey.c_str()).c_str(),
                                "WiiUButtonToAxisDirectionMapping");
    mConsoleVariable->SetInteger(StringHelper::Sprintf("%s.Stick", mappingCvarKey.c_str()).c_str(), mStickIndex);
    mConsoleVariable->SetInteger(StringHelper::Sprintf("%s.Direction", mappingCvarKey.c_str()).c_str(), mDirection);
    mConsoleVariable->SetInteger(StringHelper::Sprintf("%s.WiiUDeviceIndex", mappingCvarKey.c_str()).c_str(),
                                 mDeviceIndex);
    mConsoleVariable->SetInteger(StringHelper::Sprintf("%s.WiiUButton", mappingCvarKey.c_str()).c_str(),
                                 static_cast<int32_t>(mButton));
    mConsoleVariable->Save();
}

void WiiUButtonToAxisDirectionMapping::EraseFromConfig() {
    const std::string mappingCvarKey = CVAR_PREFIX_CONTROLLERS ".AxisDirectionMappings." + GetAxisDirectionMappingId();

    mConsoleVariable->ClearVariable(
        StringHelper::Sprintf("%s.AxisDirectionMappingClass", mappingCvarKey.c_str()).c_str());
    mConsoleVariable->ClearVariable(StringHelper::Sprintf("%s.Stick", mappingCvarKey.c_str()).c_str());
    mConsoleVariable->ClearVariable(StringHelper::Sprintf("%s.Direction", mappingCvarKey.c_str()).c_str());
    mConsoleVariable->ClearVariable(StringHelper::Sprintf("%s.WiiUDeviceIndex", mappingCvarKey.c_str()).c_str());
    mConsoleVariable->ClearVariable(StringHelper::Sprintf("%s.WiiUButton", mappingCvarKey.c_str()).c_str());
    mConsoleVariable->Save();
}

std::string WiiUButtonToAxisDirectionMapping::GetPhysicalDeviceName() {
    return WiiUButtonToAnyMapping::GetPhysicalDeviceName();
}

std::string WiiUButtonToAxisDirectionMapping::GetPhysicalInputName() {
    return WiiUButtonToAnyMapping::GetPhysicalInputName();
}
} // namespace Ship
