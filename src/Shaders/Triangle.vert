#version 450

// First hardcoded draw path's vertex shader (see src/Renderer/Vertex.h for
// the matching C++ layout, and src/Game/RenderSystem.cpp for where the
// per-draw model matrix and the active Camera's view-projection matrix
// below come from). Compiled to SPIR-V at build time by
// cmake/CompileShaders.cmake (glslc) - never committed as a binary, see
// .gitignore.

// Per-draw world matrix ("model") plus the active Camera's combined
// view-projection matrix ("viewProj"), pushed via vkCmdPushConstants right
// before this draw (see Renderer/Pipeline.cpp's VkPushConstantRange and
// Renderer/FrameRecorder.cpp's vkCmdPushConstants call) - model first
// (offset 0), viewProj right after (offset 64), 128 bytes total. Both
// column-major, uploaded directly from Mat4::Data() with zero transpose -
// see Math/Mat4.h's class comment for why that layout matches GLSL's mat4
// as-is. A scene with no active Camera (ECS/Components/Camera.h) pushes
// viewProj = identity (see RenderSystem::ResolveActiveCameraViewProjection),
// so vertices land directly in clip space exactly as before this engine had
// a Camera component at all.
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
} pc;

// inPosition is a real 3D position (see Renderer/Vertex.h) - the engine's
// first hardcoded triangle demo authors z=0 explicitly rather than this
// shader assuming/padding it, so this same layout also serves genuine 3D
// geometry (the built-in primitive shapes - see Renderer/Primitives/
// PrimitiveMeshGenerator.h) with no shader variant needed.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main()
{
    gl_Position = pc.viewProj * pc.model * vec4(inPosition, 1.0);
    fragColor = inColor;
}
