#version 450

// Depth-only write pass for a per-layer TextureArray attachment (see
// src/Editor/ArrayLayerRenderValidation.h/.cpp) - two separate column-major
// mat4 push constants (model, viewProj), mirroring Triangle.vert/Mesh.vert's
// own identical convention. Writes gl_Position only - this pipeline has zero
// color attachments.

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor; // unread - VertexLayout::PositionColor always binds this attribute.

void main()
{
    gl_Position = pc.viewProj * pc.model * vec4(inPosition, 1.0);
}
