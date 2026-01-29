/* -----------------------------------------------------------------------------
   bh_input.c
   ----------------------------------------------------------------------------- */
#include "bh_input.h"

#include <string.h>

/* -----------------------------------------------------------------------------
   Internal Helpers
   ----------------------------------------------------------------------------- */

static vec3 bh_input_apply_mouselook(vec3 angles_deg, vec2 mouse_delta_px, float sensitivity)
{
    angles_deg.y -= mouse_delta_px.x * sensitivity;
    angles_deg.x += mouse_delta_px.y * sensitivity;

    angles_deg.x = clampf(angles_deg.x, -89.0f, 89.0f);

    if (angles_deg.y > 180.0f)
        angles_deg.y -= 360.0f;
    if (angles_deg.y < -180.0f)
        angles_deg.y += 360.0f;

    return angles_deg;
}

static inline uint32_t bh_rb_next(uint32_t i)
{
    return (uint32_t)((i + 1u) % (uint32_t)BH_INPUT_EVENT_CAPACITY);
}

static inline uint32_t bh_rb_prev(uint32_t i)
{
    return (i == 0u) ? (uint32_t)(BH_INPUT_EVENT_CAPACITY - 1u) : (i - 1u);
}

static void bh_input_queue_push(BH_InputState *in, const BH_InputEvent *ev)
{
    if (!in || !ev)
        return;

    /*
        Overflow is extremely unlikely with BH_INPUT_EVENT_CAPACITY=8192, but
        we still need to stay safe. We prefer to preserve the newest samples.

        If the queue is full and we're pushing mouse motion, attempt a lossless
        coalesce with the last queued motion event when the timestamp matches.
        (Coalescing across different timestamps would break fixed-tick
        time-slicing.)
    */
    if (in->event_count >= (uint32_t)BH_INPUT_EVENT_CAPACITY)
    {
        if (ev->type == BH_INPUT_EVENT_MOUSE_MOTION && in->event_count > 0u)
        {
            const uint32_t last_i = bh_rb_prev(in->event_write);
            BH_InputEvent *last = &in->events[last_i];
            if (last->type == BH_INPUT_EVENT_MOUSE_MOTION && last->timestamp_ns == ev->timestamp_ns)
            {
                last->u.motion.dx += ev->u.motion.dx;
                last->u.motion.dy += ev->u.motion.dy;
                last->u.motion.x = ev->u.motion.x;
                last->u.motion.y = ev->u.motion.y;
                return;
            }
        }

        /* Drop oldest event. */
        in->event_read = bh_rb_next(in->event_read);
        in->event_count--;
    }

    in->events[in->event_write] = *ev;
    in->event_write = bh_rb_next(in->event_write);
    in->event_count++;
}

static bool bh_input_queue_peek(const BH_InputState *in, BH_InputEvent *out)
{
    if (!in || !out || in->event_count == 0u)
        return false;
    *out = in->events[in->event_read];
    return true;
}

static void bh_input_queue_pop(BH_InputState *in)
{
    if (!in || in->event_count == 0u)
        return;
    in->event_read = bh_rb_next(in->event_read);
    in->event_count--;
}

/* -----------------------------------------------------------------------------
   Public API
   ----------------------------------------------------------------------------- */

void BH_Input_Init(BH_InputState *in, uint64_t now_ns, double fixed_dt_s)
{
    if (!in)
        return;

    memset(in, 0, sizeof(*in));

    /* Treat mouse deltas as raw counts; apply deg-per-count manually. */
    in->mouse_sensitivity = 0.022f;

    in->view_angles_frame_deg = (vec3){0.0f, 0.0f, 0.0f};
    in->view_angles_fixed_deg = (vec3){0.0f, 0.0f, 0.0f};

    in->now_ns = now_ns;

    const double step_s = (fixed_dt_s > 0.0) ? fixed_dt_s : (1.0 / 100.0);
    const double step_ns_f = step_s * 1e9;
    uint64_t step_ns = (uint64_t)(step_ns_f + 0.5);
    if (step_ns == 0u)
        step_ns = 1u;

    in->fixed_dt_ns = step_ns;
    in->fixed_time_ns = now_ns;

    /* Keep fixed boundary state consistent with the current real-time state. */
    memcpy(in->fixed_down, in->down, sizeof(in->fixed_down));

    in->event_read = 0u;
    in->event_write = 0u;
    in->event_count = 0u;
}

