# Wii U port status (`port-maintenance` backport)

This branch carries the Wii U (Cafe / `CafeOS`) port on top of upstream
[`Kenix3/libultraship`](https://github.com/Kenix3/libultraship)'s
**`port-maintenance`** branch, which is the line
[Shipwright](https://github.com/HarbourMasters/Shipwright) tracks. It is a
backport of the port developed on this fork's `main`, which is based on
upstream `main` (the 2.0 line: SDL3, component system, `Ship`/`Fast` namespace
split).

## How this differs from the `main`-based port

| Concern | `port-maintenance` (this branch) | `main` |
| --- | --- | --- |
| Windowing / input | SDL2 | SDL3 |
| Component wiring | `Context::GetRawInstance()` accessors | injected `controlDeck` / `consoleVariable` |
| Fast3D backends | `src/fast/backends/`, class-based `GfxRenderingAPI` / `GfxWindowBackend` | same |
| Header layout | `include/ship/Context.h` | `include/ship/core/Context.h` |

The Wii U-specific sources (GX2 renderer, Wii U window backend, VPAD/KPAD
input layer, `mapping/wiiu`, AX audio player, GX2 ImGui backends) are the same
code as on `main`; the shared-file edits were re-applied by hand against this
branch's SDL2-era APIs. The mapping classes and factories were de-injected to
use `Context::GetRawInstance()`.

devkitPro ships SDL2 but the port deliberately does not use it: input is native
VPAD/KPAD (`src/ship/port/wiiu/WiiUImpl.cpp`, normalized by `WiiUInput.cpp`),
audio is native AX (`src/ship/audio/WiiUAudioPlayer.cpp`), and the window is
GX2. SDL is guarded out with `__WIIU__` / `CafeOS`.

## Status

**Not yet built.** The sandbox this branch was written in has no devkitPPC
toolchain, so the first compile is the `build-wiiu` CI job. Expect a round of
compile fixes. Nothing has been run on hardware.

## Deliberately left out of this backport

- **Wii U crash handler** (`OSSetExceptionCallbackEx` register/backtrace report).
  `port-maintenance`'s `CrashHandler` is Linux/Windows only and compiles to a
  no-op on `CafeOS`; the `main`-based handler would need rewriting for the
  non-Component `CrashHandler` here.
- Unrelated `main` changes (renderer fixes, Indy ucode work, tests, multi-port
  controller-bit fixes).

## Remaining work

1. Get `build-wiiu` green and fix compile errors from the backport.
2. Runtime validation on real hardware (GX2 renderer, input, audio).
3. DRC gyroscope and touch screen (`ControllerGyroMapping` exists for gyro;
   there is no touch mapping abstraction yet).
4. Shipwright's own Wii U code (`SohInputEditorWindow`) was written against the
   2024 libultraship-wiiu `ShipDeviceIndexToWiiUDeviceIndexMapping` API and
   needs adapting to the `mapping/wiiu` classes here.
