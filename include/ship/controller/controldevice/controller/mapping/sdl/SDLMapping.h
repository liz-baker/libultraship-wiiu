#pragma once

#ifndef __WIIU__
#include <SDL3/SDL.h>
#else
typedef int SDL_GamepadButton;
typedef int SDL_GamepadAxis;
#endif

// Axis / AxisDirection are shared with the non-SDL mapping backends.
#include "ship/controller/controldevice/controller/mapping/AxisDirection.h"
