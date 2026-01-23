#include "common.hlsl"

struct DBG_VSInput
{
    float3 position : TEXCOORD0;
    float4 color    : TEXCOORD1;
};

struct DBG_VSOutput
{
    float4 position : SV_Position;
    float4 color    : TEXCOORD0;
};

cbuffer PerDraw : register(b0, WR_VS_UNIFORM_SPACE)
{
    float4x4 u_MVP;
};

DBG_VSOutput vs_main(DBG_VSInput input)
{
    DBG_VSOutput o;
    o.position = mul(u_MVP, float4(input.position, 1.0));
    o.color = input.color;
    return o;
}
