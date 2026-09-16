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

## Platform-specific behavior belongs in the cross-platform API, not in Init()/Exit()

When Wii U needs to do something every other platform already does
differently per-platform (resolve a writable directory, find the app bundle
path, etc.), look first for an existing cross-platform entry point - e.g.
`Context::GetAppDirectoryPath()`, `Context::GetAppBundlePath()` - and add a
`__WIIU__` branch there, matching that function's existing contract (what it
returns, whether it creates anything, what "failure" looks like). Don't bolt
a workaround onto `Ship::WiiU::Init()`/`Exit()` (`src/ship/port/wiiu/WiiUImpl.cpp`)
just because that's where Wii U-specific bring-up already lives - those should
stay limited to genuine platform bring-up (logging, native input), not path/
filesystem policy that has a real cross-platform home.

This is exactly what happened with SD card directory setup: `Init()` used to
`mkdir()`/`chdir()` straight into `/vol/external01/...` with every return
value discarded, standing in for a missing `__WIIU__` case in
`GetAppDirectoryPath()` (every other platform's directory resolution lives
there, not in their equivalent of `Init()`). Fixed by adding that case
(`WiiUAppDirectoryPath()` in `Context.cpp`) instead of hardening the
workaround in place - see the discussion on
[liz-baker/lus-wiiu-harness#4](https://github.com/liz-baker/lus-wiiu-harness/issues/4).

`GetAppBundlePath()` (read-only install/bundle path, used by
`GetPathRelativeToAppBundle()`/`LocateFileAcrossAppDirs()`) has the same gap
on Wii U and also falls through to `"."` - currently harmless since nothing
Wii U-reachable calls it yet (`os.cpp`'s only call is `#ifndef __WIIU__`),
but worth the same fix if/when something on this platform needs it.
