/* -----------------------------------------------------------------------------
   bh_math.h
   ----------------------------------------------------------------------------- */

#pragma once

#include "../core/bh_core.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* -----------------------------------------------------------------------------
       Constants & Types
       ----------------------------------------------------------------------------- */

#define BH_PI 3.14159265358979323846f

    typedef struct bh_vec2
    {
        float x, y;
    } vec2;
    typedef struct bh_vec3
    {
        float x, y, z;
    } vec3;
    typedef struct bh_vec4
    {
        float x, y, z, w;
    } vec4;

    /* Quaternion: (x, y, z) imaginary, w real */
    typedef struct bh_quat
    {
        float x, y, z, w;
    } quat;

    /* Column-major 4x4 matrix: m[col*4 + row] */
    typedef struct bh_mat4
    {
        float m[16];
    } mat4;

    /* -----------------------------------------------------------------------------
       Inline Helpers
       ----------------------------------------------------------------------------- */

    static inline float lerpf(float a, float b, float t)
    {
        return a + (b - a) * t;
    }

    static inline float clampf(float v, float min, float max)
    {
        if (v < min)
            return min;
        if (v > max)
            return max;
        return v;
    }

    static BH_FORCEINLINE vec2 vec2_make(float x, float y)
    {
        vec2 v = {x, y};
        return v;
    }

    static BH_FORCEINLINE vec3 vec3_make(float x, float y, float z)
    {
        vec3 v = {x, y, z};
        return v;
    }

    static BH_FORCEINLINE vec4 vec4_make(float x, float y, float z, float w)
    {
        vec4 v = {x, y, z, w};
        return v;
    }

    /* -----------------------------------------------------------------------------
       Vector Operations
       ----------------------------------------------------------------------------- */

    vec3 vec3_add(vec3 a, vec3 b);
    vec3 vec3_sub(vec3 a, vec3 b);
    vec3 vec3_scale(vec3 v, float s);
    float vec3_dot(vec3 a, vec3 b);
    vec3 vec3_cross(vec3 a, vec3 b);
    float vec3_len(vec3 v);
    float vec3_lensq(vec3 v);
    vec3 vec3_norm(vec3 v);

    /* Calculates pitch/yaw to face target from eye. Z-up, +X forward. */
    vec3 vec3_angleto(vec3 eye, vec3 target);

    /* Normalizes in-place. Returns original length or 0 if too small. */
    float vec3_norm_inplace(vec3 *v);

    /* -----------------------------------------------------------------------------
       Quaternion Operations
       ----------------------------------------------------------------------------- */

    quat quat_identity(void);
    quat quat_fromaxisangle(vec3 axis_unit, float radians);
    quat quat_norm(quat q);
    quat quat_mul(quat a, quat b);
    quat quat_nlerp(quat a, quat b, float t);

    /* -----------------------------------------------------------------------------
       Matrix Operations
       ----------------------------------------------------------------------------- */

    mat4 mat4_identity(void);
    mat4 mat4_mul(mat4 a, mat4 b);
    mat4 mat4_translate(vec3 t);
    mat4 mat4_scale(vec3 s);
    mat4 mat4_from_quat(quat q);
    mat4 mat4_trs(vec3 t, quat r, vec3 s);
    mat4 mat4_transpose(mat4 m);

    /* Left-handed, z range [0, 1] (D3D-style) */
    mat4 mat4_perspective_lh_z0to1(float fovy_radians, float aspect, float zn, float zf);
    mat4 mat4_lookat_lh(vec3 eye, vec3 target, vec3 up);

    /* -----------------------------------------------------------------------------
       View & Basis
       ----------------------------------------------------------------------------- */

    /* Quake/Source-style Euler->basis conversion.
       angles_deg: { pitch, yaw, roll } in degrees.
       Convention: X forward, Y left, Z up.
    */
    void AngleVectors(vec3 angles_deg, vec3 *forward, vec3 *right, vec3 *up);

    /* Builds view matrix from explicit camera basis.
       View space is left-handed with +Z forward.
    */
    mat4 mat4_view_from_axes_lh(vec3 eye, vec3 forward, vec3 right, vec3 up);

    /* Convenience: view matrix from Euler angles (degrees). */
    mat4 mat4_view_from_angles_lh(vec3 eye, vec3 angles_deg);

#ifdef __cplusplus
} /* extern "C" */
#endif
