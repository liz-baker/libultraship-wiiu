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
   the GX2 renderer nor the native input and audio paths have been exercised on
   a console. Tracked in [issue #5](https://github.com/liz-baker/libultraship-wiiu/issues/5)
   via `tools/wiiu-harness/`, a staged, loadable `.wuhb` test app (Aroma-loadable,
   `wiiload`-friendly for iteration) built only for `CafeOS`, gated behind
   `LUS_BUILD_WIIU_HARNESS` (default `ON`). Stages are ordered so each one
   isolates a failure before the next adds complexity — see the issue for the
   full rationale. `build-wiiu` publishes the `.wuhb` and the static lib as a
   GitHub Release on every push to `main` (prerelease, tagged
   `wiiu-main-<sha>`) and as a real release on `v*` tags, so hardware testing
   doesn't require a local devkitPro install. The harness's main screen is a
   D-Pad/A text menu that picks which stage to test — `B` returns to it from
   any stage — rather than cycling through them with `+`, so the list can
   keep growing without becoming tedious to navigate.

   - **Stage 0 — boot & link** (done, [PR #7](https://github.com/liz-baker/libultraship-wiiu/pull/7);
     confirmed on hardware): OSScreen console (chosen over GX2 for the early
     stages specifically because it has no dependency on the renderer under
     test), prints compiler/build info and MEM2 heap free bytes, writes
     `sd:/wiiu/apps/lus-harness/results.txt`. Links `libultraship` with
     `-Wl,--whole-archive` so the whole static archive gets symbol-resolved,
     not just what Stage 0 itself calls — the first thing in this repo to
     actually *link* an executable against `libultraship.a`, closing a gap
     the static-lib-only CI build couldn't catch: an undefined Wii U symbol.
   - **Stage 1 — normalized input readout** (done,
     [PR #9](https://github.com/liz-baker/libultraship-wiiu/pull/9);
     confirmed on hardware for the GamePad — Wii Remote, Nunchuk, Classic, and
     Pro Controller are still untested): live-prints, per connected device,
     `GetDeviceName()`, the decoded `GetButtonsHeld()` mask (names, not hex),
     and all four `GetAxisValue()` axes from
     `include/ship/port/wiiu/WiiUInput.h`, for GamePad, Wii Remote, Nunchuk,
     Classic, and Pro Controller. Directly validates the button tables in
     `src/ship/port/wiiu/WiiUInput.cpp`, which compiled but were never checked
     against real hardware. (Unrelated observation from the hardware run: the
     screen tints reddish under the system HOME menu overlay — expected
     `OSScreen`-vs-system-overlay behavior, not a port bug, and moot once
     Stage 3 replaces `OSScreen` with GX2.)
   - **Stage 2 — AX audio** (harness code landed; not yet run on real
     hardware): instantiates `WiiUAudioPlayer` directly — it only takes an
     `AudioSettings` struct at construction, no `Context` dependency — and
     feeds it a continuous, phase-continuous 220–880 Hz sweep rather than a
     steady tone, with the right channel offset from the left by a fixed
     pitch ratio (a perfect fourth). A steady single tone can't reveal a
     wrap-boundary click against its own unchanging pitch, and identical L/R
     tones can't reveal a channel swap; the sweep and the L/R offset make both
     audible. `X` cycles Both → Left → Right → Both to isolate one channel at
     a time — the muted channel's phase accumulator keeps advancing while
     silent, so toggling never introduces a click of its own (only stereo is
     wired up, per `WiiUAudioPlayer`'s class doc, so Both/Left/Right is the
     whole space worth isolating; AX itself can drive 5.1, but that path
     isn't implemented here). `Buffered()` is tracked for underrun count
     (buffered hits 0), min/max over the session, and a near-ring-capacity
     flag (approaching the internal ring size risks the writer lapping the
     read head) — all surfaced live on screen and in `results.txt`, so a
     dropout is confirmed by the display rather than by ear alone.
   - **Stage 3 — GX2 renderer** (harness code landed; not yet run on real
     hardware): drives `GfxRenderingAPIGX2` itself directly, deliberately not
     raw GX2 calls — raw GX2 would only prove the console can draw a
     triangle, and wouldn't touch a single line of libultraship's own
     rendering code (that's what the GX2 ImGui backend already does, as a
     separate, parallel path). `GfxRenderingAPIGX2` is the layer a real N64
     decomp actually calls, through the Fast3D microcode interpreter, so it's
     the layer worth de-risking here. With no display list to drive it from,
     shader IDs for two minimal configs — untextured per-vertex-color, and
     textured with no vertex color — are hand-encoded the way
     `gfx_cc_get_features()` would decode them from a real one. Brings up
     `GfxWindowBackendWiiU(nullptr)` + `GfxRenderingAPIGX2`, cycles the clear
     color (previously hardcoded black in `ClearFramebuffer`; added
     `GfxRenderingAPIGX2::SetClearColor()` for this), CPU-transforms a
     rotating cube's vertices into clip space every frame and submits them via
     `DrawTriangles`, and separately drives
     `NewTexture`/`UploadTexture`/`SetSamplerParameters` with a hand-made
     checkerboard on a static quad. One finding from building this: GX2's
     vertex shader does no MVP multiply of its own
     (`generateVertexShader()` in `src/fast/backends/gx2_shader_gen.cpp`) — a
     real display list's CPU-side matrix stack has already put vertices in
     clip space by the time they reach `DrawTriangles()`, so the harness has
     to do the same multiply itself; this was confirmed by reading the
     shader-gen code, not assumed. Tears down OSScreen (it and GX2 can't both
     own the display — output moves to `WHBLogUdp` port 4405 and/or the
     results file) and layers a raw ImGui demo window on top using ImGui's
     built-in font (confirmed font-loading-free — `Fast3dGui`'s OTR-backed
     font path is a separate concern from ImGui's own default bitmap font).
     Entering this stage is a one-way trip for the run: OSScreen isn't torn
     back down once GX2 has taken the screen, so `B` exits the harness
     instead of returning to the menu.
   - **Stage 4 — full `Context` + mapping layer** (open): drive a
     `ControlDeck` via `Context::CreateDefaultInstance(...)` to exercise
     `mapping/wiiu/` end to end (built-in defaults, rumble). Confirmed during
     Stage 0 investigation: `CreateDefaultInstance` cannot succeed with zero
     archives — `ArchiveManager::Init` (`src/ship/resource/archive/ArchiveManager.cpp`)
     only marks itself initialized once at least one archive loads, so this
     stage needs either a minimal single-file `FolderArchive`-style directory,
     or bypassing `CreateDefaultInstance` for the lower-level
     `Context::CreateInstance(name, shortName, components)` overload,
     hand-assembling only the components the mapping layer actually needs.
2. Wii U input features not yet surfaced: the DRC's gyroscope (there is a
   `ControllerGyroMapping` interface waiting for it) and its touch screen.

Progress is driven through CI on real devkitPPC output: the devkitPPC toolchain
image cannot be pulled from the Claude Code sandbox (its Docker Hub blob CDN is
blocked by the egress policy), so the `build-wiiu` job is the compile loop for
the Wii U code.
