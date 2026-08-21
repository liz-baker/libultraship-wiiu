# Wii U port status

This document tracks the state of the Wii U (Cafe / `CafeOS`) port of this
libultraship fork. The port re-homes the Wii U-specific code that previously
lived in [`harbourmasters/libultraship-wiiu`](https://github.com/harbourmasters/libultraship-wiiu)
onto the current, refactored upstream (`Kenix/libultraship`) tree while keeping
a clean upstream history.

## Why this is a port, not a cherry-pick

The upstream tree diverged substantially from the code the original Wii U work
was written against:

| Concern | Upstream (this repo) | Original Wii U fork |
| --- | --- | --- |
| Fast3D backends | `src/fast/backends/gfx_*.cpp`, C++ classes (`GfxRenderingAPI`, `GfxWindowBackend`) | `src/graphic/Fast3D/gfx_*.cpp`, C structs of function pointers |
| Platform ports | `src/ship/port/` | `src/port/` |
| ImGui | fetched via `FetchContent` | vendored under `extern/ImGui` |
| Windowing / input | SDL3 | SDL2 |

Because of this, the Wii U changes are being reintroduced as fresh, adapted
commits rather than replayed cherry-picks.

## The SDL3 problem

Upstream now depends on **SDL3** across the ImGui backend, the whole controller
/ input mapping layer, and audio. devkitPro only ships **SDL2** for the Wii U —
SDL3 is not available for the platform. The port therefore treats SDL3 as
unavailable on `CafeOS`:

- The SDL ImGui backends and the SDL controller mapping layer
  (`controller/.../mapping/sdl`) are excluded from the Wii U build, and a
  parallel `controller/.../mapping/wiiu` layer takes their place.
- Input is read natively through VPAD/KPAD (`src/ship/port/wiiu/WiiUImpl.cpp`).
- Audio is mixed natively through AX (`src/ship/audio/WiiUAudioPlayer.cpp`).

## What is in place

- **Toolchain / CMake wiring** — `CafeOS` is detected throughout the build.
  `cmake/dependencies/wiiu.cmake` fetches and patches `spdlog` (no TLS / thread
  id) and `thread-pool` (no `thread_local`) and pulls `nlohmann_json`. The
  common dependency file guards the desktop ImGui backends, STB thread-locals,
  and the default thread-pool for `CafeOS`.
- **Native platform layer** — `src/ship/port/wiiu/WiiUImpl.{h,cpp}` (logging,
  working directory, native VPAD/KPAD polling) and
  `src/ship/port/wiiu/WiiUInput.cpp`, which normalizes those devices for the
  controller layer.
- **Native input and audio** — a `WiiUGamepad` physical-device type with its own
  mapping classes, and an AX audio player.
- **Ported GX2 graphics sources** — the GX2 renderer, Wii U window backend,
  GX2 shader generation/util, and the GX2 ImGui backends are re-homed into
  `src/fast/backends/` and `src/ship/port/wiiu/ImGui/`.
- **CI** — `.github/workflows/build-wiiu.yml` cross-compiles inside the
  `devkitpro/devkitppc` container.

## Status

**Phases A, B, C and D are complete.** The core `libultraship.a` compiles and
links end to end for `CafeOS`, including the GX2 renderer and Wii U window
backend. SDL3 is fully guarded out of the always-compiled layers, controllers
and audio are driven natively, and the blocking `build-wiiu` CI job covers the
whole console build. The desktop `build-validation` / `test-validation`
workflows are back to running on every push and PR, so the fork now gets full
CI coverage — desktop matrix, Wii U cross-compile, and clang-format/tidy — on
every change, not just the port-specific check.

### What Phase B changed

- `gfx_gx2` is now `GfxRenderingAPIGX2 : public Fast::GfxRenderingAPI`, with the
  former file-static state and the `ShaderProgram` / `Texture` / `Framebuffer`
  types moved onto the class. It implements the methods the old C API lacked —
  `ClearShaderCache`, `GetTextureById` (replacing `gfx_gx2_texture_for_imgui`)
  and `SetCurrentPrimDepth` (store-only; GX2 has no prim-depth uniform) — and the
  old `gfx_gx2_shutdown` became the destructor.
- `gfx_wiiu` is now `GfxWindowBackendWiiU : public Fast::GfxWindowBackend`
  (`start_frame` → `IsFrameReady`, plus the new mouse/dimension/monitor members).
  The MEM1 / foreground heap and context-state helpers stay free functions shared
  with the renderer. Teardown is split across `Destroy()` (native input), the
  renderer destructor (GX2 resources) and the window backend destructor
  (`GX2Shutdown` + `WHBProcShutdown`) to preserve ordering.
- `FAST3D_GX2` was added to the `WindowBackend` enum and wired into
  `Fast3dWindow` (backend registration, `InitWindowManager`,
  `GetWindowBackendName`) and the `Fast3dGui` ImGui backend switches.
- `gx2_shader_gen.c` became `.cpp` so it can use the now-C++ `CCFeatures`, and the
  GX2/Wii U ImGui backends were ported to the current ImGui.
- The `LUS_WIIU_GX2` option was removed. It existed only so Phase A could compile
  the core while the GX2 sources were still unconverted; every use was paired with
  `__WIIU__`, and with it off the Wii U build registered `FAST3D_SDL_OPENGL` as
  its only backend even though SDL is excluded on `CafeOS`. `__WIIU__` / `CafeOS`
  is now the single gate.

### What Phase C changed

**Input.** `PhysicalDeviceType` gained `WiiUGamepad`, and every platform-agnostic
call site that previously named `SDLGamepad` now uses the
`PHYSICAL_DEVICE_TYPE_GAMEPAD` macro, which resolves to `WiiUGamepad` on the
console and `SDLGamepad` everywhere else.

Underneath sits a normalized input layer, `include/ship/port/wiiu/WiiUInput.h`,
implemented by `src/ship/port/wiiu/WiiUInput.cpp`. VPAD and the KPAD extensions
each use their own mutually incompatible button masks, so the layer translates
all of them — GamePad, Wii Remote, Wii Remote + Nunchuk, Classic Controller and
Pro Controller — into one `WiiUButton` set and one four-entry `WiiUAxis` set.
Bindings are therefore stored against the normalized set and stay meaningful when
a player swaps hardware; a device that lacks a button simply never reports it.
The header is deliberately free of devkitPPC includes, so the mapping layer never
pulls in Cafe SDK headers.

On top of it, `controller/.../mapping/wiiu/` mirrors the SDL mapping classes:
`WiiUButtonTo{Button,AxisDirection}Mapping`,
`WiiUAxisDirectionTo{Button,AxisDirection}Mapping` and `WiiURumbleMapping`. Each
stores the device it belongs to — `WIIU_DEVICE_GAMEPAD` for the DRC, or a KPAD
channel — alongside the normalized button or axis, and the three mapping
factories gained `__WIIU__` branches for config load, defaults, and the input
editor's "press a button to bind" flow.

Unlike the SDL tables, which libultraship leaves for the consuming game to fill
in, `ControllerDefaultMappings` ships built-in Wii U defaults: with SDL gone,
nothing else would supply one. Port 0 binds both the GamePad and KPAD channel 0,
so a lone Wii Remote works as player 1; ports 1-3 take the KPAD channel of the
same number, leaving no channel shared between ports. The N64 C buttons default
to the right stick, and `BTN_Z` maps to several source buttons at once (ZL, ZR,
the nunchuk's Z, and the Wii Remote's "1") so one table covers every device.

**Audio.** `AudioBackend::AX` and `WiiUAudioPlayer` drive the console's AX mixer
directly: two looping voices, one per output channel, read from a pair of ring
buffers that `DoPlay()` writes ahead of the hardware read offset, and `Buffered()`
reports the gap between the write and read heads so the audio system paces itself
exactly as it does on the other backends. Output is stereo; a 5.1 channel setting
is downmixed to the front pair rather than driving AX's surround path.

## Remaining work

1. Runtime validation on real hardware — the port is compile-clean, but neither
   the GX2 renderer nor the native input and audio paths have been exercised on a
   console. Tracked in [issue #5](https://github.com/liz-baker/libultraship-wiiu/issues/5):
   `tools/wiiu-harness/` is a staged, loadable `.wuhb` test app (Aroma-loadable,
   `wiiload`-friendly) built only for `CafeOS`. Stage 0 (boot, toolchain/heap
   info, SD write via OSScreen) has landed and is the first thing in this repo
   to actually *link* an executable against `libultraship.a`, closing a gap
   the static-lib-only CI build couldn't catch: an undefined Wii U symbol.
   Stages 1-4 (normalized input readout, AX audio, the GX2 renderer, and the
   full `Context`/mapping layer) are still open. `build-wiiu` publishes the
   `.wuhb` (and the static lib) as a GitHub Release on every push to `main`,
   and as a real release on `v*` tags, so testing on hardware doesn't require
   a local devkitPro install.
2. Wii U input features not yet surfaced: the DRC's gyroscope (there is a
   `ControllerGyroMapping` interface waiting for it) and its touch screen.

Progress is driven through CI on real devkitPPC output: the devkitPPC toolchain
image cannot be pulled from the Claude Code sandbox (its Docker Hub blob CDN is
blocked by the egress policy), so the `build-wiiu` job is the compile loop for
the Wii U code.
