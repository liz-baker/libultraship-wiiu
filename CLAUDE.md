# CLAUDE.md

Working notes for this fork of libultraship. This fork tracks upstream
(`Kenix/libultraship`) while adding **Wii U (CafeOS)** support. See
[`docs/wiiu-port-status.md`](docs/wiiu-port-status.md) for the port design and
remaining work.

## Wii U port TO-DO

Roadmap to a working Wii U build, in dependency order. Keep this list current as
phases land.

- [x] **Phase A — Guard SDL3 out so the core compiles for Wii U.** Done: the
  core `libultraship.a` compiles and links end to end for `CafeOS` (GX2 renderer
  gated behind `LUS_WIIU_GX2`, off). SDL3 is guarded out of the always-compiled
  layers — the Fast3D GUI/window layer, audio (falls back to the null player),
  the controller/physical-device layer and mapping factories, the libultra OS
  shim, and the crash handler. The `build-wiiu` compile step is now blocking.
- [ ] **Phase B — Convert the GX2 backends to the class-based Fast3D API**
  (the `LUS_WIIU_GX2` code). `gfx_gx2` → `GfxRenderingAPIGX2 : GfxRenderingAPI`,
  `gfx_wiiu` → `GfxWindowBackendWiiU : GfxWindowBackend`; fix renamed includes;
  add `FAST3D_GX2` to the `WindowBackend` enum and wire it into `Fast3dWindow` /
  `Fast3dGui`; port the GX2 ImGui backends to the current ImGui.
- [ ] **Phase C — Native input/audio.** Wire native VPAD/KPAD input into the
  controller layer (a Wii U physical-device backend replacing the SDL mapping)
  and add a native AX audio player, so the build is functional, not just
  compiling.
- [ ] **Phase D — Finalize CI.** Remove `continue-on-error` from the
  `build-wiiu` compile step and re-enable the desktop `build-validation` /
  `test-validation` PR triggers (see the revert checklist below).

## ⚠️ Temporary CI changes to revert before finishing the Wii U port

While the Wii U port is in progress, PR CI is intentionally kept focused on the
`build-wiiu` job so the desktop matrix doesn't run on every push to the port
branch. **These changes are temporary and must be reverted** once the port is
ready (or whenever full PR validation is wanted again):

1. **`.github/workflows/build-validation.yml`** — trigger changed to
   `push: branches: [main]` only; the `pull_request:` trigger is commented out.
   *Restore:* re-enable `pull_request:` and drop the `branches` filter so it
   runs on all pushes and PRs again.
2. **`.github/workflows/test-validation.yml`** — same change as above.
   *Restore:* same as above.
3. ~~**`.github/workflows/build-wiiu.yml`** — the build step was
   `continue-on-error`.~~ Done (Phase A): the Wii U library compiles and links
   end to end, so the `Build` step is now blocking.
5. **`.github/workflows/build-wiiu.yml`** — a second job, `build-wiiu-gx2`,
   configures with `-DLUS_WIIU_GX2=ON` and builds the experimental GX2 renderer
   / Wii U window backend with `continue-on-error: true` (Ninja `-k 0`). It is a
   Phase B diagnostic that surfaces the remaining GX2 compile errors on real
   devkitPPC output without blocking PRs. *Restore (Phase B/D):* once the GX2
   backend compiles, make the build step blocking — ideally by folding
   `-DLUS_WIIU_GX2=ON` into the main `build-wiiu` job (or dropping the separate
   job) so the console build is validated as one blocking check.
4. **`.github/workflows/tidy-format-validation.yml`** — `src/ship/port/wiiu/*`
   is excluded from the clang-tidy-diff step because those files include
   devkitPPC-only headers unavailable on the Linux tidy host. This exclusion is
   fine to keep, but revisit if the wiiu port sources should be tidied via a
   cross-toolchain setup.

Still active on PRs during the port: `build-wiiu` (blocking), `build-wiiu-gx2`
(non-blocking GX2 diagnostic) and `tidy-format-validation` (clang-format +
clang-tidy). Docs workflows are path-filtered and only run when `docs/**`
changes.

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
  is native VPAD/KPAD (`src/ship/port/wiiu/WiiUImpl.cpp`).
- The GX2 renderer / window backend are gated behind `-DLUS_WIIU_GX2=ON` (off by
  default) until converted to the class-based `GfxRenderingAPI` /
  `GfxWindowBackend` interfaces.

## Conventions

- C++ is formatted with **clang-format-14** (`.clang-format`). Run it on changed
  files before pushing or `tidy-format` will fail.
