#version 450

// Toon additive overlay: hair highlight ring from the sphere map. Only the HairOverlay class draws.

#include "DirectionalLighting.glsl"
#include "MaterialParams.glsl"
#include "ToonCommon.glsl"

layout(set = 0, binding = 0) uniform sampler2D uDiffuseTexture;
layout(set = 0, binding = 5) uniform sampler2D uSphereTexture; // Sphere map, already linear.

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in vec3 inHeadForward;

layout(location = 0) out vec4 outColor;

void main()
{
    if (ToonClassOf(uMaterial.pbr.y) != kToonHairOverlay) { discard; }

    ToonLookParams look = ReadToonLook();
    if (look.debugView != 0) { discard; }

    vec4 texel = texture(uDiffuseTexture, inUv) * uMaterial.baseColorFactor;
    if (texel.a < kToonAlphaCutoff) { discard; }

    vec3 n = normalize(gl_FrontFacing ? inWorldNormal : -inWorldNormal);
    vec3 ring = texture(uSphereTexture, ToonMatcapUv(n)).rgb * texel.rgb;
    outColor = vec4(min(ring * look.hairHighlight, vec3(kToonMaxRadiance)), 1.0);
}
