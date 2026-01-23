#include "common.hlsl"

cbuffer Material : register(b0, WR_FS_UNIFORM_SPACE)
{
    float3 u_Tint;
    float u_NormalScale;

    float u_MetallicFactor;
    float u_RoughnessFactor;
    float2 _MatPad0;

    float u_LightmapStrength;
    float u_LightmapDirStrength;
    float u_ShadowmaskStrength;
    float _MatPad1;
};

cbuffer Scene : register(b1, WR_FS_UNIFORM_SPACE)
{

float3 u_CameraPos;
float u_AmbientStrength;

float3 u_LightDirection;
float u_LightIntensity;

float3 u_LightColor;
float _ScenePad0;
};

/* PBR textures (t0..t4) */
Texture2D<float4> t_Albedo : register(t0, WR_FS_SAMPLED_SPACE);
SamplerState s_Albedo : register(s0, WR_FS_SAMPLED_SPACE);

Texture2D<float4> t_Normal : register(t1, WR_FS_SAMPLED_SPACE);
SamplerState s_Normal : register(s1, WR_FS_SAMPLED_SPACE);

Texture2D<float4> t_Roughness : register(t2, WR_FS_SAMPLED_SPACE);
SamplerState s_Roughness : register(s2, WR_FS_SAMPLED_SPACE);

Texture2D<float4> t_Metallic : register(t3, WR_FS_SAMPLED_SPACE);
SamplerState s_Metallic : register(s3, WR_FS_SAMPLED_SPACE);

Texture2D<float4> t_AO : register(t4, WR_FS_SAMPLED_SPACE);
SamplerState s_AO : register(s4, WR_FS_SAMPLED_SPACE);

/* Baked lighting textures (t5..t7) */
Texture2D<float4> t_Lightmap : register(t5, WR_FS_SAMPLED_SPACE);
SamplerState s_Lightmap : register(s5, WR_FS_SAMPLED_SPACE);

Texture2D<float4> t_LightDir : register(t6, WR_FS_SAMPLED_SPACE);
SamplerState s_LightDir : register(s6, WR_FS_SAMPLED_SPACE);

Texture2D<float4> t_ShadowMask : register(t7, WR_FS_SAMPLED_SPACE);
SamplerState s_ShadowMask : register(s7, WR_FS_SAMPLED_SPACE);

static const float PI = 3.14159265359;
static const float RGBM_MAX_RANGE = 16.0;

// -------------------------------------------------------------------------
// Helper Functions
// -------------------------------------------------------------------------

static float3 normal_from_map(float3 normal_ws, float3 pos_ws, float2 uv)
{
    float3 N = normalize(normal_ws);

    float3 dp1 = ddx(pos_ws);
    float3 dp2 = ddy(pos_ws);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);

    float3 T = dp1 * duv2.y - dp2 * duv1.y;
    // Note: flipped sign logic for B to match standard
    float3 B = dp2 * duv1.x - dp1 * duv2.x;
    
    // Gram-Schmidt
    T = normalize(T - N * dot(N, T));
    B = cross(N, T);

    float3x3 TBN = float3x3(T, B, N);

    float3 n_ts = t_Normal.Sample(s_Normal, uv).xyz * 2.0 - 1.0;
    n_ts.xy *= u_NormalScale;
    n_ts = normalize(n_ts);
    
    return normalize(mul(n_ts, TBN));
}

static float D_GGX(float NdotH, float roughness)
{
    float a = max(roughness * roughness, 0.001);
    float a2 = a * a;
    float denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 1e-6);
}

static float G_SchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

