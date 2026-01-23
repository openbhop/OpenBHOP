/* -----------------------------------------------------------------------------
   bh_input_sdl.c
   ----------------------------------------------------------------------------- */
#include "bh_input_sdl.h"

/* -----------------------------------------------------------------------------
    Internal Helpers
    ----------------------------------------------------------------------------- */

static bool bh_map_scancode(SDL_Scancode sc, BH_Key *out_key)
{
    /* Internal: out_key assumed valid. */
    switch (sc)
    {
    case SDL_SCANCODE_W:
        *out_key = BH_KEY_W;
        return true;
    case SDL_SCANCODE_A:
        *out_key = BH_KEY_A;
        return true;
    case SDL_SCANCODE_S:
        *out_key = BH_KEY_S;
        return true;
    case SDL_SCANCODE_D:
        *out_key = BH_KEY_D;
        return true;
    case SDL_SCANCODE_Q:
        *out_key = BH_KEY_Q;
        return true;
    case SDL_SCANCODE_E:
        *out_key = BH_KEY_E;
        return true;
    case SDL_SCANCODE_N:
        *out_key = BH_KEY_N;
        return true;
    case SDL_SCANCODE_SPACE:
        *out_key = BH_KEY_SPACE;
        return true;
    case SDL_SCANCODE_LCTRL:
        *out_key = BH_KEY_LCTRL;
        return true;
    case SDL_SCANCODE_RCTRL:
        *out_key = BH_KEY_LCTRL;
        return true;
    case SDL_SCANCODE_LSHIFT:
        *out_key = BH_KEY_LSHIFT;
        return true;
    case SDL_SCANCODE_RSHIFT:
        *out_key = BH_KEY_LSHIFT;
        return true;
    case SDL_SCANCODE_ESCAPE:
        *out_key = BH_KEY_ESCAPE;
        return true;
    case SDL_SCANCODE_LEFT:
        *out_key = BH_KEY_LEFT;
        return true;
    case SDL_SCANCODE_RIGHT:
        *out_key = BH_KEY_RIGHT;
        return true;
    case SDL_SCANCODE_UP:
        *out_key = BH_KEY_UP;
        return true;
    case SDL_SCANCODE_DOWN:
        *out_key = BH_KEY_DOWN;
        return true;
    default:
        return false;
    }
}

/* -----------------------------------------------------------------------------
   Public API
   ----------------------------------------------------------------------------- */

SDL_WindowID BH_Input_WindowIDFromSDLEvent(const SDL_Event *e)
{
    if (!e)
        return 0;

    switch (e->type)
    {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        return e->key.windowID;
    case SDL_EVENT_TEXT_INPUT:
        return e->text.windowID;
    case SDL_EVENT_MOUSE_MOTION:
        return e->motion.windowID;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return e->button.windowID;
    case SDL_EVENT_MOUSE_WHEEL:
        return e->wheel.windowID;
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    case SDL_EVENT_WINDOW_MOVED:
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_MAXIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_EXPOSED:
        return e->window.windowID;
    default:
        return 0;
    }
}

void BH_Input_ProcessSDLEvent(BH_InputState *in, const SDL_Event *e)
{
    if (!in || !e)
        return;

    if (e->type == SDL_EVENT_KEY_DOWN || e->type == SDL_EVENT_KEY_UP)
    {
        BH_Key key;
        if (bh_map_scancode(e->key.scancode, &key))
        {
            BH_Input_SetKey(in, key, (e->type == SDL_EVENT_KEY_DOWN));
        }
    }

    if (e->type == SDL_EVENT_MOUSE_MOTION)
    {
        /* SDL3 provides window-space coordinates; rel is delta. */
        in->mouse_pos_px = (vec2){(float)e->motion.x, (float)e->motion.y};

        const float dx = (float)e->motion.xrel;
        const float dy = (float)e->motion.yrel;

        in->mouse_delta_px.x += dx;
        in->mouse_delta_px.y += dy;

        /* Accumulate across render frames until a fixed tick consumes it. */
        in->mouse_delta_accum_px.x += dx;
        in->mouse_delta_accum_px.y += dy;
    }
}