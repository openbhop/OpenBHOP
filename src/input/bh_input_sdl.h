/* -----------------------------------------------------------------------------
   bh_input_sdl.h
   ----------------------------------------------------------------------------- */
#pragma once

#include "../input/bh_input.h"

#include <SDL3/SDL.h>

/* -----------------------------------------------------------------------------
    Public API
    ----------------------------------------------------------------------------- */

/* Extracts target window ID from generic SDL events. Returns 0 if inapplicable. */
SDL_WindowID BH_Input_WindowIDFromSDLEvent(const SDL_Event *e);

/* Translates SDL events to internal input state. */
void BH_Input_ProcessSDLEvent(BH_InputState *in, const SDL_Event *e);