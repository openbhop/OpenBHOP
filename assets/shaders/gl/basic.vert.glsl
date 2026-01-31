#version 300 es
precision highp float; // Good practice to define in vertex shader too

// -----------------------------------------------------------------------------
// basic.vert.glsl
// -----------------------------------------------------------------------------

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_UV;
layout(location = 3) in vec2 a_UV2;

// Vertex uniform slot 0 (PerDraw)
layout(std140) uniform VS_UBO0
{
    mat4 u_MVP;
    mat4 u_Model;
};

out vec3 v_NormalWS;
out vec2 v_UV;
out vec3 v_PositionWS;
out vec2 v_UV2;

void main()
{
    vec4 clip = u_MVP * vec4(a_Position, 1.0);
    // Engine builds a D3D/Vulkan-style projection (LH, Z in [0,1]).
    // OpenGL expects clip-space Z in [-w, w] (NDC Z in [-1, 1]).
    // Convert so our depth buffer values match the other backends.
    clip.z = clip.z * 2.0 - clip.w;
    gl_Position = clip;

    vec4 world = u_Model * vec4(a_Position, 1.0);
    v_PositionWS = world.xyz;

    mat3 m3 = mat3(u_Model);
    v_NormalWS = normalize(m3 * a_Normal);

    v_UV = a_UV;
    v_UV2 = a_UV2;
}