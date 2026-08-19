#pragma once

#ifndef __WIIU__
#include <SDL3/SDL.h>
#else
// SDL3 is unavailable on the Wii U. The SDL mapping classes are not built there
// (their .cpp files are excluded and construction is guarded out), but the
// headers are still pulled in transitively; alias the gamepad enums to int so
// they parse. A native VPAD/KPAD mapping backend replaces them at runtime.
typedef int SDL_GamepadButton;
typedef int SDL_GamepadAxis;
#endif

namespace Ship {

/** @brief Identifies a two-dimensional axis component. */
enum Axis { X = 0, Y = 1 };

/** @brief Identifies the sign of an axis value (negative or positive half). */
enum AxisDirection { NEGATIVE = -1, POSITIVE = 1 };

} // namespace Ship
