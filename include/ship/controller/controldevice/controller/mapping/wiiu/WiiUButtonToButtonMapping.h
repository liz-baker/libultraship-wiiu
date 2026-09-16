#pragma once

#include "ship/controller/controldevice/controller/mapping/ControllerButtonMapping.h"
#include "ship/controller/controldevice/controller/mapping/wiiu/WiiUButtonToAnyMapping.h"
#include <memory>

namespace Ship {
class ConsoleVariable;
class ControlDeck;

/** @brief Binds a normalized Wii U button to an N64 controller button. */
class WiiUButtonToButtonMapping final : public WiiUButtonToAnyMapping, public ControllerButtonMapping {
  public:
    WiiUButtonToButtonMapping(uint8_t portIndex, CONTROLLERBUTTONS_T bitmask, int32_t deviceIndex, uint32_t wiiuButton,
                              std::shared_ptr<ControlDeck> controlDeck = nullptr,
                              std::shared_ptr<ConsoleVariable> consoleVariable = nullptr);

    void UpdatePad(CONTROLLERBUTTONS_T& padButtons) override;
    int8_t GetMappingType() override;
    std::string GetButtonMappingId() override;
    void SaveToConfig() override;
    void EraseFromConfig() override;
    std::string GetPhysicalDeviceName() override;
    std::string GetPhysicalInputName() override;

  protected:
    std::shared_ptr<ConsoleVariable> mConsoleVariable;
    std::shared_ptr<ControlDeck> mControlDeck;
};
} // namespace Ship
