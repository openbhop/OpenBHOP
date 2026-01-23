/* -----------------------------------------------------------------------------
   bh_input.c
   ----------------------------------------------------------------------------- */
#include "bh_input.h"

#include <string.h>

enum
{
    BH_KEYSTATE_DOWN = 1,
    BH_KEYSTATE_IMPULSE_DOWN = 2,
    BH_KEYSTATE_IMPULSE_UP = 4,
};

/* -----------------------------------------------------------------------------
   Internal Helpers
   ----------------------------------------------------------------------------- */

static vec3 bh_input_apply_mouselook(vec3 angles_deg, vec2 mouse_delta_px, float sensitivity)
{
    /*
      Yaw + -> turn left (toward +Y)
      Pitch + -> look down
      Map positive mouse X to turning right (decreasing yaw).
    */
    angles_deg.y -= mouse_delta_px.x * sensitivity;
    angles_deg.x += mouse_delta_px.y * sensitivity;

    angles_deg.x = clampf(angles_deg.x, -89.0f, 89.0f);

    if (angles_deg.y > 180.0f)
        angles_deg.y -= 360.0f;
    if (angles_deg.y < -180.0f)
        angles_deg.y += 360.0f;

    return angles_deg;
}

static float bh_input_consume_key_fraction(BH_InputState *in, BH_Key key)
{
    /* Internal: Assumes 'in' is valid and 'key' is in range. */
    const uint8_t st = in->key_state[key];
    const bool down = (st & BH_KEYSTATE_DOWN) != 0;
    const bool impulse_down = (st & BH_KEYSTATE_IMPULSE_DOWN) != 0;
    const bool impulse_up = (st & BH_KEYSTATE_IMPULSE_UP) != 0;

    /*
      Quake/Source-style approximation:
        - down only                 -> 1.0
        - impulse down only         -> 0.5 (pressed sometime during interval)
        - impulse down + impulse up -> 0.25 (tapped within interval)
        - impulse up only           -> 0.0
    */
    float v;
    if (impulse_down && !impulse_up)
    {
        v = down ? 0.5f : 0.0f;
    }
    else if (impulse_up && !impulse_down)
    {
        v = 0.0f;
    }
    else if (!impulse_down && !impulse_up)
    {
        v = down ? 1.0f : 0.0f;
    }
    else
    {
        v = down ? 0.75f : 0.25f;
    }

    in->key_state[key] &= BH_KEYSTATE_DOWN;
    return v;
}

static bool bh_input_consume_pressed(BH_InputState *in, BH_Key key)
{
    /* Internal: Assumes 'in' is valid and 'key' is in range. */
    const bool pressed = (in->key_state[key] & BH_KEYSTATE_IMPULSE_DOWN) != 0;
    in->key_state[key] &= BH_KEYSTATE_DOWN;
    return pressed;
}

/* -----------------------------------------------------------------------------
   Public API
   ----------------------------------------------------------------------------- */

void BH_Input_Init(BH_InputState *in)
{
    if (!in)
        return;

    memset(in, 0, sizeof(*in));

    /* Treat mouse deltas as raw counts; apply deg-per-count manually. */
    in->mouse_sensitivity = 0.022f;
    in->view_angles_deg = (vec3){0.0f, 0.0f, 0.0f};
}

void BH_Input_BeginFrame(BH_InputState *in)
{
    if (!in)
        return;

    memset(in->pressed, 0, sizeof(in->pressed));
    memset(in->released, 0, sizeof(in->released));
    in->mouse_delta_px = (vec2){0.0f, 0.0f};
}

void BH_Input_UpdateViewAngles(BH_InputState *in)
{
    if (!in)
        return;

    in->view_angles_deg = bh_input_apply_mouselook(in->view_angles_deg, in->mouse_delta_px, in->mouse_sensitivity);
}

void BH_Input_SetKey(BH_InputState *in, BH_Key key, bool down)
{
    if (!in || (int)key < 0 || key >= BH_KEY_COUNT)
        return;

    const bool was_down = in->down[key];
    in->down[key] = down;

    if (down && !was_down)
    {
        in->pressed[key] = true;
    }
    else if (!down && was_down)
    {
        in->released[key] = true;
    }

    uint8_t st = in->key_state[key];
    if (down)
    {
        st |= BH_KEYSTATE_DOWN;
        if (!was_down)
        {
            st |= BH_KEYSTATE_IMPULSE_DOWN;
        }
    }
    else
    {
        st &= (uint8_t)~BH_KEYSTATE_DOWN;
        if (was_down)
        {
            st |= BH_KEYSTATE_IMPULSE_UP;
        }
    }
    in->key_state[key] = st;
}

BH_Intent BH_Input_BuildIntentFixed(BH_InputState *in)
{
    if (!in)
        return (BH_Intent){0};

    const bool quit = bh_input_consume_pressed(in, BH_KEY_ESCAPE);

    /* Treat non-zero key fractions as 'down' to catch fast taps. */
    const float jump_frac = bh_input_consume_key_fraction(in, BH_KEY_SPACE);
    const float duck_frac = bh_input_consume_key_fraction(in, BH_KEY_LCTRL);

    const float forward = bh_input_consume_key_fraction(in, BH_KEY_W) - bh_input_consume_key_fraction(in, BH_KEY_S);
    const float left = bh_input_consume_key_fraction(in, BH_KEY_A) - bh_input_consume_key_fraction(in, BH_KEY_D);
    const float up = bh_input_consume_key_fraction(in, BH_KEY_E) - bh_input_consume_key_fraction(in, BH_KEY_Q);

    const vec2 delta_accum = in->mouse_delta_accum_px;
    in->mouse_delta_accum_px = (vec2){0.0f, 0.0f};

    return (BH_Intent){.quit = quit,
                       .jump = jump_frac > 0.0f,
                       .duck = duck_frac > 0.0f,
                       .move_forward = forward,
                       .move_left = left,
                       .move_up = up,
                       .mouse_pos_px = in->mouse_pos_px,
                       .mouse_delta_px = delta_accum,
                       .view_angles_deg = in->view_angles_deg};
}

BH_Intent BH_Input_BuildIntentFrame(const BH_InputState *in)
{
    if (!in)
        return (BH_Intent){0};

    float forward = 0.0f;
    float left = 0.0f;
    float up = 0.0f;

    if (in->down[BH_KEY_W])
        forward += 1.0f;
    if (in->down[BH_KEY_S])
        forward -= 1.0f;
    if (in->down[BH_KEY_A])
        left += 1.0f;
    if (in->down[BH_KEY_D])
        left -= 1.0f;
    if (in->down[BH_KEY_E])
        up += 1.0f;
    if (in->down[BH_KEY_Q])
        up -= 1.0f;

    return (BH_Intent){.quit = in->pressed[BH_KEY_ESCAPE],
                       .jump = in->down[BH_KEY_SPACE],
                       .duck = in->down[BH_KEY_LCTRL],
                       .move_forward = forward,
                       .move_left = left,
                       .move_up = up,
                       .mouse_pos_px = in->mouse_pos_px,
                       .mouse_delta_px = in->mouse_delta_px,
                       .view_angles_deg = in->view_angles_deg};
}

void BH_Input_ClearFixedAccumulators(BH_InputState *in)
{
    if (!in)
        return;

    for (int k = 0; k < BH_KEY_COUNT; ++k)
    {
        in->key_state[k] &= BH_KEYSTATE_DOWN;
    }

    in->mouse_delta_accum_px = (vec2){0.0f, 0.0f};
}
