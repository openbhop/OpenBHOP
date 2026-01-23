#include "common.hlsl"

cbuffer PerDraw : register(b0, WR_VS_UNIFORM_SPACE)
{
    float4x4 u_MVP;
    float4x4 u_Model;
};

VSOutput vs_main(VSInput input)
{
    VSOutput o;

    // Column-major matrices with column-vector convention (GLSL-like):
    //   clip = MVP * position
    o.position = mul(u_MVP, float4(input.position, 1.0));

    /* World position for PBR shading + normal mapping. */
    float4 wp = mul(u_Model, float4(input.position, 1.0));
    o.position_ws = wp.xyz;

    /* Transform normal (ignore non-uniform scale for now) */
    float3x3 m3 = (float3x3)u_Model;
    o.normal_ws = normalize(mul(m3, input.normal));
    o.uv = input.uv;
    o.uv2 = input.uv2;
    return o;
}
