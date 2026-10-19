// Shared Lambert term for every lit material shader.
#ifndef DIRECTIONAL_LIGHTING_GLSL
#define DIRECTIONAL_LIGHTING_GLSL

// Scene-services binding 8; must match SceneLightingUniformData (32 bytes).
layout(set = 1, binding = 8, std140) uniform SceneGlobalUniformBlock {
    vec4 sunDirectionAndValid;  // xyz = toward sun, w = 1 when written
    vec4 sunIlluminanceAndPad;  // xyz = sun color * intensity
} uSceneLighting;

// Sun used while the block has never been written; same as a scene without a light.
const vec3 kDefaultSunDirection = vec3(0.70710678, 0.70710678, 0.0);
const vec3 kDefaultSunIlluminance = vec3(3.0, 2.85, 2.55);

vec3 ApplyDirectionalLight(vec3 baseColor, vec3 worldNormal)
{
    bool written = uSceneLighting.sunDirectionAndValid.w >= 0.5;
    vec3 sunDirection = written ? uSceneLighting.sunDirectionAndValid.xyz : kDefaultSunDirection;
    vec3 sunIlluminance = written ? uSceneLighting.sunIlluminanceAndPad.xyz : kDefaultSunIlluminance;

    float ndotl = max(dot(normalize(worldNormal), sunDirection), 0.0);
    const float ambient = 0.35;
    return baseColor * (ambient + ndotl * (1.0 - ambient)) * sunIlluminance;
}

#endif // DIRECTIONAL_LIGHTING_GLSL
