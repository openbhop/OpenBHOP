/* -----------------------------------------------------------------------------
   bh_parse.c
   ----------------------------------------------------------------------------- */

#include "bh_parse.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* -----------------------------------------------------------------------------
   Internal Helpers
   ----------------------------------------------------------------------------- */

static bool bh_is_sep(char c)
{
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',' || c == ';' || c == '[' || c == ']' ||
            c == '(' || c == ')' || c == '{' || c == '}');
}

static const char *bh_skip_seps(const char *s)
{
    if (!s)
    {
        return NULL;
    }
    while (*s && bh_is_sep(*s))
    {
        ++s;
    }
    return s;
}

static bool bh_parse_next_f32(const char **inout_s, float *out_v)
{
    const char *s = bh_skip_seps(*inout_s);
    if (!s || !s[0])
    {
        return false;
    }

    errno = 0;
    char *end = NULL;
    const float v = strtof(s, &end);

    if (end == s)
    {
        return false;
    }
    if (errno == ERANGE)
    {
        return false;
    }

    *out_v = v;
    *inout_s = end;
    return true;
}

static float bh_clamp_finite(float v, float min_v, float max_v)
{
    /* NaN comparisons are always false, so explicitly guard against them
       returning undefined values from clampf. */
    if (!(v == v))
    {
        return min_v;
    }
    return clampf(v, min_v, max_v);
}

static bool bh_strieq(const char *a, const char *b)
{
    while (*a && *b)
    {
        const int ca = tolower((unsigned char)*a);
        const int cb = tolower((unsigned char)*b);
        if (ca != cb)
        {
            return false;
        }
        ++a;
        ++b;
    }
    return (*a == '\0' && *b == '\0');
}

static int bh_hex_to_nibble(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F')
        return 10 + (c - 'A');
    return -1;
}

static bool bh_parse_hex_byte(const char *s, uint8_t *out)
{
    const int hi = bh_hex_to_nibble(s[0]);
    const int lo = bh_hex_to_nibble(s[1]);

    if (hi < 0 || lo < 0)
    {
        return false;
    }

    *out = (uint8_t)((hi << 4) | lo);
    return true;
}

/* -----------------------------------------------------------------------------
   Integers
   ----------------------------------------------------------------------------- */

int32_t BH_Parse_Int32(const char *s, int32_t fallback)
{
    if (!s || !s[0])
    {
        return fallback;
    }

    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);

    if (end == s)
    {
        return fallback;
    }
    if (errno == ERANGE)
    {
        return (v < 0) ? INT32_MIN : INT32_MAX;
    }

    if (v < (long)INT32_MIN)
        v = (long)INT32_MIN;
    if (v > (long)INT32_MAX)
        v = (long)INT32_MAX;

    return (int32_t)v;
}

uint32_t BH_Parse_UInt32(const char *s, uint32_t fallback)
{
    if (!s || !s[0])
    {
        return fallback;
    }

    errno = 0;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 10);

    if (end == s)
    {
        return fallback;
    }
    if (errno == ERANGE)
    {
        return UINT32_MAX;
    }

    if (v > (unsigned long)UINT32_MAX)
        v = (unsigned long)UINT32_MAX;

    return (uint32_t)v;
}

int32_t BH_Parse_Int32Clamp(const char *s, int32_t fallback, int32_t min_v, int32_t max_v)
{
    if (min_v > max_v)
    {
        const int32_t tmp = min_v;
        min_v = max_v;
        max_v = tmp;
    }

    int32_t v = BH_Parse_Int32(s, fallback);
    if (v < min_v)
        v = min_v;
    if (v > max_v)
        v = max_v;

    return v;
}

uint32_t BH_Parse_UInt32Clamp(const char *s, uint32_t fallback, uint32_t min_v, uint32_t max_v)
{
    if (min_v > max_v)
    {
        const uint32_t tmp = min_v;
        min_v = max_v;
        max_v = tmp;
    }

    uint32_t v = BH_Parse_UInt32(s, fallback);
    if (v < min_v)
        v = min_v;
    if (v > max_v)
        v = max_v;

    return v;
}

/* -----------------------------------------------------------------------------
   Floating Point
   ----------------------------------------------------------------------------- */

float BH_Parse_Float32(const char *s, float fallback)
{
    if (!s || !s[0])
    {
        return fallback;
    }

    errno = 0;
    char *end = NULL;
    const float v = strtof(s, &end);

    if (end == s)
    {
        return fallback;
    }
    if (errno == ERANGE)
    {
        return fallback;
    }
    if (!(v == v))
    {
        return fallback;
    }

    return v;
}

