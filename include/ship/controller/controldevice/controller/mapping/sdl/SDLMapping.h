#pragma once

#ifndef __WIIU__
#include <SDL2/SDL.h>
#elif __has_include(<SDL2/SDL_gamecontroller.h>)
// The Wii U port does not use SDL itself, but a game built with it may (devkitPro ships an SDL2 port).
// Use the real types when they are available so the two declarations agree.
#include <SDL2/SDL_gamecontroller.h>
#else
typedef int SDL_GameControllerButton;
typedef int SDL_GameControllerAxis;
#endif

// Axis / AxisDirection are shared with the non-SDL mapping backends.
#include "ship/controller/controldevice/controller/mapping/AxisDirection.h"
