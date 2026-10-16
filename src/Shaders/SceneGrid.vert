#version 450

// Fullscreen triangle from gl_VertexIndex alone (no vertex input); hands SceneGrid.frag a per-pixel NDC xy.

layout(location = 0) out vec2 outNdcXY;

void main()
{
    // Index 0, 1, 2 maps to NDC (-1,-1), (3,-1), (-1,3): one triangle covering the whole clip square.
    vec2 ndc = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    outNdcXY = ndc;
}
