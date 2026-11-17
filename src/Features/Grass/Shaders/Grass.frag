#version 450

// Opaque blade shading: root-to-tip colour, root AO, wrap diffuse, hemisphere ambient, fake translucency.
// Linear HDR radiance with the same energy scale as DirectionalLighting.glsl (kDirectScale 0.65).
#include "GrassCommon.glsl"

// NOTE: must match the block in Grass.vert.
layout(push_constant) uniform GrassPush {
    uint lod;
    uint instanceBase;
    uint reserved0;
    uint reserved1;
} pc;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec4 inInfo;
layout(location = 2) in vec3 inPosRel;
layout(location = 3) flat in vec4 inGround; // rgb = terrain colour, a = ground normal . sun.
layout(location = 4) flat in float inSunVis;

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

    float t = inInfo.x;
    float rnd = inInfo.y;
    float tint = params.colorTip.w;
    vec3 albedo = mix(params.colorRoot.rgb, params.colorTip.rgb, smoothstep(0.0, 1.0, t));
    albedo *= 1.0 + tint * (rnd * 2.0 - 1.0) * 0.6;
    albedo = mix(albedo, albedo * vec3(1.35, 1.1, 0.55), tint * smoothstep(0.6, 1.0, rnd));
    // Roots take the terrain colour; tips keep more of the blade colour.
    float distanceM = length(inPosRel);
    float farBlend = params.extra.z * smoothstep(0.2 * params.lodDist.z, 0.9 * params.lodDist.z, distanceM);
    albedo = mix(albedo, inGround.rgb, max(params.look.x * mix(0.9, 0.4, t), farBlend));

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

    float ao = mix(params.colorRoot.w, 1.0, smoothstep(0.0, 0.45, t));
    vec3 sunDir = normalize(params.sunDir.xyz);
    float sunUp = smoothstep(-0.05, 0.1, sunDir.y);
    float groundLit = mix(1.0, smoothstep(-0.1, 0.3, inGround.a), params.look.z);
    float sunVis = clamp(inSunVis, 0.0, 1.0);
    vec3 sun = params.sunRadiance.rgb * (kDirectScale * params.sunRadiance.w * sunUp * groundLit * sunVis);

    float wrap = clamp((dot(normal, sunDir) + 0.35) / 1.35, 0.0, 1.0);
    float hemi = normal.y * 0.5 + 0.5;
    vec3 skyFill = params.sunRadiance.rgb
        * (0.3 * kDirectScale * params.sunRadiance.w * sunUp * max(sunDir.y, 0.0) * params.skyAmbient.w * (1.0 - sunVis) * hemi);
    vec3 ambient = mix(params.groundAmbient.rgb, params.skyAmbient.rgb, hemi) + skyFill;
    vec3 color = albedo * (ambient * ao + sun * wrap * sqrt(ao));

    // Fake subsurface: light through the blade when the sun is behind it, stronger toward the thin tip.
    float through = pow(clamp(dot(viewDir, -sunDir), 0.0, 1.0), 3.0);
    color += albedo * vec3(1.1, 1.2, 0.5) * sun * (through * params.sunDir.w * mix(0.5, 1.0, t) * ao);

    outColor = vec4(color, 1.0);
}
