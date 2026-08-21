# CLAUDE.md

Working notes for this fork of libultraship. This fork tracks upstream
(`Kenix/libultraship`) while adding **Wii U (CafeOS)** support. See
[`docs/wiiu-port-status.md`](docs/wiiu-port-status.md) for the port design and
remaining work.

## Wii U port TO-DO

Roadmap to a working Wii U build, in dependency order. Keep this list current as
phases land.

- [x] **Phase A — Guard SDL3 out so the core compiles for Wii U.** Done: the
  core `libultraship.a` compiles and links end to end for `CafeOS` (with the GX2
  renderer still excluded). SDL3 is guarded out of the always-compiled
  layers — the Fast3D GUI/window layer, audio (falls back to the null player),
  the controller/physical-device layer and mapping factories, the libultra OS
  shim, and the crash handler. The `build-wiiu` compile step is now blocking.
- [x] **Phase B — Convert the GX2 backends to the class-based Fast3D API.**
  Done: `gfx_gx2` → `GfxRenderingAPIGX2 : GfxRenderingAPI` and `gfx_wiiu` →
  `GfxWindowBackendWiiU : GfxWindowBackend`, includes re-rooted, `FAST3D_GX2`
  added to the `WindowBackend` enum and wired into `Fast3dWindow` / `Fast3dGui`,
  the GX2 ImGui backends ported to the current ImGui, and `gx2_shader_gen`
  converted to C++. The GX2 renderer compiles and links end to end, so the
  `LUS_WIIU_GX2` scaffolding option was dropped — the backend now builds
  unconditionally on `CafeOS` — and the single blocking `build-wiiu` CI job
  covers it.
- [x] **Phase C — Native input/audio.** Done: `PhysicalDeviceType::WiiUGamepad`
  and a `mapping/wiiu/` backend (button / axis-direction / rumble mappings) feed
  the controller layer from a normalized VPAD + KPAD input layer
  (`ship/port/wiiu/WiiUInput.h`), with built-in Wii U defaults; audio plays
  through a native AX player (`AudioBackend::AX`). Not yet covered: DRC gyro and
  the touch screen, and the port has still not been run on hardware.
- [x] **Phase D — Finalize CI.** Done: the desktop `build-validation` /
  `test-validation` workflows are back to running on every push and PR (see
  the (now-historical) revert checklist below). The `build-wiiu` compile step
  stays blocking and covers the GX2 configuration. Full PR CI — desktop
  matrix, `build-wiiu`, and `tidy-format-validation` — is active again.
- [ ] **Phase E — Wii U on-hardware test harness.** [Issue #5](https://github.com/liz-baker/libultraship-wiiu/issues/5).
  The port is compile-clean end to end but **nothing in it has ever executed**
  on a console or emulator. `tools/wiiu-harness/` builds a loadable `.wuhb`
  (target: Aroma, plus `wiiload` for iteration) in 5 stages, ordered so each
  one isolates a failure before the next stage adds complexity. Full design,
  API references, and packaging notes are in the issue; short version:
  - [x] **Stage 0 — boot & link.** [PR #7](https://github.com/liz-baker/libultraship-wiiu/pull/7).
    OSScreen console, toolchain/heap info, SD write test. Links `libultraship`
    with `--whole-archive` so every Wii U object file's symbols get resolved —
    the first time anything has *linked* against the Wii U code, not just
    compiled it. Also wired `build-wiiu` to publish the `.wuhb` and the static
    lib as GitHub Releases (prerelease per commit to `main`, real release on
    `v*` tags) so hardware testing doesn't need a local devkitPro install.
  - [ ] **Stage 1 — normalized input readout.** [PR #9](https://github.com/liz-baker/libultraship-wiiu/pull/9)
    (harness code landed, not yet run on hardware). Highest-value remaining
    stage: live-prints `GetDeviceName()`/`GetButtonsHeld()`/`GetAxisValue()`
    from `WiiUInput.h` for GamePad, Wii Remote, Nunchuk, Classic, and Pro
    Controller. Validates the button tables in `WiiUInput.cpp`, which have
    compiled but were never semantically checked against real hardware.
  - [ ] **Stage 2 — AX audio.** Instantiate `WiiUAudioPlayer` standalone (no
    `Context` needed — just an `AudioSettings`), play a generated 440 Hz sine,
    watch `Buffered()` for underrun/overrun/ring-wrap.
  - [ ] **Stage 3 — GX2 renderer.** Tear down OSScreen (conflicts with GX2 —
    switch logging to `WHBLogUdp`/results file), bring up
    `GfxWindowBackendWiiU` + `GfxRenderingAPIGX2`, cycling clear color, then a
    raw ImGui demo window using ImGui's built-in font (not `Fast3dGui`, which
    needs an OTR-backed font resource).
  - [ ] **Stage 4 — full `Context` + mapping layer.** Drive a `ControlDeck`
    through `Context::CreateDefaultInstance(...)` to exercise
    `mapping/wiiu/` end to end (built-in defaults, rumble). Open question,
    confirmed during Stage 0 investigation: `CreateDefaultInstance` cannot
    succeed with zero archives (`ArchiveManager::Init` requires at least one
    loaded archive) — this stage needs either a minimal single-file
    FolderArchive, or bypassing `CreateDefaultInstance` for the lower-level
    `Context::CreateInstance(name, shortName, components)` overload.

## ⚠️ Temporary CI changes made during the Wii U port (now reverted)

While the Wii U port was in progress, PR CI was intentionally kept focused on
the `build-wiiu` job so the desktop matrix didn't run on every push to the
port branch. All of these have been reverted now that the port is compile-clean
end to end:

1. ~~**`.github/workflows/build-validation.yml`** — trigger changed to
   `push: branches: [main]` only; the `pull_request:` trigger was commented
   out.~~ Reverted (Phase D): back to `push:` / `pull_request:` on every branch.
2. ~~**`.github/workflows/test-validation.yml`** — same change as above.~~
   Reverted (Phase D): same as above.
3. ~~**`.github/workflows/build-wiiu.yml`** — the build step was
   `continue-on-error`.~~ Done (Phase A): the Wii U library compiles and links
   end to end, so the `Build` step is now blocking.
4. ~~**`.github/workflows/build-wiiu.yml`** — a second, non-blocking
   `build-wiiu-gx2` job built the GX2 renderer as a Phase B diagnostic.~~ Done
   (Phase B): the GX2 backend compiles and links, so the separate job was
   dropped and the `LUS_WIIU_GX2` option removed — the console build is now
   validated as one check.
5. **`.github/workflows/tidy-format-validation.yml`** — `src/ship/port/wiiu/*`
   is excluded from the clang-tidy-diff step because those files include
   devkitPPC-only headers unavailable on the Linux tidy host. This exclusion is
   fine to keep, but revisit if the wiiu port sources should be tidied via a
   cross-toolchain setup.

Active on PRs: `build-validation`, `test-validation`, `build-wiiu` (all
blocking) and `tidy-format-validation` (clang-format + clang-tidy). Docs
workflows are path-filtered and only run when `docs/**` changes.

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

## Conventions

- C++ is formatted with **clang-format-14** (`.clang-format`). Run it on changed
  files before pushing or `tidy-format` will fail.
