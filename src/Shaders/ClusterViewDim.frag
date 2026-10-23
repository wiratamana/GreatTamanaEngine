#version 450

// Viewer fragment shader: flat dim color for culled clusters.

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec3 inWorldPos;
layout(location = 2) flat in uint inClusterId;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(58.0 / 255.0, 60.0 / 255.0, 68.0 / 255.0, 1.0);
}
