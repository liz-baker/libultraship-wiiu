#include "ship/controller/controldevice/controller/mapping/wiiu/WiiUButtonToAnyMapping.h"

namespace Ship {
WiiUButtonToAnyMapping::WiiUButtonToAnyMapping(int32_t deviceIndex, uint32_t wiiuButton)
    : ControllerInputMapping(PhysicalDeviceType::WiiUGamepad) {
    mDeviceIndex = deviceIndex;
    mButton = wiiuButton;
}

WiiUButtonToAnyMapping::~WiiUButtonToAnyMapping() {
}

bool WiiUButtonToAnyMapping::ButtonIsHeld() {
    return (WiiU::GetButtonsHeld(mDeviceIndex) & mButton) != 0;
}

std::string WiiUButtonToAnyMapping::GetPhysicalInputName() {
    const std::string name = WiiU::GetButtonName(mDeviceIndex, mButton);
    // The bound device may be disconnected, or swapped for one without this button.
    return name.empty() ? "Unknown" : name;
}

std::string WiiUButtonToAnyMapping::GetPhysicalDeviceName() {
    return WiiU::GetDeviceName(mDeviceIndex);
}
} // namespace Ship
