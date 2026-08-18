#version 450

// First hardcoded draw path's vertex shader (see src/Renderer/Vertex.h for
// the matching C++ layout, and src/Game/Game.cpp for where this triangle's
// vertex data comes from). Compiled to SPIR-V at build time by
// cmake/CompileShaders.cmake (glslc) - never committed as a binary, see
// .gitignore.

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main()
{
    gl_Position = vec4(inPosition, 0.0, 1.0);
    fragColor = inColor;
}
