#version 450

// Viewer id pass: writes the cluster index + 1 as 24 bits into an 8-bit RGBA target (0 = background).

layout(location = 2) flat in uint inClusterId;

layout(location = 0) out vec4 outColor;

vec4 Pack24(uint v)
{
    return vec4(float(v & 255u), float((v >> 8u) & 255u), float((v >> 16u) & 255u), 255.0) / 255.0;
}

void main()
{
    outColor = Pack24(inClusterId + 1u);
}
