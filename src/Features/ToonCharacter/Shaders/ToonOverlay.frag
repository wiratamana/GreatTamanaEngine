#version 450

// Toon hair ring: alpha-blended bluish-white band on the upper hair, masked by the camera-up normal.
// Only the HairOverlay class draws; hairHighlight is the band opacity.

#include "DirectionalLighting.glsl"
#include "MaterialParams.glsl"
#include "ToonCommon.glsl"

layout(set = 0, binding = 0) uniform sampler2D uDiffuseTexture;

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

    vec3 n = normalize(gl_FrontFacing ? inWorldNormal : -inWorldNormal);
    vec3 right, up;
    ToonCameraAxes(right, up);
    float ny = dot(n, up);
    float mask = smoothstep(0.30, 0.50, ny) * (1.0 - smoothstep(0.62, 0.80, ny));
    float alpha = clamp(look.hairHighlight, 0.0, 1.0) * mask;
    if (alpha < 0.01) { discard; }

    vec4 texel = texture(uDiffuseTexture, inUv) * uMaterial.baseColorFactor;
    if (texel.a < kToonAlphaCutoff) { discard; }

    // Shadowed sun term: the band dims with the hair underneath.
    vec3 lit = ShadeDirectionalLinear(vec3(1.0), normalize(SceneSunDirection()), true, inWorldPos);
    vec3 color = mix(texel.rgb, kToonRingBand, 0.8) * lit;
    outColor = vec4(min(color, vec3(kToonMaxRadiance)), alpha);
}
