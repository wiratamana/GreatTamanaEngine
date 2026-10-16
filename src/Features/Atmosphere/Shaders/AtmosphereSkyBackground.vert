#version 450

// Fullscreen triangle from gl_VertexIndex alone, drawn at NDC depth 1.0.
// With an EQUAL depth test it only covers pixels still at the depth clear value (nothing drawn there).

layout(location = 0) out vec2 outNdcXY;

void main()
{
    vec2 ndc = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 1.0, 1.0);
    outNdcXY = ndc;
}
