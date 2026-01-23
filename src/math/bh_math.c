/* -----------------------------------------------------------------------------
   bh_math.c
   ----------------------------------------------------------------------------- */

#include "bh_math.h"
#include <math.h>

/* -----------------------------------------------------------------------------
   Vector Implementation
   ----------------------------------------------------------------------------- */

vec3 vec3_add(vec3 a, vec3 b)
{
    return (vec3){.x = a.x + b.x, .y = a.y + b.y, .z = a.z + b.z};
}

vec3 vec3_sub(vec3 a, vec3 b)
{
    return (vec3){.x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z};
}

vec3 vec3_scale(vec3 v, float s)
{
    return (vec3){.x = v.x * s, .y = v.y * s, .z = v.z * s};
}

float vec3_dot(vec3 a, vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

vec3 vec3_cross(vec3 a, vec3 b)
{
    return (vec3){.x = a.y * b.z - a.z * b.y, .y = a.z * b.x - a.x * b.z, .z = a.x * b.y - a.y * b.x};
}

float vec3_len(vec3 v)
{
    return sqrtf(vec3_dot(v, v));
}

float vec3_lensq(vec3 v)
{
    return vec3_dot(v, v);
}

vec3 vec3_norm(vec3 v)
{
    const float len = vec3_len(v);
    if (len <= 0.0f)
    {
        return (vec3){0};
    }
    const float inv = 1.0f / len;
    return vec3_scale(v, inv);
}

vec3 vec3_angleto(vec3 eye, vec3 target)
{
    vec3 d = vec3_sub(target, eye);
    float hyp = sqrtf(d.x * d.x + d.y * d.y);

    /* Yaw: atan2(y, x) because 0 degrees is +X */
    float yaw_rad = atan2f(d.y, d.x);

    /* Pitch: AngleVectors defines pitch+ as looking down (forward.z = -sp).
       Standard atan2(z, hyp) gives positive for UP.
       Negate Z to make looking DOWN positive. */
    float pitch_rad = atan2f(-d.z, hyp);

    const float rad2deg = 180.0f / BH_PI;
    return (vec3){.x = pitch_rad * rad2deg, .y = yaw_rad * rad2deg, .z = 0.0f};
}

float vec3_norm_inplace(vec3 *v)
{
    if (!v)
        return 0.0f;

    const float len = vec3_len(*v);
    if (len <= 0.0f)
    {
        *v = (vec3){0};
        return 0.0f;
    }

    const float inv = 1.0f / len;
    v->x *= inv;
    v->y *= inv;
    v->z *= inv;

    return len;
}

/* -----------------------------------------------------------------------------
   Quaternion Implementation
   ----------------------------------------------------------------------------- */

quat quat_identity(void)
{
    return (quat){.w = 1.0f};
}

quat quat_fromaxisangle(vec3 axis_unit, float radians)
{
    const float half = 0.5f * radians;
    const float s = sinf(half);
    const float c = cosf(half);
    return (quat){.x = axis_unit.x * s, .y = axis_unit.y * s, .z = axis_unit.z * s, .w = c};
}

quat quat_norm(quat q)
{
    const float len = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len <= 0.0f)
    {
        return quat_identity();
    }
    const float inv = 1.0f / len;
    return (quat){.x = q.x * inv, .y = q.y * inv, .z = q.z * inv, .w = q.w * inv};
}

