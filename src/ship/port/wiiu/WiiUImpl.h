#pragma once

#include <string>
#include <vpad/input.h>
#include <padscore/kpad.h>

namespace Ship {
namespace WiiU {

// Platform bring-up: logging, working directory, and native VPAD/KPAD input.
void Init(const std::string& shortName);

void Exit();

void ThrowMissingOTR(const char* otrPath);

void ThrowInvalidOTR();

// Polls the native VPAD/KPAD devices. Call once per frame before querying state.
void Update();

// Returns the latest gamepad (DRC) state, or nullptr if it is unavailable.
VPADStatus* GetVPADStatus(VPADReadError* error);

// Returns the latest Wii Remote / Pro Controller state for the given channel,
// or nullptr if that channel is unavailable.
KPADStatus* GetKPADStatus(WPADChan chan, KPADError* error);

}; // namespace WiiU
}; // namespace Ship
