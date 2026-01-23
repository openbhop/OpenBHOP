#include "common.hlsl"

cbuffer SkyboxVS : register(b0, WR_VS_UNIFORM_SPACE)
{
    float4x4 u_MVP;
};

struct SkyVSOut
{
    float4 position : SV_Position;
    float3 dir_ws   : TEXCOORD0;
};

SkyVSOut vs_main(VSInput input)
{
    SkyVSOut o;
    o.position = mul(u_MVP, float4(input.position, 1.0));
    o.dir_ws = input.position;
    return o;
}
