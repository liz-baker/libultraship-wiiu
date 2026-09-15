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
  one isolates a failure before the next stage adds complexity. The harness's
  main screen is now a D-Pad/A text menu that picks which stage to test (`B`
  returns to it from any stage) rather than cycling through them with `+`, so
  the list can keep growing without becoming tedious to navigate. Full design,
  API references, and packaging notes are in the issue; short version:
  - [x] **Stage 0 — boot & link.** [PR #7](https://github.com/liz-baker/libultraship-wiiu/pull/7).
    OSScreen console, toolchain/heap info, SD write test. Links `libultraship`
    with `--whole-archive` so every Wii U object file's symbols get resolved —
    the first time anything has *linked* against the Wii U code, not just
    compiled it. Also wired `build-wiiu` to publish the `.wuhb` and the static
    lib as GitHub Releases (prerelease per commit to `main`, real release on
    `v*` tags) so hardware testing doesn't need a local devkitPro install.
  - [x] **Stage 1 — normalized input readout.** [PR #9](https://github.com/liz-baker/libultraship-wiiu/pull/9).
    Confirmed on hardware: GamePad button readout works cleanly via
    `GetDeviceName()`/`GetButtonsHeld()`/`GetAxisValue()`. Wii Remote, Nunchuk,
    Classic, and Pro Controller are still untested. (Unrelated observation from
    the hardware run: the screen tints reddish under the system HOME menu
    overlay — expected `OSScreen`-vs-system-overlay behavior, not a port bug,
    and moot once Stage 3 replaces `OSScreen` with GX2.)
  - [ ] **Stage 2 — AX audio** (harness code landed, not yet run on hardware).
    Instantiates a standalone `WiiUAudioPlayer` (no `Context` needed — just an
    `AudioSettings`) and feeds it a continuous, phase-continuous sweep
    (220–880 Hz) with the right channel offset from the left by a fixed pitch
    ratio, so a wrap/underrun click stands out against the smooth pitch change
    and a channel swap is audible throughout. `X` cycles which channel(s) are
    audible (Both → Left → Right → Both) to isolate one side at a time —
    both phase accumulators keep advancing while muted so toggling never
    introduces its own click. `Buffered()` is tracked for underrun count,
    min/max, and a near-ring-capacity flag, surfaced live on screen and in
    `results.txt` rather than relying on listening alone.
  - [ ] **Stage 3 — GX2 renderer** (harness code landed, not yet run on
    hardware). Drives `GfxRenderingAPIGX2` directly rather than raw GX2 calls,
    on purpose: raw GX2 would only prove the console can draw a triangle, not
    that libultraship's own rendering path works, and that's the layer a real
    N64 decomp actually calls (through the F3D microcode interpreter). There's
    no display list to drive it from, so shader IDs for two minimal configs
    (untextured per-vertex-color, and textured with no vertex color) are
    hand-encoded the way `gfx_cc_get_features()` would decode them from a
    real one. Brings up `GfxWindowBackendWiiU` + `GfxRenderingAPIGX2` and
    CPU-transforms a rotating cube's vertices into clip space each frame
    (GX2's vertex shader does no
    MVP multiply of its own — confirmed while building this, not assumed) and
    submits it via `DrawTriangles`, and separately exercises
    `NewTexture`/`UploadTexture`/`SetSamplerParameters` with a hand-made
    checkerboard on a static quad. Tears down OSScreen (conflicts with GX2 —
    logging moves to `WHBLogUdp`/results file) and layers a raw ImGui demo
    window on top using ImGui's built-in font (not `Fast3dGui`, which needs an
    OTR-backed font resource). Entering this stage is a one-way trip for the
    run — `B` exits the harness rather than returning to the OSScreen menu.
  - [ ] **Stage 4 — full `Context` + mapping layer.** Drive a `ControlDeck`
    through `Context::CreateDefaultInstance(...)` to exercise
    `mapping/wiiu/` end to end (built-in defaults, rumble). Open question,
    confirmed during Stage 0 investigation: `CreateDefaultInstance` cannot
    succeed with zero archives (`ArchiveManager::Init` requires at least one
    loaded archive) — this stage needs either a minimal single-file
    FolderArchive, or bypassing `CreateDefaultInstance` for the lower-level
    `Context::CreateInstance(name, shortName, components)` overload.

[Issue #14](https://github.com/liz-baker/libultraship-wiiu/issues/14) tracks a
follow-up once Stage 4 lands: Stages 1 and 2 currently exercise the raw Wii U
input/audio backends (`WiiUInput`, `WiiUAudioPlayer`) directly rather than
libultraship's `ControlDeck`/mapping and audio-manager abstractions, so they
don't yet catch bugs in those layers the way Stage 3's GX2 test catches bugs
in `GfxRenderingAPIGX2`.

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
