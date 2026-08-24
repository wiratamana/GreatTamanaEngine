#version 450

// Vertex shader for the engine's TEXTURED "imported mesh submesh" draw path
// (see src/Renderer/MeshVertex.h's MeshVertexUv for the matching C++
// layout, Pipeline.h's VertexLayout::PositionNormalUv, and
// Game::EnsureMeshAsset(), src/Game/Game.cpp - the thing that actually
// builds a Mesh of these, its Pipeline, and the per-material
// MaterialTexture this pipeline samples). Distinct from Mesh.vert (which
// consumes position+normal only, no UV, for a material-less submesh) -
// same "model"+"viewProj" push-constant shape as every other vertex shader
// in this engine (see Triangle.vert's own comment), so RenderSystem::Draw()/
// Renderer::Submit() needs no special-casing for this pipeline either.

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec3 outWorldNormal;
layout(location = 1) out vec2 outUv;

void main()
{
    gl_Position = pc.viewProj * pc.model * vec4(inPosition, 1.0);
    // mat3(pc.model) (no inverse-transpose) is only exactly correct for a
    // uniform scale - see Mesh.vert's own comment for why this doesn't
    // matter yet (every spawned mesh entity starts with an identity
    // Transform).
    outWorldNormal = mat3(pc.model) * inNormal;
    outUv = inUv;
}
