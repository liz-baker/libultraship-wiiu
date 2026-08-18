# CLAUDE.md

Working notes for this fork of libultraship. This fork tracks upstream
(`Kenix/libultraship`) while adding **Wii U (CafeOS)** support. See
[`docs/wiiu-port-status.md`](docs/wiiu-port-status.md) for the port design and
remaining work.

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
3. **`.github/workflows/build-wiiu.yml`** — the `Build (work in progress)` step
   is `continue-on-error: true` so the validated toolchain/dependency configure
   can land while the port is finished. *Restore:* remove `continue-on-error`
   once the Wii U library compiles end to end, so the compile becomes blocking.
4. **`.github/workflows/tidy-format-validation.yml`** — `src/ship/port/wiiu/*`
   is excluded from the clang-tidy-diff step because those files include
   devkitPPC-only headers unavailable on the Linux tidy host. This exclusion is
   fine to keep, but revisit if the wiiu port sources should be tidied via a
   cross-toolchain setup.

Still active on PRs during the port: `build-wiiu` and `tidy-format-validation`
(clang-format + clang-tidy). Docs workflows are path-filtered and only run when
`docs/**` changes.

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
