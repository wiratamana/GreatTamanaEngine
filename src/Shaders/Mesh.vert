#version 450

// Vertex shader for the engine's shared "imported mesh" draw path (see
// src/Renderer/MeshVertex.h for the matching C++ layout, Pipeline.h's
// VertexLayout::PositionNormal, and Game::CreateMeshEntityFromGtaFile(),
// src/Game/Game.cpp - the thing that actually builds a Mesh of these and a
// Pipeline against this shader pair). Distinct from Triangle.vert (which
// consumes position+color instead) - an imported *.gta AssetType::Mesh
// carries no per-vertex color, only a real per-vertex NORMAL (see
// src/Assets/MeshData.h), so this shader pair exists specifically to shade
// with that instead. Compiled to SPIR-V at build time by
// cmake/CompileShaders.cmake (glslc) - never committed as a binary, see
// .gitignore.

// Same push-constant SHAPE as Triangle.vert's own PushConstants block (a
// "model" matrix immediately followed by a "viewProj" matrix, both
// column-major, uploaded directly from Mat4::Data() - see Math/Mat4.h and
// Renderer/FrameRecorder.cpp's vkCmdPushConstants call) - this is what lets
// RenderSystem::Draw()/Renderer::Submit() treat this Pipeline exactly like
// the default one, with no special-casing anywhere in the draw-submission
// path.
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

// World-space normal, interpolated across the triangle and consumed by
// Mesh.frag for a simple fixed-direction lambert term - see that file's own
// comment for why this engine has no real material/lighting system yet.
// mat3(pc.model) (no inverse-transpose) is only exactly correct for a
// uniform scale - see Math/Mat4.h's TransformNormal() for the general case -
// but every entity spawned via Game::CreateMeshEntityFromGtaFile() starts
// with an identity Transform (see its own comment), so this doesn't matter
// yet; revisit if/when a spawned mesh entity's Transform is given a
// non-uniform scale.
layout(location = 0) out vec3 outWorldNormal;

void main()
{
    gl_Position = pc.viewProj * pc.model * vec4(inPosition, 1.0);
    outWorldNormal = mat3(pc.model) * inNormal;
}
