#version 450

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 lightViewProj;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal; // Unused.
layout(location = 2) in vec2 inUv;     // Unused.

void main()
{
    gl_Position = pc.lightViewProj * pc.model * vec4(inPosition, 1.0);
}