void BH_Input_BeginFrame(BH_InputState *in, uint64_t now_ns)
{
    if (!in)
        return;

    in->now_ns = now_ns;

    memset(in->pressed, 0, sizeof(in->pressed));
    memset(in->released, 0, sizeof(in->released));
    in->mouse_delta_px = (vec2){0.0f, 0.0f};
}

void BH_Input_UpdateViewAnglesFrame(BH_InputState *in)
{
    if (!in)
        return;

    in->view_angles_frame_deg =
        bh_input_apply_mouselook(in->view_angles_frame_deg, in->mouse_delta_px, in->mouse_sensitivity);
}

void BH_Input_SetViewAngles(BH_InputState *in, vec3 angles_deg)
{
    if (!in)
        return;

    in->view_angles_frame_deg = angles_deg;
    in->view_angles_fixed_deg = angles_deg;
}

void BH_Input_SetKey(BH_InputState *in, BH_Key key, bool down, uint64_t timestamp_ns)
{
    if (!in || (int)key < 0 || key >= BH_KEY_COUNT)
        return;

    if (timestamp_ns == 0u)
        timestamp_ns = in->now_ns;

    const bool was_down = in->down[key];
    in->down[key] = down;

    /* Ignore repeats/no-op transitions (prevents redundant queue spam). */
    if (down == was_down)
    {
        return;
    }

    if (down)
    {
        in->pressed[key] = true;
    }
    else
    {
        in->released[key] = true;
    }

    const BH_InputEvent ev = {
        .timestamp_ns = timestamp_ns,
        .type = BH_INPUT_EVENT_KEY,
        .u.key = {
            .key = (uint8_t)key,
            .down = (uint8_t)(down ? 1u : 0u),
        },
    };
    bh_input_queue_push(in, &ev);
}

void BH_Input_AddMouseMotion(BH_InputState *in, vec2 pos_px, vec2 delta_px, uint64_t timestamp_ns)
{
    if (!in)
        return;

    if (timestamp_ns == 0u)
        timestamp_ns = in->now_ns;

    in->mouse_pos_px = pos_px;

    /* Frame-accurate camera: accumulate per render frame. */
    in->mouse_delta_px.x += delta_px.x;
    in->mouse_delta_px.y += delta_px.y;

    const BH_InputEvent ev = {
        .timestamp_ns = timestamp_ns,
        .type = BH_INPUT_EVENT_MOUSE_MOTION,
        .u.motion = {
            .x = pos_px.x,
            .y = pos_px.y,
            .dx = delta_px.x,
            .dy = delta_px.y,
        },
    };
    bh_input_queue_push(in, &ev);
}

