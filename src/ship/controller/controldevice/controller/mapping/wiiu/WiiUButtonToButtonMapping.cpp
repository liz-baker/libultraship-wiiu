#include "ship/controller/controldevice/controller/mapping/wiiu/WiiUButtonToButtonMapping.h"

#include "ship/utils/StringHelper.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldeck/ControlDeck.h"

namespace Ship {
WiiUButtonToButtonMapping::WiiUButtonToButtonMapping(uint8_t portIndex, CONTROLLERBUTTONS_T bitmask,
                                                     int32_t deviceIndex, uint32_t wiiuButton,
                                                     std::shared_ptr<ControlDeck> controlDeck,
                                                     std::shared_ptr<ConsoleVariable> consoleVariable)
    : ControllerInputMapping(PhysicalDeviceType::WiiUGamepad), WiiUButtonToAnyMapping(deviceIndex, wiiuButton),
      ControllerButtonMapping(PhysicalDeviceType::WiiUGamepad, portIndex, bitmask, controlDeck) {
    mConsoleVariable = std::move(consoleVariable);
    mControlDeck = std::move(controlDeck);
}

void WiiUButtonToButtonMapping::UpdatePad(CONTROLLERBUTTONS_T& padButtons) {
    if (mControlDeck->GamepadGameInputBlocked()) {
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
    mConsoleVariable->SetString(StringHelper::Sprintf("%s.ButtonMappingClass", mappingCvarKey.c_str()).c_str(),
                                "WiiUButtonToButtonMapping");
    mConsoleVariable->SetInteger(StringHelper::Sprintf("%s.Bitmask", mappingCvarKey.c_str()).c_str(), mBitmask);
    mConsoleVariable->SetInteger(StringHelper::Sprintf("%s.WiiUDeviceIndex", mappingCvarKey.c_str()).c_str(),
                                 mDeviceIndex);
    mConsoleVariable->SetInteger(StringHelper::Sprintf("%s.WiiUButton", mappingCvarKey.c_str()).c_str(),
                                 static_cast<int32_t>(mButton));
    mConsoleVariable->Save();
}

void WiiUButtonToButtonMapping::EraseFromConfig() {
    const std::string mappingCvarKey = CVAR_PREFIX_CONTROLLERS ".ButtonMappings." + GetButtonMappingId();

    mConsoleVariable->ClearVariable(StringHelper::Sprintf("%s.ButtonMappingClass", mappingCvarKey.c_str()).c_str());
    mConsoleVariable->ClearVariable(StringHelper::Sprintf("%s.Bitmask", mappingCvarKey.c_str()).c_str());
    mConsoleVariable->ClearVariable(StringHelper::Sprintf("%s.WiiUDeviceIndex", mappingCvarKey.c_str()).c_str());
    mConsoleVariable->ClearVariable(StringHelper::Sprintf("%s.WiiUButton", mappingCvarKey.c_str()).c_str());
    mConsoleVariable->Save();
}

std::string WiiUButtonToButtonMapping::GetPhysicalDeviceName() {
    return WiiUButtonToAnyMapping::GetPhysicalDeviceName();
}

std::string WiiUButtonToButtonMapping::GetPhysicalInputName() {
    return WiiUButtonToAnyMapping::GetPhysicalInputName();
}
} // namespace Ship
