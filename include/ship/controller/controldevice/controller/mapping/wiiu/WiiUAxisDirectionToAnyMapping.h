#pragma once

#include "ship/controller/controldevice/controller/mapping/wiiu/WiiUMapping.h"
#include "ship/controller/controldevice/controller/mapping/ControllerInputMapping.h"

namespace Ship {

/**
 * @brief Shared base for mappings driven by one half of a normalized Wii U stick axis.
 *
 * Holds the device the binding belongs to, the WiiUAxis it reads, and which half of
 * that axis (negative or positive) activates the mapping.
 */
class WiiUAxisDirectionToAnyMapping : virtual public ControllerInputMapping {
  public:
    /**
     * @brief Constructs a mapping for one direction of one axis on one Wii U device.
     * @param deviceIndex WIIU_DEVICE_GAMEPAD, or a KPAD channel in [0, WIIU_KPAD_CHANNEL_COUNT).
     * @param wiiuAxis A WiiUAxis value.
     * @param axisDirection NEGATIVE or POSITIVE.
     */
    WiiUAxisDirectionToAnyMapping(int32_t deviceIndex, int32_t wiiuAxis, int32_t axisDirection);
    virtual ~WiiUAxisDirectionToAnyMapping();

    std::string GetPhysicalInputName() override;
    std::string GetPhysicalDeviceName() override;

  protected:
    /**
     * @brief Returns how far the bound axis is pushed in the bound direction.
     * @return A value in [0.0, 1.0]; 0.0 when the axis is pushed the other way.
     */
    float GetAxisDirectionMagnitude();

    int32_t mDeviceIndex;
    int32_t mAxis;
    AxisDirection mAxisDirection;
};
} // namespace Ship
