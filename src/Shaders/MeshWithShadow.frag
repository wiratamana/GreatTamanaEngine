#version 450

// Block 4 "Global Scene Services Descriptor Set"
// (task_manager/better-render-pass-6/PHASE0_MASTER_STRATEGY.md), PHASE8
// (PHASE8_EXAMPLE_SHADER_AND_FULL_VERIFICATION.md) - the proof-of-contract
// shader for this whole campaign: a Pipeline built from this shader (plus
// Mesh.vert, reused unchanged) and Core::GetSceneServicesDescriptorSet().
// Layout() (passed as Renderer::CreatePipeline()'s PHASE4 trailing
// sceneServicesSetLayout parameter) gets a real, working `set = 1, binding =
// 0` descriptor with ZERO further Pipeline.h/Renderer.h change needed for
// THIS shader specifically - every bit of plumbing it relies on already
// shipped in PHASE1-7. Based directly on Mesh.frag - same lambert term, same
// flat "clay" base color - plus exactly one addition: sampling the reserved
// scene-services shadow slot and multiplying it into the final color.

layout(location = 0) in vec3 inWorldNormal;

layout(location = 0) out vec4 outColor;

// The ONE reserved "scene services" descriptor set (SceneServicesDescriptorSet.h) -
// binding 0 is SceneServiceSlot::ShadowMap. Whatever this resolves to THIS
// frame/view - a real published texture (Core.cpp's
// "SceneServicesExampleShadowFeature" provider, PHASE8) or this class's own
// dummy opaque-white fallback when nothing published - is sampled directly
// here.
layout(set = 1, binding = 0) uniform sampler2D u_SceneShadowMap;

void main()
{
    // Fixed world-space light direction (pointing FROM the surface TOWARD
    // the light) - identical to Mesh.frag's own lighting; see that file's own
    // comment for why this engine has no real light/material system yet.
    const vec3 lightDir = normalize(vec3(0.45, 0.8, -0.4));
    const vec3 baseColor = vec3(0.72, 0.73, 0.76); // Neutral light grey - matches Mesh.frag's own "clay" look.

    vec3 normal = normalize(inWorldNormal);
    float diffuse = max(dot(normal, lightDir), 0.0);

    const float ambient = 0.35;
    vec3 shaded = baseColor * (ambient + diffuse * (1.0 - ambient));

    // Fixed UV - this proof-of-contract shader exists to show `set = 1`
    // sampling works at all, not to implement real shadow-map projection
    // (Block 4 ships no real shadow feature - see PHASE7's own doc comment).
    // The dummy fallback is opaque white (1.0, i.e. "no darkening"); a real
    // feature publishing darker data here visibly darkens the surface -
    // that visible difference is this shader's entire reason to exist.
    float shadowFactor = texture(u_SceneShadowMap, vec2(0.5, 0.5)).r;
    outColor = vec4(shaded * shadowFactor, 1.0);
}
