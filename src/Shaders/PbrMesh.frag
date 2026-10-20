#version 450

// Lit PBR mesh fragment shader: normal map, metallic-roughness, occlusion, GGX sun specular; outputs linear scene color.

#include "PbrMaterial.glsl"

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in vec4 inWorldTangent;
layout(location = 4) flat in float inWindingSign;

layout(location = 0) out vec4 outColor;

void main()
{
    PbrFragmentInput f = PbrFragmentInput(inWorldNormal, inWorldTangent, inWorldPos, inUv, inWindingSign, gl_FrontFacing);
    outColor = vec4(ShadePbrMaterial(SampleBaseColor(inUv).rgb, f), 1.0);   // Opaque pipeline: alpha is always 1.
}
