#version 450

// task_manager/mrt-1 campaign, PHASE4 - fragment shader for the
// "GBufferValidation" debug pass (src/Editor/GBufferValidation.h/.cpp).
// Writes TWO independent, deliberately visually DISTINCT color outputs -
// proving the campaign's own new N-target MRT mechanism (PHASE1-3) really
// works end-to-end - purely from the incoming full-screen UV varying, with
// NO real scene/material data sampled at all (a deliberate simplification -
// see PHASE4_GBUFFER_VALIDATION_PASS_AND_SHADER.md's own Step 2, "Vertex
// geometry"):
//
//   outAlbedo (location = 0) - a plain checkerboard pattern.
//   outNormal (location = 1) - a synthetic "fake normal" derived from a
//                               radial falloff around the UV center,
//                               packed into [0,1] via the standard
//                               normal-buffer encoding convention
//                               (n * 0.5 + 0.5).
//
// A human/LLM looking at both via GET /get_texture should immediately see
// two clearly different images, never a copy of one another.

layout(location = 0) in vec2 inUv;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;

void main()
{
    // Checkerboard, 8x8 cells across the full [0,1] UV range.
    vec2 cell = floor(inUv * 8.0);
    float checker = mod(cell.x + cell.y, 2.0);
    vec3 albedo = mix(vec3(0.85, 0.15, 0.15), vec3(0.95, 0.85, 0.2), checker);
    outAlbedo = vec4(albedo, 1.0);

    // A fake per-pixel "normal": a hemisphere-like radial falloff around
    // the UV center, packed [-1,1] -> [0,1] the standard way.
    vec2 centered = inUv * 2.0 - 1.0;
    float r = clamp(length(centered), 0.0, 1.0);
    vec3 fakeNormal = normalize(vec3(centered, 1.0 - r));
    outNormal = vec4(fakeNormal * 0.5 + 0.5, 1.0);
}
