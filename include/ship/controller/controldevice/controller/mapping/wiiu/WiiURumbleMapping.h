#pragma once

#include "ship/controller/controldevice/controller/mapping/ControllerRumbleMapping.h"
#include "ship/controller/controldevice/controller/mapping/wiiu/WiiUMapping.h"
#include <memory>

namespace Ship {

/**
 * @brief Drives the rumble motor of one Wii U device.
 *
 * Wii U motors are on/off only — neither VPAD nor WPAD exposes an intensity — so the
 * inherited low/high frequency percentages only decide whether the motor runs: a
 * mapping with both set to 0 stays silent.
 */
class WiiURumbleMapping final : public ControllerRumbleMapping {
  public:
    WiiURumbleMapping(uint8_t portIndex, int32_t deviceIndex, uint8_t lowFrequencyIntensityPercentage,
                      uint8_t highFrequencyIntensityPercentage);

    void StartRumble() override;
    void StopRumble() override;
    std::string GetRumbleMappingId() override;
    void SaveToConfig() override;
    void EraseFromConfig() override;
    std::string GetPhysicalDeviceName() override;

  protected:
    int32_t mDeviceIndex;
};
} // namespace Ship
