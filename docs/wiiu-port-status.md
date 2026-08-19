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

**Phase A is complete.** The core `libultraship.a` compiles and links end to end
for `CafeOS` (GX2 renderer gated behind `LUS_WIIU_GX2`, off by default), and the
`build-wiiu` CI compile is now a blocking check. SDL3 is fully guarded out of the
always-compiled layers.

## Remaining work

The GX2 renderer / window backend are still written against the old C
struct-of-function-pointers Fast3D API. They are compiled only when
`-DLUS_WIIU_GX2=ON` is passed, so the rest of the port was brought to a
clean compile first. Outstanding items:

1. Convert `gfx_gx2` to a `GfxRenderingAPIGX2 : public Fast::GfxRenderingAPI`
   class and `gfx_wiiu` to a `GfxWindowBackendWiiU : public Fast::GfxWindowBackend`
   class, matching the current virtual interfaces.
2. Fix include paths / renamed headers (`gfx_pc.h`, `gfx_cc.h`,
   `consolevariablebridge.h`, etc.) for the new layout.
3. Add a `FAST3D_GX2` entry to the `WindowBackend` enum and wire it into
   `Fast3dWindow`'s backend selection.
4. Finish guarding remaining SDL3 usages in `src/ship` (audio, core, utils) and
   provide native replacements where needed.
5. Verify the `ppc-tinyxml2` / `ppc-libzip` portlibs satisfy the core's
   `find_package` calls on `CafeOS`.

Progress is driven through the `build-wiiu` CI job, which surfaces the concrete
remaining compile errors on real toolchain output.
