#version 450

// Imported-mesh vertex shader: position + normal inputs, world-space normal and position outputs.

// Column-major model and view-projection matrices, uploaded straight from Mat4::Data().
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 outWorldNormal;
layout(location = 1) out vec3 outWorldPos;

void main()
{
    vec4 worldPos4 = pc.model * vec4(inPosition, 1.0);
    gl_Position = pc.viewProj * worldPos4;
    // mat3(model) is only exact for uniform scale (no inverse-transpose).
    outWorldNormal = mat3(pc.model) * inNormal;
    outWorldPos = worldPos4.xyz;
}
