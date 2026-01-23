#version 300 es
precision highp float; // <--- REQUIRED for WebGL 2

// -----------------------------------------------------------------------------
// basic.frag.glsl
// -----------------------------------------------------------------------------

in vec3 v_NormalWS;
in vec2 v_UV;
in vec3 v_PositionWS;
in vec2 v_UV2;

out vec4 o_Color;

// NOTE: The engine's CPU side packs uniforms based on SPIR-V reflection
// (std140-like offsets). Keep member order/types stable.

// Fragment uniform slot 0 (Material)
layout(std140) uniform FS_UBO0
{
    vec3  u_Tint;
    float u_NormalScale;

    float u_MetallicFactor;
    float u_RoughnessFactor;
    vec2  _MatPad0;

    float u_LightmapStrength;
    float u_LightmapDirStrength;
    float u_ShadowmaskStrength;
    float _MatPad1;
};

// Fragment uniform slot 1 (Scene)
layout(std140) uniform FS_UBO1
{
    vec3  u_CameraPos;
    float u_AmbientStrength;

    // Matches HLSL/SPIR-V reflection names used by the CPU-side ParamBlock.
    vec3  u_LightDirection; // normalized direction TO light
    float u_LightIntensity;

    vec3  u_LightColor;
    float _ScenePad0;
};

// Fragment samplers (slots 0..7)
uniform sampler2D u_Tex[8];

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

const float BH_PI = 3.14159265358979323846;

float saturate(float x) { return clamp(x, 0.0, 1.0); }
vec3  saturate(vec3 x)  { return clamp(x, vec3(0.0), vec3(1.0)); }

vec3 DecodeRGBM(vec4 rgbm)
{
    // gamma decode + RGBM scale
    vec3 c = pow(rgbm.rgb, vec3(2.2));
    return c * ((rgbm.a * 15.0) + 1.0);
}

vec3 NormalFromMap(vec2 uv, vec3 normal_ws)
{
    // Tangent-space normal from texture.
    vec3 n = texture(u_Tex[1], uv).rgb * 2.0 - 1.0;
    n.xy *= u_NormalScale;
    n = normalize(n);

    // Compute screen-space tangents.
    // NOTE: dFdx/dFdy are standard in ES 3.0 (no extension needed)
    vec3 dp1 = dFdx(v_PositionWS);
    vec3 dp2 = dFdy(v_PositionWS);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);

    vec3 N = normalize(normal_ws);

    vec3 T = normalize(dp1 * duv2.y - dp2 * duv1.y);
    vec3 B = normalize(dp2 * duv1.x - dp1 * duv2.x);

    // Orthonormalize.
    T = normalize(T - N * dot(N, T));
    B = cross(N, T);

    mat3 TBN = mat3(T, B, N);
    return normalize(TBN * n);
}

float D_GGX(float NdotH, float a)
{
    float a2 = a * a;
    float denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / (BH_PI * denom * denom);
}

float G_SchlickGGX(float NdotV, float k)
{
    return NdotV / (NdotV * (1.0 - k) + k);
}

float G_Smith(float NdotV, float NdotL, float k)
{
    return G_SchlickGGX(NdotV, k) * G_SchlickGGX(NdotL, k);
}

vec3 F_Schlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 PBR_Direct(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness, vec3 lightColor, float intensity)
{
    vec3 H = normalize(V + L);

    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    float a = max(roughness * roughness, 0.001);

    float D = D_GGX(NdotH, a);

    float k = (roughness + 1.0);
    k = (k * k) / 8.0;
    float G = G_Smith(NdotV, NdotL, k);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = F_Schlick(VdotH, F0);

    vec3 numerator = D * G * F;
    float denom = 4.0 * max(NdotV, 0.001) * max(NdotL, 0.001);
    vec3 specular = numerator / denom;

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    vec3 diffuse = kD * albedo / BH_PI;

    vec3 radiance = lightColor * intensity;

    return (diffuse + specular) * radiance * NdotL;
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------

void main()
{
    vec2 uv = v_UV;
    vec2 uv2 = vec2(v_UV2.x, 1.0 - v_UV2.y);

    vec4 albedoSample = texture(u_Tex[0], uv);
    vec3 albedo = albedoSample.rgb * u_Tint;
    float alpha = albedoSample.a;

    float ao = saturate(texture(u_Tex[4], uv).r);

    vec3 N = normalize(v_NormalWS);
    N = NormalFromMap(uv, N);

    vec3 finalColor = vec3(0.0);

    if (u_LightmapStrength > 0.0)
    {
        vec3 lm = DecodeRGBM(texture(u_Tex[5], uv2));

        if (u_LightmapDirStrength > 0.0)
        {
            vec3 dir = normalize(texture(u_Tex[6], uv2).rgb * 2.0 - 1.0);
            float ndl = saturate(dot(N, dir));
            float dir_factor = mix(1.0, ndl, saturate(u_LightmapDirStrength));
            lm *= dir_factor;
        }

        vec3 baked = (albedo / BH_PI) * lm * ao;
        finalColor += baked * u_LightmapStrength;
    }
    else
    {
        finalColor += u_AmbientStrength * albedo * ao;
    }

    float metallic = texture(u_Tex[3], uv).r * u_MetallicFactor;
    float roughness = texture(u_Tex[2], uv).r * u_RoughnessFactor;
    metallic = saturate(metallic);
    roughness = saturate(roughness);

    vec3 dN = fwidth(N);
    float geometricRoughness = length(dN);
    roughness = max(roughness, geometricRoughness);
    roughness = max(roughness, 0.04);

    vec3 V = normalize(u_CameraPos - v_PositionWS);

    float shadowFactor = 1.0;
    if (u_ShadowmaskStrength > 0.0)
    {
        float shadowMask = saturate(texture(u_Tex[7], uv2).r);
        shadowFactor = mix(1.0, shadowMask, saturate(u_ShadowmaskStrength));
    }

    vec3 L = normalize(u_LightDirection);
    vec3 pbr = PBR_Direct(N, V, L, albedo, metallic, roughness, u_LightColor, u_LightIntensity);
    finalColor += pbr * shadowFactor;

    finalColor = pow(finalColor, vec3(1.0 / 2.2));

    o_Color = vec4(finalColor, alpha);
}