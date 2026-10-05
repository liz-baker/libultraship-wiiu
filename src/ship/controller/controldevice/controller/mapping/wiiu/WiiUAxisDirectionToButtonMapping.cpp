#include "ship/controller/controldevice/controller/mapping/wiiu/WiiUAxisDirectionToButtonMapping.h"

#include "ship/utils/StringHelper.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/Context.h"

// How far a stick must be pushed before it counts as a button press.
#define WIIU_AXIS_BUTTON_THRESHOLD 0.7f

namespace Ship {
WiiUAxisDirectionToButtonMapping::WiiUAxisDirectionToButtonMapping(uint8_t portIndex, CONTROLLERBUTTONS_T bitmask,
                                                                   int32_t deviceIndex, int32_t wiiuAxis,
                                                                   int32_t axisDirection)
    : ControllerInputMapping(PhysicalDeviceType::WiiUGamepad),
      ControllerButtonMapping(PhysicalDeviceType::WiiUGamepad, portIndex, bitmask),
      WiiUAxisDirectionToAnyMapping(deviceIndex, wiiuAxis, axisDirection) {
}

void WiiUAxisDirectionToButtonMapping::UpdatePad(CONTROLLERBUTTONS_T& padButtons) {
    if (Context::GetRawInstance()->GetControlDeck()->GamepadGameInputBlocked()) {
        return;
    }

    if (GetAxisDirectionMagnitude() > WIIU_AXIS_BUTTON_THRESHOLD) {
        padButtons |= mBitmask;
    }
}

int8_t WiiUAxisDirectionToButtonMapping::GetMappingType() {
    return MAPPING_TYPE_GAMEPAD;
}

std::string WiiUAxisDirectionToButtonMapping::GetButtonMappingId() {
    return StringHelper::Sprintf("P%d-B%d-WIIU%s-A%d-AD%s", mPortIndex, mBitmask, WiiUDeviceToken(mDeviceIndex).c_str(),
                                 mAxis, mAxisDirection == POSITIVE ? "P" : "N");
}

void WiiUAxisDirectionToButtonMapping::SaveToConfig() {
    const std::string mappingCvarKey = CVAR_PREFIX_CONTROLLERS ".ButtonMappings." + GetButtonMappingId();
    Context::GetRawInstance()->GetConsoleVariables()->SetString(
        StringHelper::Sprintf("%s.ButtonMappingClass", mappingCvarKey.c_str()).c_str(),
        "WiiUAxisDirectionToButtonMapping");
    Context::GetRawInstance()->GetConsoleVariables()->SetInteger(
        StringHelper::Sprintf("%s.Bitmask", mappingCvarKey.c_str()).c_str(), mBitmask);
    Context::GetRawInstance()->GetConsoleVariables()->SetInteger(
        StringHelper::Sprintf("%s.WiiUDeviceIndex", mappingCvarKey.c_str()).c_str(), mDeviceIndex);
    Context::GetRawInstance()->GetConsoleVariables()->SetInteger(
        StringHelper::Sprintf("%s.WiiUAxis", mappingCvarKey.c_str()).c_str(), mAxis);
    Context::GetRawInstance()->GetConsoleVariables()->SetInteger(
        StringHelper::Sprintf("%s.AxisDirection", mappingCvarKey.c_str()).c_str(), mAxisDirection);
    Context::GetRawInstance()->GetConsoleVariables()->Save();
}

void WiiUAxisDirectionToButtonMapping::EraseFromConfig() {
    const std::string mappingCvarKey = CVAR_PREFIX_CONTROLLERS ".ButtonMappings." + GetButtonMappingId();

    Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.ButtonMappingClass", mappingCvarKey.c_str()).c_str());
    Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.Bitmask", mappingCvarKey.c_str()).c_str());
    Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.WiiUDeviceIndex", mappingCvarKey.c_str()).c_str());
    Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.WiiUAxis", mappingCvarKey.c_str()).c_str());
    Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.AxisDirection", mappingCvarKey.c_str()).c_str());
    Context::GetRawInstance()->GetConsoleVariables()->Save();
}

std::string WiiUAxisDirectionToButtonMapping::GetPhysicalDeviceName() {
    return WiiUAxisDirectionToAnyMapping::GetPhysicalDeviceName();
}

std::string WiiUAxisDirectionToButtonMapping::GetPhysicalInputName() {
    return WiiUAxisDirectionToAnyMapping::GetPhysicalInputName();
}
} // namespace Ship
