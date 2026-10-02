#version 450

// Proof-of-contract shader for a feature sampling the reserved "scene
// services" descriptor set (set = 1) - based directly on Mesh.frag (same
// lambert term, same flat "clay" base color) plus exactly one addition:
// sampling this feature's own registered scene-service slot and multiplying
// it into the final color.

layout(location = 0) in vec3 inWorldNormal;

layout(location = 0) out vec4 outColor;

// The one reserved "scene services" descriptor set
// (SceneServicesDescriptorSet.h) - binding 0 is the scene-service slot this
// feature was assigned, currently index 0 (see the owning feature's own
// registration call in Core.cpp for the authoritative constant). Whatever
// this resolves to THIS frame/view - a real published texture or this
// class's own dummy opaque-white fallback when nothing published - is
// sampled directly here.
layout(set = 1, binding = 0) uniform sampler2D u_SceneServiceSlot0;

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
    // sampling works at all, not to implement any real per-pixel sampling.
    // The dummy fallback is opaque white (1.0, i.e. "no darkening"); a real
    // feature publishing darker data here visibly darkens the surface -
    // that visible difference is this shader's entire reason to exist.
    float sceneServiceFactor = texture(u_SceneServiceSlot0, vec2(0.5, 0.5)).r;
    outColor = vec4(shaded * sceneServiceFactor, 1.0);
}
