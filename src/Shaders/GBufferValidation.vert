#version 450

// task_manager/mrt-1 campaign (Multi-Render-Target / G-Buffer support),
// PHASE4 (PHASE4_GBUFFER_VALIDATION_PASS_AND_SHADER.md) - vertex shader for
// the "GBufferValidation" debug pass (Editor "Scene" panel-only, opt-in,
// src/Editor/GBufferValidation.h/.cpp). Mirrors
// Shaders/AtmosphereSkyBackground.vert's own "full-screen triangle from
// gl_VertexIndex alone" technique EXACTLY (see that file's own header
// comment for the full derivation: gl_VertexIndex in {0, 1, 2} -> ndc in
// {(-1,-1), (3,-1), (-1,3)}, a single triangle that safely covers the
// whole [-1,1] clip-space square).
//
// The pipeline this shader is built into (via Renderer::CreatePipeline()'s
// N-format overload, PHASE3) DOES declare a real
// VertexLayout::PositionColor vertex binding - Pipeline's constructor
// always does, with no way to opt out (see Pipeline.h) - and a real
// (throwaway/unused) 3-vertex Mesh IS bound before this pass's own draw
// call (see GBufferValidation.cpp's own EnsureInitialized()/AddPass()) so
// that mandatory binding has SOMETHING real bound at draw time. This
// shader itself reads NONE of that vertex data - an unused vertex
// attribute is legal in Vulkan (only a shader `in` variable with nothing
// bound for it would be illegal, and this shader declares none).

layout(location = 0) out vec2 outUv;

void main()
{
    vec2 ndc = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    outUv = ndc * 0.5 + 0.5;
}