double BH_Parse_Float64(const char *s, double fallback)
{
    if (!s || !s[0])
    {
        return fallback;
    }

    errno = 0;
    char *end = NULL;
    const double v = strtod(s, &end);

    if (end == s)
    {
        return fallback;
    }
    if (errno == ERANGE)
    {
        return fallback;
    }
    if (!(v == v))
    {
        return fallback;
    }

    return v;
}

float BH_Parse_Float32Clamp(const char *s, float fallback, float min_v, float max_v)
{
    if (min_v > max_v)
    {
        const float tmp = min_v;
        min_v = max_v;
        max_v = tmp;
    }

    const float v = BH_Parse_Float32(s, fallback);
    return bh_clamp_finite(v, min_v, max_v);
}

/* -----------------------------------------------------------------------------
   Boolean
   ----------------------------------------------------------------------------- */

bool BH_Parse_Boolean(const char *s, bool fallback)
{
    if (!s || !s[0])
    {
        return fallback;
    }

    s = bh_skip_seps(s);
    if (!s || !s[0])
    {
        return fallback;
    }

    if (bh_strieq(s, "true") || bh_strieq(s, "yes") || bh_strieq(s, "on"))
    {
        return true;
    }
    if (bh_strieq(s, "false") || bh_strieq(s, "no") || bh_strieq(s, "off"))
    {
        return false;
    }

    const int32_t v = BH_Parse_Int32(s, fallback ? 1 : 0);
    return (v != 0);
}

/* -----------------------------------------------------------------------------
   Vectors
   ----------------------------------------------------------------------------- */

vec2 BH_Parse_Vec2(const char *s, vec2 fallback)
{
    float x = 0.0f;
    float y = 0.0f;
    const char *p = s;

    if (!bh_parse_next_f32(&p, &x))
        return fallback;
    if (!bh_parse_next_f32(&p, &y))
        return fallback;

    return (vec2){x, y};
}

vec3 BH_Parse_Vec3(const char *s, vec3 fallback)
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    const char *p = s;

    if (!bh_parse_next_f32(&p, &x))
        return fallback;
    if (!bh_parse_next_f32(&p, &y))
        return fallback;
    if (!bh_parse_next_f32(&p, &z))
        return fallback;

    return (vec3){x, y, z};
}

vec4 BH_Parse_Vec4(const char *s, vec4 fallback)
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
    const char *p = s;

    if (!bh_parse_next_f32(&p, &x))
        return fallback;
    if (!bh_parse_next_f32(&p, &y))
        return fallback;
    if (!bh_parse_next_f32(&p, &z))
        return fallback;
    if (!bh_parse_next_f32(&p, &w))
        return fallback;

    return (vec4){x, y, z, w};
}

/* -----------------------------------------------------------------------------
   Color
   ----------------------------------------------------------------------------- */

color4f BH_Parse_Color(const char *s, color4f fallback)
{
    if (!s || !s[0])
    {
        return fallback;
    }

    if (s[0] == '#')
    {
        const char *hex = s + 1;
        const size_t n = strlen(hex);

        if (n != 6 && n != 8)
        {
            return fallback;
        }

        uint8_t r = 255, g = 255, b = 255, a = 255;
        if (!bh_parse_hex_byte(hex + 0, &r))
            return fallback;
        if (!bh_parse_hex_byte(hex + 2, &g))
            return fallback;
        if (!bh_parse_hex_byte(hex + 4, &b))
            return fallback;

        if (n == 8)
        {
            if (!bh_parse_hex_byte(hex + 6, &a))
                return fallback;
        }

        return bh_color_rgba8(r, g, b, a);
    }

    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
    const char *p = s;

    if (!bh_parse_next_f32(&p, &r))
        return fallback;
    if (!bh_parse_next_f32(&p, &g))
        return fallback;
    if (!bh_parse_next_f32(&p, &b))
        return fallback;

    (void)bh_parse_next_f32(&p, &a);

    /* Clamp to [0..1] (typical linear color range) */
    r = bh_clamp_finite(r, 0.0f, 1.0f);
    g = bh_clamp_finite(g, 0.0f, 1.0f);
    b = bh_clamp_finite(b, 0.0f, 1.0f);
    a = bh_clamp_finite(a, 0.0f, 1.0f);

    return bh_color_rgba(r, g, b, a);
}