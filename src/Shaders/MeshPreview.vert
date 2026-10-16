#version 450

// Vertex shader of the mesh asset preview: position + normal vertices, world-space outputs.

// Column-major matrices, uploaded from Mat4::Data().
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

// World-space normal and position for the fragment shader's lighting.
layout(location = 0) out vec3 outWorldNormal;
layout(location = 1) out vec3 outWorldPos;

void main()
{
    vec4 worldPos4 = pc.model * vec4(inPosition, 1.0);
    gl_Position = pc.viewProj * worldPos4;
    // mat3(model) is enough: the preview model matrix is a pure rotation, never a non-uniform scale.
    outWorldNormal = mat3(pc.model) * inNormal;
    outWorldPos = worldPos4.xyz;
}
