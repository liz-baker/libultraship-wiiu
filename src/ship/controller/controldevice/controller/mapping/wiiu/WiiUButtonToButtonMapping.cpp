#include "ship/controller/controldevice/controller/mapping/wiiu/WiiUButtonToButtonMapping.h"

#include "ship/utils/StringHelper.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/Context.h"

namespace Ship {
WiiUButtonToButtonMapping::WiiUButtonToButtonMapping(uint8_t portIndex, CONTROLLERBUTTONS_T bitmask,
                                                     int32_t deviceIndex, uint32_t wiiuButton)
    : ControllerInputMapping(PhysicalDeviceType::WiiUGamepad), WiiUButtonToAnyMapping(deviceIndex, wiiuButton),
      ControllerButtonMapping(PhysicalDeviceType::WiiUGamepad, portIndex, bitmask) {
}

void WiiUButtonToButtonMapping::UpdatePad(CONTROLLERBUTTONS_T& padButtons) {
    if (Context::GetRawInstance()->GetControlDeck()->GamepadGameInputBlocked()) {
        return;
    }

    if (ButtonIsHeld()) {
        padButtons |= mBitmask;
    }
}

int8_t WiiUButtonToButtonMapping::GetMappingType() {
    return MAPPING_TYPE_GAMEPAD;
}

std::string WiiUButtonToButtonMapping::GetButtonMappingId() {
    return StringHelper::Sprintf("P%d-B%d-WIIU%s-B%u", mPortIndex, mBitmask, WiiUDeviceToken(mDeviceIndex).c_str(),
                                 mButton);
}

void WiiUButtonToButtonMapping::SaveToConfig() {
    const std::string mappingCvarKey = CVAR_PREFIX_CONTROLLERS ".ButtonMappings." + GetButtonMappingId();
    Context::GetRawInstance()->GetConsoleVariables()->SetString(
        StringHelper::Sprintf("%s.ButtonMappingClass", mappingCvarKey.c_str()).c_str(), "WiiUButtonToButtonMapping");
    Context::GetRawInstance()->GetConsoleVariables()->SetInteger(
        StringHelper::Sprintf("%s.Bitmask", mappingCvarKey.c_str()).c_str(), mBitmask);
    Context::GetRawInstance()->GetConsoleVariables()->SetInteger(
        StringHelper::Sprintf("%s.WiiUDeviceIndex", mappingCvarKey.c_str()).c_str(), mDeviceIndex);
    Context::GetRawInstance()->GetConsoleVariables()->SetInteger(
        StringHelper::Sprintf("%s.WiiUButton", mappingCvarKey.c_str()).c_str(), static_cast<int32_t>(mButton));
    Context::GetRawInstance()->GetConsoleVariables()->Save();
}

void WiiUButtonToButtonMapping::EraseFromConfig() {
    const std::string mappingCvarKey = CVAR_PREFIX_CONTROLLERS ".ButtonMappings." + GetButtonMappingId();

    Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.ButtonMappingClass", mappingCvarKey.c_str()).c_str());
    Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.Bitmask", mappingCvarKey.c_str()).c_str());
    Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.WiiUDeviceIndex", mappingCvarKey.c_str()).c_str());
    Context::GetRawInstance()->GetConsoleVariables()->ClearVariable(
        StringHelper::Sprintf("%s.WiiUButton", mappingCvarKey.c_str()).c_str());
    Context::GetRawInstance()->GetConsoleVariables()->Save();
}

std::string WiiUButtonToButtonMapping::GetPhysicalDeviceName() {
    return WiiUButtonToAnyMapping::GetPhysicalDeviceName();
}

std::string WiiUButtonToButtonMapping::GetPhysicalInputName() {
    return WiiUButtonToAnyMapping::GetPhysicalInputName();
}
} // namespace Ship
