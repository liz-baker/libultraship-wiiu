#pragma once

// A minimal Ship::Window stub used only by the "input" harness category.
//
// ControlDeck's real mapping objects (WiiUButtonToButtonMapping and friends) call
// mControlDeck->GamepadGameInputBlocked(), which dereferences GetWindow()->GetGui(). That means
// driving the real mapping layer needs *some* live Window+Gui, but a fully real, Init()'d Gui
// requires a ResourceManager (OTR archives) to build its icon fonts and default windows - the
// same wall that makes a full Context (Stage 4) more than this harness wants to take on.
//
// The way out: Gui::GetMenuOrMenubarVisible() (the only thing GamepadGameInputBlocked() actually
// calls) is safe to call on a Gui that has never had Init() run - both mMenuBar and mMenu are
// still null, so it just returns false. So this class exists purely to satisfy ControlDeck's
// dependency contract: every other Window virtual is a harmless stub, and the Gui it hands out
// is deliberately never Init()'d (no ImGui context is ever created for it, so there's nothing to
// tear down and no risk of colliding with the separate ImGui context the GX2 category creates).
//
// Deliberately never destructed once created - see the comment on the harness's
// ControlDeckTestState for why.

#include "ship/window/Window.h"
#include "ship/window/gui/Gui.h"

class HarnessWindow : public Ship::Window {
  public:
    HarnessWindow() : Ship::Window(std::make_shared<Ship::Gui>()) {
    }

    void Close() override {
    }
    void RunGuiOnly() override {
    }
    void StartFrame() override {
    }
    void EndFrame() override {
    }
    bool IsFrameReady() override {
        return true;
    }
    void HandleEvents() override {
    }
    void SetCursorVisibility(bool /*visible*/) override {
    }
    uint32_t GetWidth() override {
        return 1280;
    }
    uint32_t GetHeight() override {
        return 720;
    }
    float GetAspectRatio() override {
        return 1280.0f / 720.0f;
    }
    int32_t GetPosX() override {
        return 0;
    }
    int32_t GetPosY() override {
        return 0;
    }
    void SetMousePos(Ship::Coords /*pos*/) override {
    }
    Ship::Coords GetMousePos() override {
        return { 0, 0 };
    }
    Ship::Coords GetMouseDelta() override {
        return { 0, 0 };
    }
    Ship::CoordsF GetMouseWheel() override {
        return { 0.0f, 0.0f };
    }
    bool GetMouseState(Ship::MouseBtn /*btn*/) override {
        return false;
    }
    void SetMouseCapture(bool /*capture*/) override {
    }
    bool IsMouseCaptured() override {
        return false;
    }
    uint32_t GetCurrentRefreshRate() override {
        return 60;
    }
    bool SupportsWindowedFullscreen() override {
        return false;
    }
    bool CanDisableVerticalSync() override {
        return false;
    }
    void SetResolutionMultiplier(float /*multiplier*/) override {
    }
    void SetMsaaLevel(uint32_t /*value*/) override {
    }
    void SetFullscreen(bool /*isFullscreen*/) override {
    }
    bool IsFullscreen() override {
        return false;
    }
    bool IsRunning() override {
        return true;
    }
    const char* GetKeyName(int32_t /*scancode*/) override {
        return "";
    }
    uintptr_t GetGfxFrameBuffer() override {
        return 0;
    }
    void SetCurrentDimensions(uint32_t /*width*/, uint32_t /*height*/) override {
    }
    void SetCurrentDimensions(uint32_t /*width*/, uint32_t /*height*/, int32_t /*posX*/, int32_t /*posY*/) override {
    }
    void SetCurrentDimensions(bool /*isFullscreen*/, uint32_t /*width*/, uint32_t /*height*/) override {
    }
    void SetCurrentDimensions(bool /*isFullscreen*/, uint32_t /*width*/, uint32_t /*height*/, int32_t /*posX*/,
                              int32_t /*posY*/) override {
    }
    Ship::WindowRect GetPrimaryMonitorRect() override {
        return { 0, 0, 0, 0 };
    }
};
