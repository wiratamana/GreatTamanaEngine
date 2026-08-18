#version 450

// Fragment shader half of the first hardcoded draw path (see Triangle.vert).
// Just outputs the interpolated per-vertex color the rasterizer hands it -
// no lighting/texturing yet.

layout(location = 0) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(fragColor, 1.0);
}
