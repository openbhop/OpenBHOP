#version 300 es

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;

// Replaced UBO with standard uniform
uniform mat4 u_MVP;

out vec4 v_Color;

void main()
{
    vec4 clip = u_MVP * vec4(a_Position, 1.0);
    
    // Convert Z [0,1] -> [-1,1] for WebGL/OpenGL.
    clip.z = clip.z * 2.0 - clip.w;
    
    gl_Position = clip;
    v_Color = a_Color;
}