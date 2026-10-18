#version 450

// Vertex shader of the mesh asset preview: position + normal vertices, world-space normal output.

// Column-major matrices, uploaded from Mat4::Data().
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 outWorldNormal;

void main()
{
    gl_Position = pc.viewProj * (pc.model * vec4(inPosition, 1.0));
    // mat3(model) is enough: the preview model matrix is a pure rotation, never a non-uniform scale.
    outWorldNormal = mat3(pc.model) * inNormal;
}
