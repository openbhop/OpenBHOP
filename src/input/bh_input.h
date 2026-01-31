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

#ifndef BH_INPUT_EVENT_CAPACITY
#define BH_INPUT_EVENT_CAPACITY 8192u
#endif

typedef enum BH_InputEventType
{
    BH_INPUT_EVENT_KEY = 1,
    BH_INPUT_EVENT_MOUSE_MOTION = 2,
} BH_InputEventType;

typedef struct BH_InputEvent
{
    uint64_t timestamp_ns;
    BH_InputEventType type;
    union {
        struct
        {
            uint8_t key;
            uint8_t down;
        } key;

        struct
        {
            float x;
            float y;
            float dx;
            float dy;
        } motion;
    } u;
} BH_InputEvent;

typedef struct BH_InputState
{
    bool down[BH_KEY_COUNT];
    bool pressed[BH_KEY_COUNT];
    bool released[BH_KEY_COUNT];

    /* Current cursor position (window-space). */
    vec2 mouse_pos_px;

    /* Cursor position as of the last fixed-tick boundary (for fixed intents). */
    vec2 mouse_pos_fixed_px;

    /* Per-render-frame mouse delta accumulator (used for frame-accurate camera). */
    vec2 mouse_delta_px;

    /* Frame-accurate view angles (updated once per render frame). */
    vec3 view_angles_frame_deg;

    /* Fixed-tick view angles (updated per fixed tick from queued mouse motion). */
    vec3 view_angles_fixed_deg;

    float mouse_sensitivity;

    /* Monotonic time (nanoseconds) associated with the latest BeginFrame(). */
    uint64_t now_ns;

    /* Fixed-step timing (nanoseconds). */
    uint64_t fixed_dt_ns;
    uint64_t fixed_time_ns; /* End timestamp of the last generated fixed intent. */

    /* Key hold state at the fixed timestep boundary (maintained by fixed consumption). */
    bool fixed_down[BH_KEY_COUNT];

    /* ------------------------------------------------------------
       Input event queue (ring buffer)
       ------------------------------------------------------------ */

    BH_InputEvent events[BH_INPUT_EVENT_CAPACITY];
    uint32_t event_read;
    uint32_t event_write;
    uint32_t event_count;
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

/*
    Initialize input state.

    now_ns should be based on the same time source as platform event timestamps
    (e.g. SDL_GetTicksNS() when using the SDL backend).
*/
void BH_Input_Init(BH_InputState *in, uint64_t now_ns, double fixed_dt_s);

/* Called once per render frame before platform events are processed. */
void BH_Input_BeginFrame(BH_InputState *in, uint64_t now_ns);
void BH_Input_UpdateViewAnglesFrame(BH_InputState *in);

/* Sets both frame and fixed view angles (useful for teleport/reset). */
void BH_Input_SetViewAngles(BH_InputState *in, vec3 angles_deg);

/* Platform event injection */
void BH_Input_SetKey(BH_InputState *in, BH_Key key, bool down, uint64_t timestamp_ns);

void BH_Input_AddMouseMotion(BH_InputState *in, vec2 pos_px, vec2 delta_px, uint64_t timestamp_ns);

/* Generates fixed-step simulation command; consumes accumulators. */
BH_Intent BH_Input_BuildIntentFixed(BH_InputState *in);

/* Generates frame-step command (UI/Debug); preserves accumulators. */
BH_Intent BH_Input_BuildIntentFrame(const BH_InputState *in);

/* Flushes fixed-tick accumulators; preserves hold state. */
void BH_Input_ClearFixedAccumulators(BH_InputState *in);
