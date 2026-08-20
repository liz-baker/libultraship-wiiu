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
  (`controller/.../mapping/sdl`) are excluded from the Wii U build.
- Input is read natively through VPAD/KPAD (`src/ship/port/wiiu/WiiUImpl.cpp`).

## What is in place

- **Toolchain / CMake wiring** — `CafeOS` is detected throughout the build.
  `cmake/dependencies/wiiu.cmake` fetches and patches `spdlog` (no TLS / thread
  id) and `thread-pool` (no `thread_local`) and pulls `nlohmann_json`. The
  common dependency file guards the desktop ImGui backends, STB thread-locals,
  and the default thread-pool for `CafeOS`.
- **Native platform layer** — `src/ship/port/wiiu/WiiUImpl.{h,cpp}` (logging,
  working directory, native VPAD/KPAD polling).
- **Ported GX2 graphics sources** — the GX2 renderer, Wii U window backend,
  GX2 shader generation/util, and the GX2 ImGui backends are re-homed into
  `src/fast/backends/` and `src/ship/port/wiiu/ImGui/`.
- **CI** — `.github/workflows/build-wiiu.yml` cross-compiles inside the
  `devkitpro/devkitppc` container.

## Status

**Phases A and B are complete.** The core `libultraship.a` compiles and links end
to end for `CafeOS`, including the GX2 renderer and Wii U window backend. SDL3 is
fully guarded out of the always-compiled layers, and the single blocking
`build-wiiu` CI job covers the whole console build.

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

## Remaining work

1. **Phase C — native input/audio.** Wire native VPAD/KPAD input into the
   controller layer (a Wii U physical-device backend replacing the SDL mapping)
   and add a native AX audio player, so the build is functional, not just
   compiling.
2. **Phase D — finalize CI.** Re-enable the desktop `build-validation` /
   `test-validation` PR triggers (see the revert checklist in `CLAUDE.md`).
3. Runtime validation on real hardware — the port is compile-clean, but the GX2
   renderer has not yet been exercised on a console.

Progress is driven through CI on real devkitPPC output: the devkitPPC toolchain
image cannot be pulled from the Claude Code sandbox (its Docker Hub blob CDN is
blocked by the egress policy), so the `build-wiiu` job is the compile loop for
the Wii U code.
