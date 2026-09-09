#version 450

// Vertex shader for the Editor's "Scene" panel infinite ground grid (see
// task_manager/editor-enchancements-1/PHASE0_MASTER_STRATEGY.md). Unlike
// every other vertex shader in this engine, this one has NO vertex input
// at all - src/Editor/SceneGridRenderer.cpp (PHASE3) issues a bare
// vkCmdDraw(3, 1, 0, 0) with no bound vertex/index buffer. This is the
// classic "full-screen triangle from gl_VertexIndex alone" trick: 3
// vertices whose NDC positions cover the entire [-1, 1] clip-space square
// with a single triangle (the far corner sits safely outside the visible
// area, which is fine - it's clipped/rasterized normally). All of the
// actual grid math happens in SceneGrid.frag; this file exists purely to
// hand it a per-pixel NDC (x, y) to work from. Compiled to SPIR-V at build
// time by cmake/CompileShaders.cmake (glslc) - never committed as a
// binary, see .gitignore.

layout(location = 0) out vec2 outNdcXY;

void main()
{
    // gl_VertexIndex in {0, 1, 2} -> ndc in {(-1,-1), (3,-1), (-1,3)} - the
    // standard derivation: ((i << 1) & 2, i & 2) yields (0,0), (2,0),
    // (0,2) for i = 0, 1, 2; scaling by 2 and subtracting 1 maps that to
    // (-1,-1), (3,-1), (-1,3).
    vec2 ndc = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    outNdcXY = ndc;
}
