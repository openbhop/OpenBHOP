/* -----------------------------------------------------------------------------
   bh_parse.h
   ----------------------------------------------------------------------------- */

#pragma once

#include "../math/bh_color.h"
#include "../math/bh_math.h"
#include "bh_core.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* -----------------------------------------------------------------------------
       Integers
       ----------------------------------------------------------------------------- */

    int32_t BH_Parse_Int32(const char *s, int32_t fallback);
    uint32_t BH_Parse_UInt32(const char *s, uint32_t fallback);

    int32_t BH_Parse_Int32Clamp(const char *s, int32_t fallback, int32_t min_v, int32_t max_v);
    uint32_t BH_Parse_UInt32Clamp(const char *s, uint32_t fallback, uint32_t min_v, uint32_t max_v);

    /* -----------------------------------------------------------------------------
       Floating Point
       ----------------------------------------------------------------------------- */

    float BH_Parse_Float32(const char *s, float fallback);
    double BH_Parse_Float64(const char *s, double fallback);

    float BH_Parse_Float32Clamp(const char *s, float fallback, float min_v, float max_v);

    /* -----------------------------------------------------------------------------
       Boolean
       ----------------------------------------------------------------------------- */

    bool BH_Parse_Boolean(const char *s, bool fallback);

    /* -----------------------------------------------------------------------------
       Vectors
       ----------------------------------------------------------------------------- */

    vec2 BH_Parse_Vec2(const char *s, vec2 fallback);
    vec3 BH_Parse_Vec3(const char *s, vec3 fallback);
    vec4 BH_Parse_Vec4(const char *s, vec4 fallback);

    /* -----------------------------------------------------------------------------
       Color
       ----------------------------------------------------------------------------- */

    color4f BH_Parse_Color(const char *s, color4f fallback);

#ifdef __cplusplus
}
#endif
