#version 450
// Full-screen triangle from gl_VertexIndex alone - no vertex buffer bound.
layout(location = 0) out vec2 outUv;

void main()
{
    outUv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(outUv * 2.0 - 1.0, 0.0, 1.0);
}
