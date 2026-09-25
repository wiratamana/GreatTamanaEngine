#version 450

// editor-core-separation-9 campaign, PHASE3
// (PHASE3_GENERIC_RESOURCE_PLUMBING_AND_BLUR_DEMO_PROOF.md, Step 3.1) - vertex
// shader for `gte.builtin.blit_fullscreen`, this campaign's ONE brand-new,
// tiny, additive GRAPHICS-kind PluginRenderOperationRegistry entry (the
// second proof, alongside PHASE2's `gte.builtin.box_blur`, that a new
// operation lands with ZERO IPluginRenderPassBuilder_v3 interface change -
// this time for a GRAPHICS-kind operation).
//
// Mirrors Shaders/AtmosphereSkyBackground.vert's/Shaders/GBufferValidation.vert's
// own "full-screen triangle from gl_VertexIndex alone" technique EXACTLY - do
// not reinvent this, copy the exact formula (gl_VertexIndex in {0, 1, 2} ->
// ndc in {(-1,-1), (3,-1), (-1,3)}, a single triangle that safely covers the
// whole [-1,1] clip-space square).
//
// The pipeline this shader is built into
// (PluginRenderOperationRegistry::RegisterBlitFullscreen()) is built through
// the standard, shared `Pipeline` class (Renderer/Pipeline.h) with
// VertexLayout::PositionColor - Pipeline's constructor always declares a real
// vertex binding, with no way to opt out (see Pipeline.h's own class
// comment) - and a real (throwaway/unused) 3-vertex Mesh IS bound before
// this pass's own draw call (see
// PluginRenderPassBuilderAdapter_v3::DrawFullscreenTriangle()'s own
// PluginRenderOpInfo::dummyVertexBuffer binding, mirroring
// src/Editor/GBufferValidation.cpp's own identical "m_dummyTriangle" pattern
// for the exact same underlying reason). This shader itself reads NONE of
// that vertex data - an unused vertex attribute is legal in Vulkan (only a
// shader `in` variable with nothing bound for it would be illegal, and this
// shader declares none).

layout(location = 0) out vec2 outUv;

void main()
{
    vec2 ndc = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    outUv = ndc * 0.5 + 0.5;
}
