#version 450

// Toon hair ring: alpha-blended blue band on the upper hair, masked by the camera-up normal.
// The band ignores the material texture: its layer texture is a faint alpha mask, not a colour source.
// Debug views 6..8 draw every HairOverlay fragment: flat magenta, mask test, camera-up normal.

#include "DirectionalLighting.glsl"
#include "MaterialParams.glsl"
#include "ToonCommon.glsl"

layout(location = 0) in vec3 inWorldNormal;
layout(location = 2) in vec3 inWorldPos;

layout(location = 0) out vec4 outColor;

void main()
{
    if (ToonClassOf(uMaterial.pbr.y) != kToonHairOverlay) { discard; }

    ToonLookParams look = ReadToonLook();
    if (look.debugView == 6) { outColor = vec4(1.0, 0.0, 1.0, 1.0); return; }
    if (look.debugView != 0 && look.debugView < 6) { discard; } // Views 1..5 show the surface only.

    vec3 n = normalize(gl_FrontFacing ? inWorldNormal : -inWorldNormal);
    vec3 right, up;
    ToonCameraAxes(right, up);
    float ny = dot(n, up);
    float mask = smoothstep(0.30, 0.50, ny) * (1.0 - smoothstep(0.62, 0.80, ny));
    if (look.debugView == 7) {
        outColor = (mask > 0.01) ? vec4(1.0, 0.0, 1.0, 1.0) : vec4(vec3(clamp(ny, 0.0, 1.0)), 1.0);
        return;
    }
    if (look.debugView == 8) { outColor = vec4(vec3(ny * 0.5 + 0.5), 1.0); return; }

    float alpha = clamp(look.hairHighlight, 0.0, 1.0) * mask;
    if (alpha < 0.01) { discard; }

    // Shadowed sun term: the band dims with the hair underneath.
    vec3 lit = ShadeDirectionalLinear(vec3(1.0), normalize(SceneSunDirection()), true, inWorldPos);
    outColor = vec4(min(kToonRingBand * lit, vec3(kToonMaxRadiance)), alpha);
}
