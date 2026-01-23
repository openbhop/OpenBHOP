/* -----------------------------------------------------------------------------
   bh_input.h
   ----------------------------------------------------------------------------- */
#pragma once

#include "../core/bh_core.h"
#include "../math/bh_math.h"

/* -----------------------------------------------------------------------------
    Types
    ----------------------------------------------------------------------------- */

typedef enum BH_Key
{
    BH_KEY_W,
    BH_KEY_A,
    BH_KEY_S,
    BH_KEY_D,
    BH_KEY_Q,
    BH_KEY_E,
    BH_KEY_N,

    BH_KEY_SPACE,
    BH_KEY_LCTRL,
    BH_KEY_LSHIFT,

    BH_KEY_ESCAPE,
    BH_KEY_LEFT,
    BH_KEY_RIGHT,
    BH_KEY_UP,
    BH_KEY_DOWN,

    BH_KEY_COUNT
} BH_Key;

typedef struct BH_InputState
{
    bool down[BH_KEY_COUNT];
    bool pressed[BH_KEY_COUNT];
    bool released[BH_KEY_COUNT];

    /* Fixed-tick accumulation bits:
       0x01 = Down
       0x02 = Impulse Down (Pressed since last tick)
       0x04 = Impulse Up   (Released since last tick)
    */
    uint8_t key_state[BH_KEY_COUNT];

    vec2 mouse_pos_px;
    vec2 mouse_delta_px;       /* Frame accumulator */
    vec2 mouse_delta_accum_px; /* Fixed-tick accumulator */
    vec3 view_angles_deg;
    float mouse_sensitivity;
} BH_InputState;

typedef struct BH_Intent
{
    bool quit;
    bool jump;
    bool duck;

    float move_forward;
    float move_left;
    float move_up;

    vec2 mouse_pos_px;
    vec2 mouse_delta_px;
    vec3 view_angles_deg;
} BH_Intent;

/* -----------------------------------------------------------------------------
   Public API
   ----------------------------------------------------------------------------- */

void BH_Input_Init(BH_InputState *in);
void BH_Input_BeginFrame(BH_InputState *in);
void BH_Input_UpdateViewAngles(BH_InputState *in);

/* Platform event injection */
void BH_Input_SetKey(BH_InputState *in, BH_Key key, bool down);

/* Generates fixed-step simulation command; consumes accumulators. */
BH_Intent BH_Input_BuildIntentFixed(BH_InputState *in);

/* Generates frame-step command (UI/Debug); preserves accumulators. */
BH_Intent BH_Input_BuildIntentFrame(const BH_InputState *in);

/* Flushes fixed-tick accumulators; preserves hold state. */
void BH_Input_ClearFixedAccumulators(BH_InputState *in);
