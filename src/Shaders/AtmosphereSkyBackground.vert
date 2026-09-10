#version 450

// Vertex shader for the Atmosphere Sky Background pass (Atmosphere
// Scattering + Aerial Perspective campaign, Phase 7 -
// task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASE7_SKY_BACKGROUND_AND_COMPOSITE_PASSES_v1.md).
// Mirrors src/Shaders/SceneGrid.vert's own "full-screen triangle from
// gl_VertexIndex alone, zero vertex input" technique EXACTLY (see that
// file's own header comment for the full derivation: gl_VertexIndex in
// {0, 1, 2} -> ndc in {(-1,-1), (3,-1), (-1,3)}, a single triangle that
// safely covers the whole [-1,1] clip-space square).
//
// The one deliberate difference from SceneGrid.vert: this shader writes a
// FIXED NDC depth of exactly 1.0 (gl_Position.z = gl_Position.w, so the
// perspective divide yields z/w == 1.0) - per this phase's own CONFIRMED
// depth convention (this engine clears depth to 1.0, the far plane, every
// frame - see FrameRecorder.cpp's own depthAttachment.clearValue - and
// every real pipeline depth-tests with VK_COMPARE_OP_LESS). Paired with
// this pass's own VK_COMPARE_OP_EQUAL depth-compare op (see
// AtmosphereSkyBackgroundRenderer.cpp), this fragment only ever survives
// the depth test at a pixel whose depth is STILL exactly the clear value -
// i.e. nothing real (real scene geometry, or a later same-frame overlay
// like the Editor's SceneGridRenderer) was ever drawn there.

layout(location = 0) out vec2 outNdcXY;

void main()
{
    vec2 ndc = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 1.0, 1.0);
    outNdcXY = ndc;
}
