#version 450

// Toon outline colour: albedo times a class tint (never black); darkness blends the tint toward half strength.

#include "DirectionalLighting.glsl"
#include "MaterialParams.glsl"
#include "ToonCommon.glsl"

layout(set = 0, binding = 0) uniform sampler2D uDiffuseTexture;

layout(location = 0) in vec2 inUv;
layout(location = 1) flat in int inClass;

layout(location = 0) out vec4 outColor;

void main()
{
    ToonLookParams look = ReadToonLook();
    if (look.debugView >= 1 && look.debugView <= 5 && look.debugView != 4) { discard; } // Views 6..8 keep the normal look.

    vec4 texel = texture(uDiffuseTexture, inUv) * uMaterial.baseColorFactor;
    if (texel.a < kToonAlphaCutoff) { discard; }

    if (look.debugView == 4) {
        outColor = vec4(1.0);
        return;
    }
    vec3 tint = kToonOutlineTint[inClass];
    // The line follows the world light: it is never brighter than the shade side of the body.
    outColor = vec4(texel.rgb * mix(tint, tint * 0.5, look.outlineDarkness) * min(ToonShadeSideLight(), vec3(1.0)), 1.0);
}