static float G_Smith(float NdotV, float NdotL, float roughness)
{
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

static float3 F_Schlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

static float3 DecodeRGBM(float4 rgbm)
{
    // cmap baker: float alpha = (mult - 1.0f) / (16.0f - 1.0f);
    // to decode: mult = (alpha * 15.0) + 1.0;
    
    float multiplier = (rgbm.a * 15.0) + 1.0;
    
    rgbm.rgb = pow(rgbm.rgb, 2.2);
    return rgbm.rgb * ((rgbm.a * 15.0) + 1.0);
    // note: If rgbm.rgb has already been linearized use this:
    //return rgbm.rgb * multiplier;
}

// -------------------------------------------------------------------------
// PBR Logic
// -------------------------------------------------------------------------

static float3 CalculatePBRLighting(
    float3 N, float3 V, float3 albedo, float metallic, float roughness,
    float3 lightDir, float3 lightColor, float lightIntensity, float shadow)
{
    float3 L = normalize(lightDir);
    float3 H = normalize(V + L);
    
    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    
    // Specular BRDF
    float3 F = F_Schlick(VdotH, F0);
    float D = D_GGX(NdotH, roughness);
    float G = G_Smith(NdotV, NdotL, roughness);

    float3 numerator = D * G * F;
    float denom = max(4.0 * NdotV * NdotL, 1e-4);
    float3 specular = numerator / denom;

    // Diffuse BRDF (Lambertian with energy conservation)
    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic);
    float3 diffuse = albedo; // Note: Usually albedo/PI, but moved to radiance calc often

    float3 radiance = lightColor * lightIntensity;
    
    // Note: The PI division for diffuse usually happens here or in the final composition.
    // In the original code, it looked like: (kD * albedo / PI + specular) ...
    // But later simply used (diffuse + specular). We will stick to the previous implementation logic.
    return (kD * diffuse / PI + specular) * radiance * (NdotL * shadow);
}

// -------------------------------------------------------------------------
// Main
// -------------------------------------------------------------------------

float4 ps_main(VSOutput input) : SV_Target
{
    float2 uv = input.uv;
    float2 uv2 = float2(input.uv2.x, 1.0 - input.uv2.y);

    // 1. Base Material Data
    float4 albedo_sample = t_Albedo.Sample(s_Albedo, uv);
    float3 albedo = albedo_sample.rgb * u_Tint;
    float alpha = albedo_sample.a;
    float ao = saturate(t_AO.Sample(s_AO, uv).r);

    // 2. Normal Mapping 
    // Even for just lightmaps, we calculate this to allow Directional Lightmaps to work accurately.
    float3 N = normal_from_map(input.normal_ws, input.position_ws, uv);
    
    // 3. Ambient / Lightmap Accumulator
    float3 finalColor = float3(0, 0, 0);

    if (u_LightmapStrength > 0.0)
    {
        // Sample Irradiance
        float4 lm_raw = t_Lightmap.Sample(s_Lightmap, uv2);
        float3 lm = DecodeRGBM(lm_raw);
        //float3 lm = t_Lightmap.Sample(s_Lightmap, uv2).rgb;

        // Directional Lightmap Modulation (Accurate)
        if (u_LightmapDirStrength > 0.0)
        {
            // Decode direction from [0,1] to [-1,1]
            float3 dir = normalize(t_LightDir.Sample(s_LightDir, uv2).rgb * 2.0 - 1.0);
            
            // Calculate alignment between surface normal and dominant light direction
            float ndl_lm = saturate(dot(N, dir));
            
            // Blend based on strength
            float dir_factor = lerp(1.0, ndl_lm, saturate(u_LightmapDirStrength));
            
            // Modulate irradiance
            lm *= dir_factor;
        }

        // Apply Lambertian Diffuse: (Albedo / PI) * Irradiance * AO
        float3 baked = (albedo / PI) * lm * ao;
        finalColor += baked * u_LightmapStrength;
    }
    else
    {
        // Fallback ambient if no lightmap
        finalColor += u_AmbientStrength * albedo * ao;
    }

    // ---------------------------------------------------------------------
    // PBR Dynamic Lighting
    // ---------------------------------------------------------------------
    float metallic = saturate(t_Metallic.Sample(s_Metallic, uv).r * u_MetallicFactor);
    float roughness = saturate(t_Roughness.Sample(s_Roughness, uv).r * u_RoughnessFactor);
    
    // Anti-aliasing for roughness
    float3 dN = fwidth(N);
    float geometricRoughness = length(dN);
    roughness = max(roughness, geometricRoughness);
    roughness = max(roughness, 0.04);

    float3 V = normalize(u_CameraPos - input.position_ws);
    
    // Shadow Mask logic
    float shadow = 1.0;
    if (u_ShadowmaskStrength > 0.0) {
        float sm = saturate(t_ShadowMask.Sample(s_ShadowMask, uv2).r);
        shadow = lerp(1.0, sm, saturate(u_ShadowmaskStrength));
    }

    float3 pbrDirect = CalculatePBRLighting(
        N, V, albedo, metallic, roughness, 
        u_LightDirection, u_LightColor, u_LightIntensity, shadow
    );
    
    finalColor += pbrDirect;
    // ---------------------------------------------------------------------

    return float4(pow(finalColor, 1.0 / 2.2), alpha);
}