BH_Intent BH_Input_BuildIntentFixed(BH_InputState *in)
{
    if (!in)
        return (BH_Intent){0};

    const uint64_t dt_ns = (in->fixed_dt_ns != 0u) ? in->fixed_dt_ns : 1u;
    const uint64_t t0 = in->fixed_time_ns;
    const uint64_t t1 = t0 + dt_ns;

    /* Per-key integration state. */
    bool state[BH_KEY_COUNT];
    uint64_t seg_start[BH_KEY_COUNT];
    uint64_t down_time_ns[BH_KEY_COUNT];
    bool pressed_in_tick[BH_KEY_COUNT];

    for (int k = 0; k < BH_KEY_COUNT; ++k)
    {
        state[k] = in->fixed_down[k];
        seg_start[k] = t0;
        down_time_ns[k] = 0u;
        pressed_in_tick[k] = false;
    }

    vec2 tick_mouse_delta = (vec2){0.0f, 0.0f};
    vec2 tick_mouse_pos = in->mouse_pos_fixed_px;

    /* Consume all queued events up to the end of this fixed tick. */
    BH_InputEvent ev;
    while (bh_input_queue_peek(in, &ev))
    {
        if (ev.timestamp_ns > t1)
            break;

        /* Clamp to the tick span (defensive; should rarely be needed). */
        uint64_t te = ev.timestamp_ns;
        if (te < t0)
            te = t0;
        if (te > t1)
            te = t1;

        if (ev.type == BH_INPUT_EVENT_KEY)
        {
            const int key = (int)ev.u.key.key;
            const bool new_down = (ev.u.key.down != 0u);
            if (key >= 0 && key < BH_KEY_COUNT)
            {
                if (state[key])
                {
                    down_time_ns[key] += (te - seg_start[key]);
                }
                seg_start[key] = te;

                if (new_down && !state[key])
                {
                    pressed_in_tick[key] = true;
                }
                state[key] = new_down;
            }
        }
        else if (ev.type == BH_INPUT_EVENT_MOUSE_MOTION)
        {
            tick_mouse_delta.x += ev.u.motion.dx;
            tick_mouse_delta.y += ev.u.motion.dy;
            tick_mouse_pos = (vec2){ev.u.motion.x, ev.u.motion.y};
        }

        bh_input_queue_pop(in);
    }

    /* Finalize held-time accumulation and commit fixed boundary state. */
    for (int k = 0; k < BH_KEY_COUNT; ++k)
    {
        if (state[k])
        {
            down_time_ns[k] += (t1 - seg_start[k]);
        }
        in->fixed_down[k] = state[k];
    }

    /* Fixed-tick view angles: integrate mouse motion that occurred during this tick. */
    in->view_angles_fixed_deg =
        bh_input_apply_mouselook(in->view_angles_fixed_deg, tick_mouse_delta, in->mouse_sensitivity);

    in->fixed_time_ns = t1;
    in->mouse_pos_fixed_px = tick_mouse_pos;

    const double inv_dt = 1.0 / (double)dt_ns;
    const float frac_w = (float)((double)down_time_ns[BH_KEY_W] * inv_dt);
    const float frac_s = (float)((double)down_time_ns[BH_KEY_S] * inv_dt);
    const float frac_a = (float)((double)down_time_ns[BH_KEY_A] * inv_dt);
    const float frac_d = (float)((double)down_time_ns[BH_KEY_D] * inv_dt);
    const float frac_q = (float)((double)down_time_ns[BH_KEY_Q] * inv_dt);
    const float frac_e = (float)((double)down_time_ns[BH_KEY_E] * inv_dt);

    const float forward = frac_w - frac_s;
    const float left = frac_a - frac_d;
    const float up = frac_e - frac_q;

    /*
        Buttons: treat a button as "down" for the tick if it was down at any
        point in the tick OR had a press edge in the tick. This prevents missing
        very fast taps at high render rates.
    */
    const bool jump = (down_time_ns[BH_KEY_SPACE] > 0u) || pressed_in_tick[BH_KEY_SPACE];
    const bool duck = (down_time_ns[BH_KEY_LCTRL] > 0u) || pressed_in_tick[BH_KEY_LCTRL];
    const bool quit = pressed_in_tick[BH_KEY_ESCAPE];

    return (BH_Intent){
        .quit = quit,
        .jump = jump,
        .duck = duck,
        .move_forward = forward,
        .move_left = left,
        .move_up = up,
        .mouse_pos_px = tick_mouse_pos,
        .mouse_delta_px = tick_mouse_delta,
        .view_angles_deg = in->view_angles_fixed_deg,
    };
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

    return (BH_Intent){
        .quit = in->pressed[BH_KEY_ESCAPE],
        .jump = in->down[BH_KEY_SPACE],
        .duck = in->down[BH_KEY_LCTRL],
        .move_forward = forward,
        .move_left = left,
        .move_up = up,
        .mouse_pos_px = in->mouse_pos_px,
        .mouse_delta_px = in->mouse_delta_px,
        .view_angles_deg = in->view_angles_frame_deg,
    };
}

void BH_Input_ClearFixedAccumulators(BH_InputState *in)
{
    if (!in)
        return;

    in->event_read = 0u;
    in->event_write = 0u;
    in->event_count = 0u;

    memcpy(in->fixed_down, in->down, sizeof(in->fixed_down));
    in->view_angles_fixed_deg = in->view_angles_frame_deg;
    in->mouse_pos_fixed_px = in->mouse_pos_px;

    in->fixed_time_ns = in->now_ns;
}
