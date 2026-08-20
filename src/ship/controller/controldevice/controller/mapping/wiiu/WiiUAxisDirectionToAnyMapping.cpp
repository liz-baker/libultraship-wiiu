#include "ship/controller/controldevice/controller/mapping/wiiu/WiiUAxisDirectionToAnyMapping.h"

#include <cmath>

#include "ship/utils/StringHelper.h"
#include "ship/window/gui/IconsFontAwesome4.h"

namespace Ship {
WiiUAxisDirectionToAnyMapping::WiiUAxisDirectionToAnyMapping(int32_t deviceIndex, int32_t wiiuAxis,
                                                             int32_t axisDirection)
    : ControllerInputMapping(PhysicalDeviceType::WiiUGamepad) {
    mDeviceIndex = deviceIndex;
    mAxis = wiiuAxis;
    mAxisDirection = static_cast<AxisDirection>(axisDirection);
}

WiiUAxisDirectionToAnyMapping::~WiiUAxisDirectionToAnyMapping() {
}

float WiiUAxisDirectionToAnyMapping::GetAxisDirectionMagnitude() {
    const float axisValue = WiiU::GetAxisValue(mDeviceIndex, mAxis);

    if ((mAxisDirection == POSITIVE && axisValue < 0.0f) || (mAxisDirection == NEGATIVE && axisValue > 0.0f)) {
        return 0.0f;
    }

    return std::fmin(std::fabs(axisValue), 1.0f);
}

std::string WiiUAxisDirectionToAnyMapping::GetPhysicalInputName() {
    const char* stick = (mAxis == WiiU::WIIU_AXIS_LEFT_X || mAxis == WiiU::WIIU_AXIS_LEFT_Y) ? "Left" : "Right";
    const bool isX = mAxis == WiiU::WIIU_AXIS_LEFT_X || mAxis == WiiU::WIIU_AXIS_RIGHT_X;

    const char* arrow = nullptr;
    if (isX) {
        arrow = mAxisDirection == POSITIVE ? ICON_FA_ARROW_RIGHT : ICON_FA_ARROW_LEFT;
    } else {
        // The Wii U reports sticks with up positive, matching the arrow directly.
        arrow = mAxisDirection == POSITIVE ? ICON_FA_ARROW_UP : ICON_FA_ARROW_DOWN;
    }

    return StringHelper::Sprintf("%s Stick %s", stick, arrow);
}

std::string WiiUAxisDirectionToAnyMapping::GetPhysicalDeviceName() {
    return WiiU::GetDeviceName(mDeviceIndex);
}
} // namespace Ship