quat quat_mul(quat a, quat b)
{
    /* Hamilton product */
    return (quat){.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                  .y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                  .z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                  .w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

quat quat_nlerp(quat a, quat b, float t)
{
    const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;

    /* Shortest path: flip b if dot < 0 */
    if (dot < 0.0f)
    {
        b.x = -b.x;
        b.y = -b.y;
        b.z = -b.z;
        b.w = -b.w;
    }

    /* Normalize after lerp */
    const quat q = {
        .x = a.x + (b.x - a.x) * t, .y = a.y + (b.y - a.y) * t, .z = a.z + (b.z - a.z) * t, .w = a.w + (b.w - a.w) * t};
    return quat_norm(q);
}

/* -----------------------------------------------------------------------------
   Matrix Implementation
   ----------------------------------------------------------------------------- */

mat4 mat4_identity(void)
{
    mat4 m = {0};
    m.m[0] = 1.0f;
    m.m[5] = 1.0f;
    m.m[10] = 1.0f;
    m.m[15] = 1.0f;
    return m;
}

mat4 mat4_mul(mat4 a, mat4 b)
{
    /* Column-major: out = a * b */
    mat4 out = {0};
    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
            {
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
            out.m[col * 4 + row] = sum;
        }
    }
    return out;
}

mat4 mat4_translate(vec3 t)
{
    mat4 m = mat4_identity();
    m.m[12] = t.x;
    m.m[13] = t.y;
    m.m[14] = t.z;
    return m;
}

mat4 mat4_scale(vec3 s)
{
    mat4 m = {0};
    m.m[0] = s.x;
    m.m[5] = s.y;
    m.m[10] = s.z;
    m.m[15] = 1.0f;
    return m;
}

mat4 mat4_from_quat(quat q)
{
    q = quat_norm(q);

    const float xx = q.x * q.x;
    const float yy = q.y * q.y;
    const float zz = q.z * q.z;
    const float xy = q.x * q.y;
    const float xz = q.x * q.z;
    const float yz = q.y * q.z;
    const float wx = q.w * q.x;
    const float wy = q.w * q.y;
    const float wz = q.w * q.z;

    mat4 m = mat4_identity();
    m.m[0] = 1.0f - 2.0f * (yy + zz);
    m.m[1] = 2.0f * (xy + wz);
    m.m[2] = 2.0f * (xz - wy);

    m.m[4] = 2.0f * (xy - wz);
    m.m[5] = 1.0f - 2.0f * (xx + zz);
    m.m[6] = 2.0f * (yz + wx);

    m.m[8] = 2.0f * (xz + wy);
    m.m[9] = 2.0f * (yz - wx);
    m.m[10] = 1.0f - 2.0f * (xx + yy);

    return m;
}

mat4 mat4_trs(vec3 t, quat r, vec3 s)
{
    const mat4 T = mat4_translate(t);
    const mat4 R = mat4_from_quat(r);
    const mat4 S = mat4_scale(s);
    return mat4_mul(T, mat4_mul(R, S));
}

mat4 mat4_perspective_lh_z0to1(float fovy_radians, float aspect, float zn, float zf)
{
    /* D3D-style: left-handed, z in [0,1] */
    const float f = 1.0f / tanf(fovy_radians * 0.5f);

    mat4 m = {0};
    m.m[0] = f / aspect;
    m.m[5] = f;
    m.m[10] = zf / (zf - zn);
    m.m[11] = 1.0f;
    m.m[14] = (-zn * zf) / (zf - zn);
    return m;
}

mat4 mat4_transpose(mat4 m)
{
    mat4 out = m;
    for (int r = 0; r < 4; ++r)
    {
        for (int c = r + 1; c < 4; ++c)
        {
            const float a = out.m[c * 4 + r];
            out.m[c * 4 + r] = out.m[r * 4 + c];
            out.m[r * 4 + c] = a;
        }
    }
    return out;
}

/* -----------------------------------------------------------------------------
   View & Camera
   ----------------------------------------------------------------------------- */

void AngleVectors(vec3 angles_deg, vec3 *forward, vec3 *right, vec3 *up)
{
    /* Quake/Source conventions:
       - angles are degrees { pitch, yaw, roll }
       - +X forward, +Y left, +Z up
       - yaw+ turns left (toward +Y)
       - pitch+ looks down
    */
    const float deg2rad = BH_PI / 180.0f;

    const float pitch = angles_deg.x * deg2rad;
    const float yaw = angles_deg.y * deg2rad;
    const float roll = angles_deg.z * deg2rad;

    const float sp = sinf(pitch);
    const float cp = cosf(pitch);
    const float sy = sinf(yaw);
    const float cy = cosf(yaw);
    const float sr = sinf(roll);
    const float cr = cosf(roll);

    if (forward)
    {
        forward->x = cp * cy;
        forward->y = cp * sy;
        forward->z = -sp;
    }

    if (right)
    {
        right->x = (-sr * sp * cy) + (-cr * -sy);
        right->y = (-sr * sp * sy) + (-cr * cy);
        right->z = (-sr * cp);
    }

    if (up)
    {
        up->x = (cr * sp * cy) + (-sr * -sy);
        up->y = (cr * sp * sy) + (-sr * cy);
        up->z = (cr * cp);
    }
}

mat4 mat4_view_from_axes_lh(vec3 eye, vec3 forward, vec3 right, vec3 up)
{
    /* View Matrix = Inverse(CameraWorld).
       For rotation: Inverse == Transpose (Basis vectors go into ROWS). */
    mat4 m = mat4_identity();

    /* Row 0: Right */
    m.m[0] = right.x;
    m.m[4] = right.y;
    m.m[8] = right.z;

    /* Row 1: Up */
    m.m[1] = up.x;
    m.m[5] = up.y;
    m.m[9] = up.z;

    /* Row 2: Forward */
    m.m[2] = forward.x;
    m.m[6] = forward.y;
    m.m[10] = forward.z;

    /* Translation: projection of Eye onto Basis */
    m.m[12] = -vec3_dot(right, eye);
    m.m[13] = -vec3_dot(up, eye);
    m.m[14] = -vec3_dot(forward, eye);

    return m;
}

mat4 mat4_lookat_lh(vec3 eye, vec3 target, vec3 up)
{
    const vec3 fwd = vec3_norm(vec3_sub(target, eye));
    const vec3 right = vec3_norm(vec3_cross(up, fwd));
    const vec3 up2 = vec3_cross(fwd, right);

    mat4 m = mat4_identity();

    /* Row 0: Right */
    m.m[0] = right.x;
    m.m[4] = right.y;
    m.m[8] = right.z;

    /* Row 1: Up */
    m.m[1] = up2.x;
    m.m[5] = up2.y;
    m.m[9] = up2.z;

    /* Row 2: Forward */
    m.m[2] = fwd.x;
    m.m[6] = fwd.y;
    m.m[10] = fwd.z;

    m.m[12] = -vec3_dot(right, eye);
    m.m[13] = -vec3_dot(up2, eye);
    m.m[14] = -vec3_dot(fwd, eye);

    return m;
}

mat4 mat4_view_from_angles_lh(vec3 eye, vec3 angles_deg)
{
    vec3 f, r, u;
    AngleVectors(angles_deg, &f, &r, &u);

    /* Defensive normalization to match movement code conventions */
    (void)vec3_norm_inplace(&f);
    (void)vec3_norm_inplace(&r);
    (void)vec3_norm_inplace(&u);

    return mat4_view_from_axes_lh(eye, f, r, u);
}