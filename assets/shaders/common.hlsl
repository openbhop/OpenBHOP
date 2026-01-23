#ifndef WR_COMMON_HLSL
#define WR_COMMON_HLSL

/* SPIR-V descriptor sets for uniform buffers (SDL GPU convention) */
#define WR_VS_UNIFORM_SET   1
#define WR_FS_UNIFORM_SET   3

/* SPIR-V descriptor sets for sampled textures (SDL GPU convention) */
#define WR_VS_SAMPLED_SET   0
#define WR_FS_SAMPLED_SET   2

/* D3D register spaces for uniform buffers (SDL GPU convention for DXBC/DXIL) */
#define WR_VS_UNIFORM_SPACE space1
#define WR_FS_UNIFORM_SPACE space3

/* D3D register spaces for sampled textures + samplers (SDL GPU convention for DXBC/DXIL) */
#define WR_VS_SAMPLED_SPACE space0
#define WR_FS_SAMPLED_SPACE space2

struct VSInput
{
    float3 position : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
    float2 uv2      : TEXCOORD3;
};

struct VSOutput
{
    float4 position  : SV_Position;
    float3 normal_ws : TEXCOORD0;
    float2 uv        : TEXCOORD1;
    float3 position_ws : TEXCOORD2;
    float2 uv2       : TEXCOORD3;
};

#endif /* WR_COMMON_HLSL */
