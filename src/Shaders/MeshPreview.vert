#version 450

// Vertex shader for the Editor's Inspector "Mesh Asset" 3D preview viewer
// (see src/Editor/AssetPreviewMesh.h/.cpp) - a small, self-contained
// pipeline entirely separate from Triangle.vert/frag's engine-wide
// position+color Vertex layout (src/Renderer/Vertex.h), since a mesh
// preview needs a per-vertex NORMAL (for simple directional shading)
// instead of a per-vertex color. Deliberately not routed through
// Renderer::CreatePipeline()/Renderer::Submit() at all - AssetPreviewMesh
// builds/binds this pipeline directly inside a Renderer::RenderOffscreen()
// recordExtra callback (see AGENTS.md, "Editor Module Structure": Vulkan
// types are fine to use directly anywhere in src/Editor/). Compiled to
// SPIR-V at build time by cmake/CompileShaders.cmake (glslc) - never
// committed as a binary, see .gitignore.

// Same push-constant SHAPE as Triangle.vert's own PushConstants block (a
// "model" matrix immediately followed by a "viewProj" matrix, both
// column-major, uploaded directly from Mat4::Data() - see Math/Mat4.h) -
// AssetPreviewMesh.cpp fills these exactly the same way FrameRecorder.cpp
// does for the engine's main draw path, just via its own
// vkCmdPushConstants() call instead of going through FrameRecorder.
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

// World-space normal, interpolated across the triangle and consumed by
// MeshPreview.frag for a simple fixed-direction lambert term. mat3(pc.model)
// is enough (no inverse-transpose needed) because AssetPreviewMesh only
// ever builds `model` from a pure rotation (see its own comment) - never a
// non-uniform scale, which is the one case a plain mat3(model) would skew a
// normal under (see Math/Mat4.h's TransformNormal() for the general case
// this preview deliberately doesn't need).
layout(location = 0) out vec3 outWorldNormal;

void main()
{
    gl_Position = pc.viewProj * pc.model * vec4(inPosition, 1.0);
    outWorldNormal = mat3(pc.model) * inNormal;
}
