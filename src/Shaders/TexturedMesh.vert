#version 450

// Textured mesh vertex shader: position + normal + uv vertices, world-space outputs.

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec3 outWorldNormal;
layout(location = 1) out vec2 outUv;
layout(location = 2) out vec3 outWorldPos;

void main()
{
    vec4 worldPos4 = pc.model * vec4(inPosition, 1.0);
    gl_Position = pc.viewProj * worldPos4;
    // mat3(model) is exact only for rotation and uniform scale; no inverse-transpose.
    outWorldNormal = mat3(pc.model) * inNormal;
    outUv = inUv;
    outWorldPos = worldPos4.xyz;
}
