#pragma once

#include "ship/controller/controldevice/controller/mapping/ControllerButtonMapping.h"
#include "ship/controller/controldevice/controller/mapping/wiiu/WiiUAxisDirectionToAnyMapping.h"
#include <memory>

namespace Ship {

/** @brief Binds one direction of a Wii U stick axis to an N64 controller button. */
class WiiUAxisDirectionToButtonMapping final : public ControllerButtonMapping, public WiiUAxisDirectionToAnyMapping {
  public:
    WiiUAxisDirectionToButtonMapping(uint8_t portIndex, CONTROLLERBUTTONS_T bitmask, int32_t deviceIndex,
                                     int32_t wiiuAxis, int32_t axisDirection);

    void UpdatePad(CONTROLLERBUTTONS_T& padButtons) override;
    int8_t GetMappingType() override;
    std::string GetButtonMappingId() override;
    void SaveToConfig() override;
    void EraseFromConfig() override;
    std::string GetPhysicalDeviceName() override;
    std::string GetPhysicalInputName() override;

  protected:
};
} // namespace Ship
