// Shared lambert + directional-occlusion term for every lit material shader.
// PCF/compare math mirrors the shadow feature's own receiver-mask math
// exactly - see Features/Shadow/ShadowTypes.h for the uniform block schema.
#ifndef DIRECTIONAL_LIGHTING_AND_RECEIVER_MASK_GLSL
#define DIRECTIONAL_LIGHTING_AND_RECEIVER_MASK_GLSL

// Must match SceneLightingUniformData (Features/Shadow/ShadowTypes.h)
// byte-for-byte - uploaded to Scene Services binding 8. Opaque to the
// engine layer; only a feature that owns this schema interprets it.
layout(set = 1, binding = 8, std140) uniform SceneGlobalUniformBlock {
    mat4 lightViewProjection;
    vec4 sunDirectionAndBias;         // xyz = toward sun, w = depth bias
    vec4 sunIlluminanceAndStrength;   // xyz = sun color*intensity, w = receiver-mask strength
    vec4 occlusionTexelSizeAndPad;    // x = 1 / receiver-mask resolution
} uSceneLighting;

vec3 ApplyDirectionalLightAndReceiverMask(vec3 baseColor, vec3 worldNormal, vec3 worldPos, sampler2D occlusionMap)
{
    vec3 normal = normalize(worldNormal);
    float ndotl = max(dot(normal, uSceneLighting.sunDirectionAndBias.xyz), 0.0);

    const float ambient = 0.35; // Same floor every shader used before - lighting-SOURCE fix only.
    vec3 shaded = baseColor * (ambient + ndotl * (1.0 - ambient)) * uSceneLighting.sunIlluminanceAndStrength.xyz;

    vec4 receiverClip = uSceneLighting.lightViewProjection * vec4(worldPos, 1.0);
    vec3 receiverNdc = receiverClip.xyz / receiverClip.w; // z already in [0,1] - this engine's ZO depth convention.
    vec2 receiverUv = receiverNdc.xy * 0.5 + 0.5;

    const float bias = uSceneLighting.sunDirectionAndBias.w;
    const float texelSize = uSceneLighting.occlusionTexelSizeAndPad.x;

    float occlusionFactor = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float storedDepth = texture(occlusionMap, receiverUv + vec2(x, y) * texelSize).r;
            occlusionFactor += (receiverNdc.z - bias > storedDepth) ? 1.0 : 0.0;
        }
    }
    occlusionFactor /= 9.0;

    const float strength = uSceneLighting.sunIlluminanceAndStrength.w;
    return shaded * (1.0 - strength * occlusionFactor);
}

#endif // DIRECTIONAL_LIGHTING_AND_RECEIVER_MASK_GLSL
