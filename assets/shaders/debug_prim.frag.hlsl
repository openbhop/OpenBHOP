#include "common.hlsl"

struct DBG_PSInput
{
    float4 position : SV_Position;
    float4 color    : TEXCOORD0;
};

float4 ps_main(DBG_PSInput input) : SV_Target0
{
    return input.color;
}
