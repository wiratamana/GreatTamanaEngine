#version 450

// First hardcoded draw path's vertex shader (see src/Renderer/Vertex.h for
// the matching C++ layout, and src/Game/RenderSystem.cpp for where the
// per-draw model matrix below comes from). Compiled to SPIR-V at build time
// by cmake/CompileShaders.cmake (glslc) - never committed as a binary, see
// .gitignore.

// Per-draw world matrix, pushed via vkCmdPushConstants right before this
// draw (see Renderer/Pipeline.cpp's VkPushConstantRange and
// Renderer/FrameRecorder.cpp's vkCmdPushConstants call). Column-major,
// uploaded directly from Mat4::Data() with zero transpose - see
// Math/Mat4.h's class comment for why that layout matches GLSL's mat4
// as-is.
layout(push_constant) uniform PushConstants {
    mat4 model;
} pc;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main()
{
    gl_Position = pc.model * vec4(inPosition, 0.0, 1.0);
    fragColor = inColor;
}
