#version 450

// Viewer fragment shader: neutral clay color, lit and display-encoded.

#include "ClusterViewShading.glsl"

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec3 inWorldPos;
layout(location = 2) flat in uint inClusterId;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 baseColor = pow(vec3(0.72, 0.73, 0.76), vec3(2.2));
    outColor = vec4(ToDisplay(ViewLight(baseColor, inWorldNormal)), 1.0);
}
