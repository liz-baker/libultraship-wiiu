#include "ship/controller/controldevice/controller/mapping/wiiu/WiiUAxisDirectionToButtonMapping.h"

#include "ship/utils/StringHelper.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldeck/ControlDeck.h"

// How far a stick must be pushed before it counts as a button press.
#define WIIU_AXIS_BUTTON_THRESHOLD 0.7f

namespace Ship {
WiiUAxisDirectionToButtonMapping::WiiUAxisDirectionToButtonMapping(uint8_t portIndex, CONTROLLERBUTTONS_T bitmask,
                                                                   int32_t deviceIndex, int32_t wiiuAxis,
                                                                   int32_t axisDirection,
                                                                   std::shared_ptr<ControlDeck> controlDeck,
                                                                   std::shared_ptr<ConsoleVariable> consoleVariable)
    : ControllerInputMapping(PhysicalDeviceType::WiiUGamepad),
      ControllerButtonMapping(PhysicalDeviceType::WiiUGamepad, portIndex, bitmask, controlDeck),
      WiiUAxisDirectionToAnyMapping(deviceIndex, wiiuAxis, axisDirection) {
    mConsoleVariable = std::move(consoleVariable);
    mControlDeck = std::move(controlDeck);
}

void WiiUAxisDirectionToButtonMapping::UpdatePad(CONTROLLERBUTTONS_T& padButtons) {
    if (mControlDeck->GamepadGameInputBlocked()) {
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
    mConsoleVariable->SetString(StringHelper::Sprintf("%s.ButtonMappingClass", mappingCvarKey.c_str()).c_str(),
                                "WiiUAxisDirectionToButtonMapping");
    mConsoleVariable->SetInteger(StringHelper::Sprintf("%s.Bitmask", mappingCvarKey.c_str()).c_str(), mBitmask);
    mConsoleVariable->SetInteger(StringHelper::Sprintf("%s.WiiUDeviceIndex", mappingCvarKey.c_str()).c_str(),
                                 mDeviceIndex);
    mConsoleVariable->SetInteger(StringHelper::Sprintf("%s.WiiUAxis", mappingCvarKey.c_str()).c_str(), mAxis);
    mConsoleVariable->SetInteger(StringHelper::Sprintf("%s.AxisDirection", mappingCvarKey.c_str()).c_str(),
                                 mAxisDirection);
    mConsoleVariable->Save();
}

void WiiUAxisDirectionToButtonMapping::EraseFromConfig() {
    const std::string mappingCvarKey = CVAR_PREFIX_CONTROLLERS ".ButtonMappings." + GetButtonMappingId();

    mConsoleVariable->ClearVariable(StringHelper::Sprintf("%s.ButtonMappingClass", mappingCvarKey.c_str()).c_str());
    mConsoleVariable->ClearVariable(StringHelper::Sprintf("%s.Bitmask", mappingCvarKey.c_str()).c_str());
    mConsoleVariable->ClearVariable(StringHelper::Sprintf("%s.WiiUDeviceIndex", mappingCvarKey.c_str()).c_str());
    mConsoleVariable->ClearVariable(StringHelper::Sprintf("%s.WiiUAxis", mappingCvarKey.c_str()).c_str());
    mConsoleVariable->ClearVariable(StringHelper::Sprintf("%s.AxisDirection", mappingCvarKey.c_str()).c_str());
    mConsoleVariable->Save();
}

std::string WiiUAxisDirectionToButtonMapping::GetPhysicalDeviceName() {
    return WiiUAxisDirectionToAnyMapping::GetPhysicalDeviceName();
}

std::string WiiUAxisDirectionToButtonMapping::GetPhysicalInputName() {
    return WiiUAxisDirectionToAnyMapping::GetPhysicalInputName();
}
} // namespace Ship
