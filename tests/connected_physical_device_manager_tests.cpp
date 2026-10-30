#include <gtest/gtest.h>

#include "ship/controller/physicaldevice/ConnectedPhysicalDeviceManager.h"

namespace Ship {
namespace {

class ConnectedPhysicalDeviceManagerTest : public testing::Test {
  protected:
    void SetUp() override {
        ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_GAMEPAD));

        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.vendor_id = 0x1209;
        desc.product_id = 0x0001;
        desc.naxes = 2;
        desc.nbuttons = 1;
        desc.button_mask = 1 << SDL_GAMEPAD_BUTTON_SOUTH;
        desc.axis_mask = (1 << SDL_GAMEPAD_AXIS_LEFTX) | (1 << SDL_GAMEPAD_AXIS_LEFTY);
        desc.name = "LUS virtual gamepad";

        mInstanceId = SDL_AttachVirtualJoystick(&desc);
        ASSERT_NE(mInstanceId, 0u) << SDL_GetError();
    }

    void TearDown() override {
        if (mInstanceId != 0) {
            SDL_DetachVirtualJoystick(mInstanceId);
        }
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    }

    SDL_JoystickID mInstanceId = 0;
};

TEST_F(ConnectedPhysicalDeviceManagerTest, RefreshPreservesHandleForConnectedGamepad) {
    ConnectedPhysicalDeviceManager manager;

    manager.RefreshConnectedSDLGamepads();
    const auto firstScan = manager.GetConnectedSDLGamepadsForPort(0);
    ASSERT_TRUE(firstScan.contains(mInstanceId));
    SDL_Gamepad* const firstHandle = firstScan.at(mInstanceId);
    ASSERT_NE(firstHandle, nullptr);

    manager.RefreshConnectedSDLGamepads();
    const auto secondScan = manager.GetConnectedSDLGamepadsForPort(0);
    ASSERT_TRUE(secondScan.contains(mInstanceId));
    EXPECT_EQ(secondScan.at(mInstanceId), firstHandle);

    const SDL_JoystickID detachedInstanceId = mInstanceId;
    ASSERT_TRUE(SDL_DetachVirtualJoystick(detachedInstanceId));
    mInstanceId = 0;
    manager.RefreshConnectedSDLGamepads();
    EXPECT_FALSE(manager.GetConnectedSDLGamepadsForPort(0).contains(detachedInstanceId));
    EXPECT_EQ(SDL_GetGamepadFromID(detachedInstanceId), nullptr);
}

} // namespace
TEST_F(ConnectedPhysicalDeviceManagerTest, EnumeratesAttachedGamepad) {
    ConnectedPhysicalDeviceManager manager;
    manager.RefreshConnectedSDLGamepads();

    const auto gamepads = manager.GetConnectedGamepads();
    ASSERT_EQ(gamepads.size(), 1u);
    EXPECT_EQ(gamepads[0].id, static_cast<int32_t>(mInstanceId));
    EXPECT_EQ(gamepads[0].name, "LUS virtual gamepad");
    EXPECT_EQ(gamepads[0].deviceType, PHYSICAL_DEVICE_TYPE_GAMEPAD);
    EXPECT_EQ(gamepads[0].faceLayout, GamepadFaceLayout::Xbox);

    ASSERT_TRUE(SDL_DetachVirtualJoystick(mInstanceId));
    mInstanceId = 0;
    manager.RefreshConnectedSDLGamepads();
    EXPECT_TRUE(manager.GetConnectedGamepads().empty());
}

TEST_F(ConnectedPhysicalDeviceManagerTest, EnumerationIsOrderedById) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.vendor_id = 0x1209;
    desc.product_id = 0x0002;
    desc.naxes = 2;
    desc.nbuttons = 1;
    desc.button_mask = 1 << SDL_GAMEPAD_BUTTON_SOUTH;
    desc.axis_mask = (1 << SDL_GAMEPAD_AXIS_LEFTX) | (1 << SDL_GAMEPAD_AXIS_LEFTY);
    desc.name = "LUS second gamepad";
    const SDL_JoystickID second = SDL_AttachVirtualJoystick(&desc);
    ASSERT_NE(second, 0u) << SDL_GetError();

    ConnectedPhysicalDeviceManager manager;
    manager.RefreshConnectedSDLGamepads();
    const auto gamepads = manager.GetConnectedGamepads();
    SDL_DetachVirtualJoystick(second);

    ASSERT_EQ(gamepads.size(), 2u);
    EXPECT_LT(gamepads[0].id, gamepads[1].id);
}

// Maps a vendor/product id pair SDL recognizes onto the face layout reported for it.
static GamepadFaceLayout LayoutForUsbId(uint16_t vendor, uint16_t product) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.vendor_id = vendor;
    desc.product_id = product;
    desc.naxes = 2;
    desc.nbuttons = 1;
    desc.button_mask = 1 << SDL_GAMEPAD_BUTTON_SOUTH;
    desc.axis_mask = (1 << SDL_GAMEPAD_AXIS_LEFTX) | (1 << SDL_GAMEPAD_AXIS_LEFTY);
    desc.name = "LUS layout probe";
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    EXPECT_NE(id, 0u) << SDL_GetError();

    ConnectedPhysicalDeviceManager manager;
    manager.RefreshConnectedSDLGamepads();
    GamepadFaceLayout layout = GamepadFaceLayout::Xbox;
    bool found = false;
    for (const auto& gamepad : manager.GetConnectedGamepads()) {
        if (gamepad.id == static_cast<int32_t>(id)) {
            layout = gamepad.faceLayout;
            found = true;
        }
    }
    EXPECT_TRUE(found);
    SDL_DetachVirtualJoystick(id);
    return layout;
}

TEST_F(ConnectedPhysicalDeviceManagerTest, FaceLayoutFollowsGamepadType) {
    EXPECT_EQ(LayoutForUsbId(0x054c, 0x0ce6), GamepadFaceLayout::PlayStation); // DualSense
    EXPECT_EQ(LayoutForUsbId(0x057e, 0x2009), GamepadFaceLayout::Nintendo);    // Switch Pro Controller
    EXPECT_EQ(LayoutForUsbId(0x045e, 0x02ea), GamepadFaceLayout::Xbox);        // Xbox One
}

TEST_F(ConnectedPhysicalDeviceManagerTest, IgnoreListsRouteDevicesPerPort) {
    ConnectedPhysicalDeviceManager manager;
    manager.RefreshConnectedSDLGamepads();
    const auto id = static_cast<int32_t>(mInstanceId);

    EXPECT_TRUE(manager.DeviceIsRoutedToPort(0, id));
    EXPECT_FALSE(manager.DeviceIsRoutedToPort(1, id)); // new devices start on port 0 only

    manager.IgnoreInstanceIdForPort(0, id);
    manager.UnignoreInstanceIdForPort(1, id);
    EXPECT_FALSE(manager.DeviceIsRoutedToPort(0, id));
    EXPECT_TRUE(manager.DeviceIsRoutedToPort(1, id));
}

TEST(ConnectedPhysicalDeviceManagerPresenceTest, KeyboardAndMouseFollowSDL) {
    ConnectedPhysicalDeviceManager manager;
    EXPECT_EQ(manager.HasKeyboard(), SDL_HasKeyboard());
    EXPECT_EQ(manager.HasMouse(), SDL_HasMouse());
}

} // namespace Ship
