#version 450

// Primitive-shape vertex shader: transforms by the per-draw model and the camera view-projection.

// Column-major push constants: model at offset 0, viewProj at offset 64 (128 bytes).
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

layout(location = 0) out vec3 outWorldNormal;
layout(location = 1) out vec3 fragColor;

void main()
{
    gl_Position = pc.viewProj * (pc.model * vec4(inPosition, 1.0));
    outWorldNormal = mat3(pc.model) * inNormal;
    fragColor = inColor;
}
