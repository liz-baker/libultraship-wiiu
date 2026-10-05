#pragma once

#ifndef __WIIU__
#include <SDL2/SDL.h>
#else
typedef int SDL_GameControllerButton;
typedef int SDL_GameControllerAxis;
#endif

// Axis / AxisDirection are shared with the non-SDL mapping backends.
#include "ship/controller/controldevice/controller/mapping/AxisDirection.h"
