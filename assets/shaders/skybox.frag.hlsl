#include "common.hlsl"

struct SkyVSOut
{
    float4 position : SV_Position;
    float3 dir_ws   : TEXCOORD0;
};

static float3 sky_gradient(float3 dir)
{
    // Z is up in WR.
    float t = saturate(dir.z * 0.5 + 0.5);
    t = pow(t, 0.65);

    const float3 horizon = float3(0.55, 0.70, 0.90);
    const float3 zenith  = float3(0.07, 0.14, 0.30);
    return lerp(horizon, zenith, t);
}

float4 ps_main(SkyVSOut input) : SV_Target
{
    float3 dir = normalize(input.dir_ws);

    float3 col = sky_gradient(dir);

    // Simple sun highlight.
    const float3 sun_dir = normalize(float3(0.25, -0.15, 0.95));
    float sun = pow(saturate(dot(dir, sun_dir)), 512.0);
    col += sun * float3(1.2, 1.1, 0.95);

    return float4(col, 1.0);
}
