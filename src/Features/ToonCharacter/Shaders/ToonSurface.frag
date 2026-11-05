#version 450

// Toon surface: two-band cel ramp with coloured shade, sun and cascade shadows, rim, unlit eyes, gem glow.

#include "DirectionalLighting.glsl"
#include "MaterialParams.glsl"
#include "ToonCommon.glsl"

layout(set = 0, binding = 0) uniform sampler2D uDiffuseTexture;
layout(set = 0, binding = 5) uniform sampler2D uSphereTexture; // Sphere map, already linear.

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in vec3 inHeadForward;
layout(location = 4) in float inModelScale;

layout(location = 0) out vec4 outColor;

void Emit(vec3 color)
{
    outColor = vec4(clamp(color, vec3(0.0), vec3(kToonMaxRadiance)), 1.0);
}

void main()
{
    int cls = ToonClassOf(uMaterial.pbr.y); // HairOverlay parts never get here: the vertex stage clips them.

    vec4 texel = texture(uDiffuseTexture, inUv) * uMaterial.baseColorFactor; // SRGB format: already linear here.
    if (texel.a < kToonAlphaCutoff) { discard; }
    vec3 albedo = texel.rgb;

    ToonLookParams look = ReadToonLook();
    if (look.debugView == 5) { Emit(albedo); return; }
    if (look.debugView == 1) { Emit(kToonClassDebugColor[cls]); return; }
    if (look.debugView == 4) { Emit(vec3(0.0)); return; }

    bool unlit = (cls == kToonEye || cls == kToonFaceLine);
    vec3 n = normalize(gl_FrontFacing ? inWorldNormal : -inWorldNormal); // Double-sided: light the visible face.
    vec3 v = normalize(SceneEyePosition() - inWorldPos);
    vec3 l = normalize(SceneSunDirection());

    // Face has no shadow map: flatten the normal toward the head forward so it gets no nose or cheek split.
    vec3 shadeNormal = (cls == kToonFaceSkin) ? normalize(mix(n, normalize(inHeadForward), look.faceFlatten)) : n;

    float sunShare = ToonShadeSunShare(); // 1 while the sun is high, 0 when it is low (toon look gives way to the real light).
    float shadow = 0.0;
    float band = 1.0;
    float deep = 1.0;
    if (!unlit) {
        float nDotL = dot(n, l);
        float shadeNDotL = dot(shadeNormal, l);
        // Gems ignore the band and the shadow in the final colour, so they skip the lookup.
        bool gemSkipsShadow = (cls == kToonGem) && (look.debugView != 2) && (look.debugView != 3);
        if (SceneSunWritten() && !gemSkipsShadow) { shadow = ToonSunShadow(inWorldPos, n, nDotL, l, kToonShadowReach * inModelScale); }
        float h = (shadeNDotL * 0.5 + 0.5) * (1.0 - look.shadowStrength * shadow * sunShare);
        float w = max(look.bandSoftness, 1e-4);
        float w2 = 2.0 * w;
        band = smoothstep(look.bandThreshold - w, look.bandThreshold + w, h);
        deep = smoothstep(look.deepThreshold - w2, look.deepThreshold + w2, h);
    }
    if (look.debugView == 2) { Emit(vec3(0.5 * deep + 0.5 * band)); return; } // lit 1, shade 0.5, deep 0
    if (look.debugView == 3) { Emit(vec3(shadow)); return; }

    if (unlit) { Emit(albedo * min(ToonShadeSideLight(), vec3(1.0))); return; }

    // Light split: ambient plus the sun. The lit side sees the sky like the sun does, the shade side like its own normal.
    // A cast shadow removes `shadowStrength` of the sun while the sun is high and all of it as the sun gets low.
    // The shade side keeps only `sunShare` of the sun.
    vec3 skyDir = mix(n, l, max(band, sunShare));
    vec3 ambient = SceneHemisphereAmbient(dot(skyDir, skyDir) > 1e-4 ? normalize(skyDir) : n);
    vec3 sunFull = SceneSunIlluminance() * kDirectScale;
    float castDepth = (look.shadowStrength > 0.0) ? mix(1.0, look.shadowStrength, sunShare) : 0.0;
    float castVisible = 1.0 - clamp(castDepth * uSceneLighting.shadowParams.y * shadow, 0.0, 1.0);
    vec3 litLight = ambient + sunFull;
    vec3 light = ambient + sunFull * (mix(sunShare, 1.0, band) * castVisible);

    vec3 shadeTint = mix(vec3(1.0), kToonShadeTint[cls], look.shadeBlend);
    vec3 tone = mix(shadeTint * kToonDeepTint[cls], shadeTint, deep);
    tone = mix(tone, vec3(1.0), band);
    float toneLuma = dot(tone, kToonLuma);
    // Lift toward white until the luminance floor holds; channels stay at or below 1.
    if (toneLuma < kToonMinShadeLuma) { tone = mix(tone, vec3(1.0), (kToonMinShadeLuma - toneLuma) / max(1.0 - toneLuma, 1e-4)); }

    vec3 color = albedo * light * tone;

    if (cls == kToonGem) {
        vec3 sphere = texture(uSphereTexture, ToonMatcapUv(n)).rgb;
        color = albedo * litLight * look.emissiveBoost + sphere * litLight;
    } else {
        // Light-side rim: reads as a back light, not an outline.
        float fres = 1.0 - clamp(dot(n, v), 0.0, 1.0);
        float rim = smoothstep(look.rimWidth, look.rimWidth + 0.15, fres)
            * clamp(dot(n, l) * 0.5 + 0.6, 0.0, 1.0) * look.rimStrength * kToonRimScale[cls] * (1.0 - shadow);
        color += rim * kToonRimTint * litLight * mix(vec3(1.0), albedo, 0.5);

        // Bright cyan-blue cloth (Hydro stones) glows; gold gets a narrow highlight.
        vec3 hsv = ToonRgbToHsv(LinearToSrgb(albedo));
        if (look.gemHeuristic > 0.5 && (cls == kToonCloth || cls == kToonClothBack)) {
            float cyan = step(185.0, hsv.x) * step(hsv.x, 215.0) * step(0.6, hsv.y) * step(0.75, hsv.z);
            color *= mix(1.0, 2.0, cyan);
        }
        if (look.goldGlint > 0.5) {
            float gold = step(25.0, hsv.x) * step(hsv.x, 50.0) * step(0.15, hsv.y) * step(hsv.y, 0.5) * step(0.5, hsv.z);
            float spec = pow(max(dot(n, normalize(l + v)), 0.0), 40.0);
            color += gold * step(0.92, spec) * 0.5 * vec3(1.0, 0.92, 0.78) * litLight * (1.0 - shadow);
        }
    }

    Emit(color);
}
