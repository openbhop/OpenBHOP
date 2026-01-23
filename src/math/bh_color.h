#pragma once

#include "bh_math.h"
/* Ensure uint8_t is available if not in bh_math.h */
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*
      bh_color

      Simple linear RGBA float color type.

      Notes:
      - Components are expected to be in [0..1].
      - Memory layout matches a float4, so it can be packed into vertices and
        uniform buffers without conversion.
    */

    typedef struct bh_color
    {
        float r;
        float g;
        float b;
        float a;
    } color4f;

    /* Literal helpers
       We switch syntax based on the language.
       C++11 uses constructor syntax: bh_color{...}
       C99 uses compound literals:    (bh_color){...}
    */
#ifdef __cplusplus
#define BH_COLOR_RGBA(r_, g_, b_, a_)                                                                                  \
    bh_color                                                                                                           \
    {                                                                                                                  \
        (r_), (g_), (b_), (a_)                                                                                         \
    }
#else
#define BH_COLOR_RGBA(r_, g_, b_, a_) ((color4f){(r_), (g_), (b_), (a_)})
#endif

#define BH_COLOR_RGB(r_, g_, b_) BH_COLOR_RGBA((r_), (g_), (b_), 1.0f)

#define BH_COLOR_WHITE BH_COLOR_RGB(1.0f, 1.0f, 1.0f)
#define BH_COLOR_BLACK BH_COLOR_RGB(0.0f, 0.0f, 0.0f)
#define BH_COLOR_RED BH_COLOR_RGB(1.0f, 0.0f, 0.0f)
#define BH_COLOR_GREEN BH_COLOR_RGB(0.0f, 1.0f, 0.0f)
#define BH_COLOR_BLUE BH_COLOR_RGB(0.0f, 0.0f, 1.0f)
#define BH_COLOR_YELLOW BH_COLOR_RGB(1.0f, 1.0f, 0.0f)
#define BH_COLOR_MAGENTA BH_COLOR_RGB(1.0f, 0.0f, 1.0f)
#define BH_COLOR_CYAN BH_COLOR_RGB(0.0f, 1.0f, 1.0f)

    /* For the functions, we use explicit variable initialization.
       This is valid in both C99 and C++, avoiding the syntax conflict entirely.
    */

    static BH_FORCEINLINE color4f bh_color_rgba(float r, float g, float b, float a)
    {
        color4f result = {r, g, b, a};
        return result;
    }

    static BH_FORCEINLINE color4f bh_color_rgb(float r, float g, float b)
    {
        color4f result = {r, g, b, 1.0f};
        return result;
    }

    static BH_FORCEINLINE color4f bh_color_rgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        const float inv = 1.0f / 255.0f;
        color4f result = {(float)r * inv, (float)g * inv, (float)b * inv, (float)a * inv};
        return result;
    }

    static BH_FORCEINLINE color4f bh_color_rgb8(uint8_t r, uint8_t g, uint8_t b)
    {
        return bh_color_rgba8(r, g, b, 255);
    }

    /* Common defaults */
    static BH_FORCEINLINE color4f bh_color_white(void)
    {
        return bh_color_rgb(1.0f, 1.0f, 1.0f);
    }
    static BH_FORCEINLINE color4f bh_color_black(void)
    {
        return bh_color_rgb(0.0f, 0.0f, 0.0f);
    }
    static BH_FORCEINLINE color4f bh_color_red(void)
    {
        return bh_color_rgb(1.0f, 0.0f, 0.0f);
    }
    static BH_FORCEINLINE color4f bh_color_green(void)
    {
        return bh_color_rgb(0.0f, 1.0f, 0.0f);
    }
    static BH_FORCEINLINE color4f bh_color_blue(void)
    {
        return bh_color_rgb(0.0f, 0.0f, 1.0f);
    }
    static BH_FORCEINLINE color4f bh_color_yellow(void)
    {
        return bh_color_rgb(1.0f, 1.0f, 0.0f);
    }
    static BH_FORCEINLINE color4f bh_color_magenta(void)
    {
        return bh_color_rgb(1.0f, 0.0f, 1.0f);
    }
    static BH_FORCEINLINE color4f bh_color_cyan(void)
    {
        return bh_color_rgb(0.0f, 1.0f, 1.0f);
    }
    static BH_FORCEINLINE color4f bh_color_gray(float v)
    {
        return bh_color_rgb(v, v, v);
    }

    static BH_FORCEINLINE vec4 bh_color_to_vec4(color4f c)
    {
        vec4 result = {c.r, c.g, c.b, c.a};
        return result;
    }

    static BH_FORCEINLINE color4f bh_color_from_vec4(vec4 v)
    {
        color4f result = {v.x, v.y, v.z, v.w};
        return result;
    }

#ifdef __cplusplus
} /* extern "C" */
#endif