#version 450

// Viewer fragment shader: cluster hash color, lit and display-encoded.
// Hue matches the engine cluster tint view; brightness differs.

#include "ClusterViewShading.glsl"

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec3 inWorldPos;
layout(location = 2) flat in uint inClusterId;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(ToDisplay(ViewLight(ClusterTint(inClusterId), inWorldNormal)), 1.0);
}
