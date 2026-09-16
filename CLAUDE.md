# CLAUDE.md

Working notes for this fork of libultraship. This fork tracks upstream
(`Kenix/libultraship`) while adding **Wii U (CafeOS)** support. See
[`docs/wiiu-port-status.md`](docs/wiiu-port-status.md) for the port design and
remaining work.

## Wii U port status

- **SDL3 guarded out.** The core `libultraship.a` compiles and links end to
  end for `CafeOS`. SDL3 is guarded out of the always-compiled layers — the
  Fast3D GUI/window layer, audio (falls back to the null player), the
  controller/physical-device layer and mapping factories, the libultra OS
  shim, and the crash handler.
- **GX2 renderer.** `GfxRenderingAPIGX2 : GfxRenderingAPI` and
  `GfxWindowBackendWiiU : GfxWindowBackend` implement the class-based Fast3D
  API; `FAST3D_GX2` is the window backend on `CafeOS`. The GX2 ImGui backends
  are ported to the current ImGui, and `gx2_shader_gen` is C++. Builds
  unconditionally on `CafeOS`.
- **Native input/audio.** `PhysicalDeviceType::WiiUGamepad` and a
  `mapping/wiiu/` backend (button / axis-direction / rumble mappings) feed
  the controller layer from a normalized VPAD + KPAD input layer
  (`ship/port/wiiu/WiiUInput.h`), with built-in Wii U defaults; audio plays
  through a native AX player (`AudioBackend::AX`). Not yet covered: DRC gyro
  and the touch screen (see [issue #19](https://github.com/liz-baker/libultraship-wiiu/issues/19)).
- **CI.** The `build-wiiu` job in `build-validation.yml` cross-compiles the
  console library and is blocking on PRs alongside the desktop matrix,
  `test-validation`, and `tidy-format-validation`. It doesn't publish
  artifacts — it's a compile check only.

Gyro has an existing interface waiting for a backend (`ControllerGyroMapping`,
mirroring the `mapping/wiiu/` pattern already used for buttons/axis-direction/
rumble); touch has no mapping abstraction anywhere in libultraship yet and
needs a design pass of its own.

An on-hardware test harness that exercises this port lives in a separate
repository (`lus-wiiu-harness`) rather than in this tree.

## Wii U build

```
dkp-pacman -S --needed wiiu-cmake wiiu-pkg-config ppc-tinyxml2 ppc-libzip
cmake --no-warn-unused-cli -H. -Bbuild-wiiu -GNinja \
  -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/WiiU.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-wiiu
```

- The devkitPro Wii U toolchain sets `CMAKE_SYSTEM_NAME=CafeOS` and defines
  `__WIIU__`; guard Wii U-only code with `CafeOS` in CMake and `__WIIU__` in C/C++.
- SDL3 is **not** available for the Wii U. It is guarded out on `CafeOS`; input
  is native VPAD/KPAD (`src/ship/port/wiiu/WiiUImpl.cpp`, normalized for the
  mapping layer by `WiiUInput.cpp`) and audio is native AX
  (`src/ship/audio/WiiUAudioPlayer.cpp`).
- Platform-agnostic code that means "the gamepad" should use the
  `PHYSICAL_DEVICE_TYPE_GAMEPAD` macro rather than naming `SDLGamepad`, so it
  resolves to `WiiUGamepad` on the console.
- The GX2 renderer / window backend build unconditionally on `CafeOS`. They
  implement the class-based `GfxRenderingAPI` / `GfxWindowBackend` interfaces and
  select the `FAST3D_GX2` window backend, which is the only backend available on
  the console.
- `src/ship/port/wiiu/*` is excluded from the `tidy-format-validation`
  clang-tidy-diff step because those files include devkitPPC-only headers
  unavailable on the Linux tidy host.

## Conventions

- C++ is formatted with **clang-format-14** (`.clang-format`). Run it on changed
  files before pushing or `tidy-format` will fail.
