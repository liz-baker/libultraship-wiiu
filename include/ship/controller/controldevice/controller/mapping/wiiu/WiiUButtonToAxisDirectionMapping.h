#pragma once

#include "ship/controller/controldevice/controller/mapping/ControllerAxisDirectionMapping.h"
#include "WiiUButtonToAnyMapping.h"
#include <memory>

namespace Ship {

/** @brief Binds a normalized Wii U button to one direction of an N64 analog stick. */
class WiiUButtonToAxisDirectionMapping final : public ControllerAxisDirectionMapping, public WiiUButtonToAnyMapping {
  public:
    WiiUButtonToAxisDirectionMapping(uint8_t portIndex, StickIndex stickIndex, Direction direction, int32_t deviceIndex,
                                     uint32_t wiiuButton);

    float GetNormalizedAxisDirectionValue() override;
    std::string GetAxisDirectionMappingId() override;
    int8_t GetMappingType() override;
    void SaveToConfig() override;
    void EraseFromConfig() override;
    std::string GetPhysicalDeviceName() override;
    std::string GetPhysicalInputName() override;

  protected:
};
} // namespace Ship
