#version 450

// Toon hair ring: alpha-blended over the hair. A bluish band whose sphere-map glint turns white.
// Only the HairOverlay class draws; hairHighlight is the band opacity.

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
    vec3 whiteLit = ShadeDirectionalLinear(vec3(1.0), normalize(SceneSunDirection()), false, inWorldPos);
    float glint = smoothstep(0.4, 0.9, dot(texture(uSphereTexture, ToonMatcapUv(n)).rgb, kToonLuma));
    vec3 color = texel.rgb * whiteLit * mix(kToonRingTint, vec3(1.0), glint);
    outColor = vec4(min(color, vec3(kToonMaxRadiance)), clamp(look.hairHighlight, 0.0, 1.0));
}
