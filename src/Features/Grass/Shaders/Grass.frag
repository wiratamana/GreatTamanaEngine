#version 450

// Opaque blade shading: species palette, two-sided transmission, sheen, distance flatten and far luminance match.
// Linear HDR radiance with the same energy scale as DirectionalLighting.glsl (kDirectScale 0.65).
#include "GrassCommon.glsl"

// NOTE: must match GrassPushGpu in GrassTypes.h and GrassPush in Grass.vert (16 bytes).
layout(push_constant) uniform GrassPush {
    uint lod;
    uint instanceBase;
    uint reserved0;
    uint reserved1;
} pc;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec4 inInfo;
layout(location = 2) in vec3 inPosRel;
layout(location = 3) flat in vec4 inGround;  // rgb = terrain colour, a = ground normal . sun.
layout(location = 4) flat in float inSunVis; // Per-blade sun visibility (cascades and cloud shadow).
layout(location = 5) in vec3 inTangent;
layout(location = 6) flat in uint inPatch;

layout(location = 0) out vec4 outColor;

const float kDirectScale = 0.65; // Same sun energy scale as the lit material shaders.

void main()
{
    vec3 viewDir = normalize(-inPosRel);
    // Two-sided: turn the normal toward the viewer (does not depend on the viewport winding).
    vec3 normal = normalize(inNormal);
    if (dot(normal, viewDir) < 0.0) {
        normal = -normal;
    }
    vec3 tangent = normalize(inTangent);

    float t = inInfo.x;
    float rnd = inInfo.y;
    float distanceM = length(inPosRel);
    float lookModern = params.variety2.y;
    float flattenStart = params.lodFar.y;
    float far = smoothstep(flattenStart, max(140.0, flattenStart + 30.0), distanceM) * lookModern;
    float tEff = mix(t, 0.7, far);

    bool isFlower = (inPatch == kGrassKindFlower);
    bool isDry = (inPatch == kGrassKindDry);
    bool isWeed = (inPatch == kGrassKindWeed);
    float pt = (inPatch <= kGrassPatchMax) ? (float(inPatch) * (1.0 / float(kGrassPatchMax))) : 0.5;
    float oliveMix = smoothstep(0.25, 0.80, pt) * params.variety2.w;
    float lushMix = (1.0 - smoothstep(0.0, 0.35, pt)) * params.variety2.w;

    vec3 baseRoot = params.colorRoot.rgb;
    vec3 baseTip = params.colorTip.rgb;
    // Patch palette: lush deep green at low patch values, yellow-olive meadow at high values.
    vec3 oliveScale = vec3(2.1, 1.0, 1.6);
    vec3 lushScale = vec3(0.7, 1.08, 0.8);
    vec3 rootCol = mix(mix(baseRoot, baseRoot * lushScale, lushMix), baseRoot * oliveScale, oliveMix);
    vec3 tipCol = mix(mix(baseTip, baseTip * lushScale, lushMix), baseTip * oliveScale, oliveMix);

    if (isWeed) {
        rootCol = baseRoot * vec3(0.85, 0.92, 1.15);
        tipCol = baseTip * vec3(0.72, 0.88, 1.25);
    } else if (isDry) {
        // Straw blades fade back to the meadow colour with distance so they do not read as orange specks.
        float dryFar = smoothstep(50.0, 140.0, distanceM);
        float tipLuma = max(dot(baseTip, vec3(0.2126, 0.7152, 0.0722)) / 0.1668, 0.25);
        vec3 dryTip = vec3(0.30, 0.22, 0.070) * tipLuma;
        tipCol = mix(dryTip, tipCol, dryFar);
        rootCol = mix(dryTip * vec3(0.32, 0.34, 0.30), rootCol, dryFar);
    }

    float tint = params.colorTip.w;
    vec3 albedo = mix(rootCol, tipCol, smoothstep(0.0, 1.0, tEff));
    float varModern = 1.0 + tint * (rnd * 2.0 - 1.0) * 0.36;
    float varLegacy = 1.0 + tint * (rnd * 2.0 - 1.0) * 0.60;
    albedo *= mix(varLegacy, varModern, lookModern);
    albedo = mix(albedo, albedo * vec3(1.35, 1.1, 0.55), (1.0 - lookModern) * tint * smoothstep(0.6, 1.0, rnd));

    // Mild darker midrib and slightly lighter blade edges.
    float mid = smoothstep(0.0, 0.25, abs(inInfo.w));
    albedo *= mix(1.0, mix(0.88, 1.06, mid), lookModern);

    float flowerMask = 0.0;
    if (isFlower && pc.lod == 0u) {
        float flowerVis = 1.0 - smoothstep(0.65 * params.lodDist.x, 0.95 * params.lodDist.x, distanceM);
        flowerMask = smoothstep(0.86, 0.91, t) * flowerVis;
        uint flowerIdx = uint(clamp(rnd * 15.0 + 0.5, 0.0, 15.0)) & 3u;
        vec3 blossom = vec3(0.80, 0.80, 0.75);
        if (flowerIdx == 1u) {
            blossom = vec3(0.90, 0.70, 0.05);
        } else if (flowerIdx == 2u) {
            blossom = vec3(0.35, 0.15, 0.60);
        } else if (flowerIdx == 3u) {
            blossom = vec3(0.75, 0.28, 0.42);
        }
        blossom *= mix(0.75, 1.05, smoothstep(0.88, 0.98, t));
        albedo = mix(albedo, blossom, flowerMask);
    }

    // Roots take the terrain colour; tips and flower petals keep more of their own colour.
    float farBlend = params.extra.z * smoothstep(0.2 * params.lodDist.z, 0.9 * params.lodDist.z, distanceM);
    float nearTerrainMix = params.look.x * mix(0.9, 0.4, tEff) * (1.0 - 0.85 * flowerMask);
    albedo = mix(albedo, inGround.rgb, max(nearTerrainMix, farBlend));

    uint debugMode = params.misc.x;
    if (debugMode == 1u) {
        vec3 lodColor = pc.lod == 0u ? vec3(0.0, 1.0, 0.0) : (pc.lod == 1u ? vec3(1.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0));
        albedo = lodColor * 0.5;
    } else if (debugMode == 2u) {
        albedo = mix(vec3(0.0, 0.2, 1.0), vec3(1.0, 0.1, 0.0), clamp(inInfo.z / 0.9, 0.0, 1.0)) * 0.5;
    } else if (debugMode == 3u) {
        // Chunk checker with bright borders, from the world position of the fragment.
        vec2 chunk = (params.cameraWorld.xy + inPosRel.xz) / params.camRel.w;
        vec2 cell = floor(chunk);
        vec2 edge = min(fract(chunk), 1.0 - fract(chunk));
        float border = 1.0 - step(0.03, min(edge.x, edge.y));
        float checker = mod(cell.x + cell.y, 2.0);
        albedo = mix(mix(vec3(0.05, 0.25, 0.6), vec3(0.6, 0.3, 0.05), checker), vec3(1.0), border) * 0.5;
    } else if (debugMode == 4u) {
        vec3 visColor = mix(vec3(0.03, 0.06, 0.35), vec3(0.95, 0.90, 0.35), clamp(inSunVis, 0.0, 1.0));
        outColor = vec4(visColor * (0.75 + 0.25 * t), 1.0);
        return;
    }

    vec3 sunDir = normalize(params.sunDir.xyz);
    float sunUp = smoothstep(-0.05, 0.1, sunDir.y);
    float groundLit = mix(1.0, smoothstep(-0.1, 0.3, inGround.a), params.look.z);
    float sunVis = clamp(inSunVis, 0.0, 1.0);
    vec3 sun = params.sunRadiance.rgb * (kDirectScale * params.sunRadiance.w * sunUp * groundLit * sunVis);

    float nl = dot(normal, sunDir);
    float front = clamp((nl + 0.35) / 1.35, 0.0, 1.0);
    float hemi = normal.y * 0.5 + 0.5;
    vec3 skyFill = params.sunRadiance.rgb
        * (0.3 * kDirectScale * params.sunRadiance.w * sunUp * max(sunDir.y, 0.0) * params.skyAmbient.w * (1.0 - sunVis) * hemi);
    vec3 ambient = mix(params.groundAmbient.rgb, params.skyAmbient.rgb, hemi) + skyFill;

    // Legacy lighting model for A/B comparison (lookModern = 0).
    float aoLegacy = mix(params.colorRoot.w, 1.0, smoothstep(0.0, 0.45, t));
    float throughLegacy = pow(clamp(dot(viewDir, -sunDir), 0.0, 1.0), 3.0);
    vec3 colorLegacy = albedo * (ambient * aoLegacy + sun * front * sqrt(aoLegacy))
        + albedo * vec3(1.1, 1.2, 0.5) * sun * (throughLegacy * params.sunDir.w * mix(0.5, 1.0, t) * aoLegacy);

    // Modern shading: distance-flattened AO, two-sided leaf transmission, Kajiya-Kay sheen, sky Fresnel and wind brightness wave.
    float aoNear = mix(params.colorRoot.w, 1.0, pow(max(smoothstep(0.0, 0.5, t), 1e-4), 0.8));
    float ao = mix(aoNear, 0.92, far);
    float back = clamp(-nl, 0.0, 1.0);
    vec3 transTint = mix(vec3(1.05, 1.15, 0.45), vec3(1.0), flowerMask);
    vec3 leaf = albedo * transTint;
    float vs = pow(clamp(dot(viewDir, -sunDir), 0.0, 1.0), 4.0);
    float thin = mix(0.55, 1.0, t);
    vec3 trans = leaf * sun * (params.sunDir.w * thin * (0.45 * back + 2.0 * vs) * ao);

    vec3 halfSum = sunDir + viewDir;
    float halfLen2 = dot(halfSum, halfSum);
    vec3 H = halfLen2 > 1e-6 ? halfSum * inversesqrt(halfLen2) : vec3(0.0, 1.0, 0.0);
    float th = clamp(dot(tangent, H), -1.0, 1.0);
    float sinTH = sqrt(max(1.0 - th * th, 0.0));
    // Sheen and rim stay green-tinted and weak so blades never turn grey.
    float spec = (halfLen2 > 1e-4 ? pow(sinTH, 48.0) : 0.0) * 0.05 * (1.0 - far);
    float fres = pow(1.0 - clamp(dot(normal, viewDir), 0.0, 1.0), 4.0) * 0.06;

    vec3 colorModern = albedo * (ambient * ao + sun * front * sqrt(ao)) + trans
        + sun * spec * vec3(0.9, 1.0, 0.6) + params.skyAmbient.rgb * vec3(0.5, 0.9, 0.35) * (fres * ao * (1.0 - far));
    float windWave = clamp(inInfo.z / 0.9 - 0.4, -0.4, 0.6);
    colorModern *= max(1.0 + params.shapeWind.w * windWave * (1.0 - far), 0.0);

    vec3 color = mix(colorLegacy, colorModern, lookModern);
    if (debugMode == 0u) {
        color *= mix(1.0, params.lodFar.z, smoothstep(75.0, 125.0, distanceM));
    }
    outColor = vec4(color, 1.0);
}
