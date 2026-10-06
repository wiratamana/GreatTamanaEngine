#version 450

// Fixed engine push-constant convention: mat4 model + mat4 viewProj.
// Caller must pass the real light-space matrix here, not the camera's.
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 lightViewProj;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal; // Unused - PositionNormal always binds this.

void main()
{
    gl_Position = pc.lightViewProj * pc.model * vec4(inPosition, 1.0);
}
