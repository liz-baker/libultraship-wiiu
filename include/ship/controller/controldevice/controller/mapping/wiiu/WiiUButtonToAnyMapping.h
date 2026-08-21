#pragma once

#include "ship/controller/controldevice/controller/mapping/wiiu/WiiUMapping.h"
#include "ship/controller/controldevice/controller/mapping/ControllerInputMapping.h"

namespace Ship {

/**
 * @brief Shared base for mappings driven by a normalized Wii U button.
 *
 * Holds the device the binding belongs to (WIIU_DEVICE_GAMEPAD for the GamePad, or
 * a KPAD channel) together with a single WiiUButton bit, and resolves both to
 * display names through the normalized input layer.
 */
class WiiUButtonToAnyMapping : virtual public ControllerInputMapping {
  public:
    /**
     * @brief Constructs a mapping for one button on one Wii U device.
     * @param deviceIndex WIIU_DEVICE_GAMEPAD, or a KPAD channel in [0, WIIU_KPAD_CHANNEL_COUNT).
     * @param wiiuButton A single WiiUButton bit.
     */
    WiiUButtonToAnyMapping(int32_t deviceIndex, uint32_t wiiuButton);
    virtual ~WiiUButtonToAnyMapping();

    std::string GetPhysicalInputName() override;
    std::string GetPhysicalDeviceName() override;

  protected:
    /** @brief Returns true if the bound button is currently held on the bound device. */
    bool ButtonIsHeld();

    int32_t mDeviceIndex;
    uint32_t mButton;
};
} // namespace Ship
