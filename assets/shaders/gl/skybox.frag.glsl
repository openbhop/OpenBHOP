#version 300 es
// Use high precision for sky gradients to avoid color banding
precision highp float;

in vec3 v_DirWS;
out vec4 o_Color;

float saturate(float x) { return clamp(x, 0.0, 1.0); }
vec3  lerp(vec3 a, vec3 b, float t) { return a + (b - a) * t; }

vec3 sky_gradient(vec3 dir)
{
    // Z is up in World Space.
    float t = saturate(dir.z * 0.5 + 0.5);
    t = pow(t, 0.65);

    vec3 horizon = vec3(0.55, 0.70, 0.90);
    vec3 zenith  = vec3(0.07, 0.14, 0.30);
    return lerp(horizon, zenith, t);
}

void main()
{
    vec3 dir = normalize(v_DirWS);
    vec3 col = sky_gradient(dir);

    // Simple sun highlight.
    vec3 sun_dir = normalize(vec3(0.25, -0.15, 0.95));
    float sun = pow(saturate(dot(dir, sun_dir)), 512.0);
    col += sun * vec3(1.2, 1.1, 0.95);

    o_Color = vec4(col, 1.0);
}