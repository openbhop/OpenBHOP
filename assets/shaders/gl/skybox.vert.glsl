#version 300 es
precision highp float;

layout(location = 0) in vec3 a_Position;

// Match CPU push layout and naming (std140, slot 0).
layout(std140) uniform VS_UBO0
{
    mat4 u_MVP;
    mat4 u_Model; // kept for layout parity even if unused
};

out vec3 v_DirWS;

void main()
{
    vec4 clip = u_MVP * vec4(a_Position, 1.0);

    // Convert Z [0,1] -> [-1,1] for WebGL/OpenGL.
    clip.z = clip.z * 2.0 - clip.w;

    gl_Position = clip;
    v_DirWS = a_Position;
}
