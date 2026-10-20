// Shared Lambert term plus hemisphere ambient for every lit material shader.
#ifndef DIRECTIONAL_LIGHTING_GLSL
#define DIRECTIONAL_LIGHTING_GLSL

// Scene-services binding 8; must match SceneLightingUniformData (64 bytes).
layout(set = 1, binding = 8, std140) uniform SceneGlobalUniformBlock {
    vec4 sunDirectionAndValid;         // xyz = toward sun, w = 1 when written
    vec4 sunIlluminance;               // xyz = sun color * intensity, w = padding
    vec4 skyAmbientAndIntensity;       // rgb = sky ambient, w = ambient intensity
    vec4 groundAmbientAndPad;          // rgb = ground ambient, w = padding
} uSceneLighting;

// Used while the block has never been written (the block is all zeros then).
const vec3 kDefaultSunDirection = vec3(0.70710678, 0.70710678, 0.0);
const vec3 kDefaultSunIlluminance = vec3(3.0, 2.85, 2.55);
const vec3 kDefaultAmbient = vec3(1.05, 1.0, 0.89);
const float kDirectScale = 0.65;
const float kPreviewExposure = 0.4;   // Used only by ApplyDirectionalLightForDisplay().

// Exact piecewise sRGB transfer functions (the same curve the GPU uses for *_SRGB formats).
vec3 SrgbToLinear(vec3 c)
{
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

vec3 LinearToSrgb(vec3 c)
{
    c = clamp(c, vec3(0.0), vec3(1.0));
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

// Linear scene radiance of a surface lit by the sun and the hemisphere ambient.
vec3 ShadeDirectionalLinear(vec3 baseColorLinear, vec3 worldNormal)
{
    bool written = uSceneLighting.sunDirectionAndValid.w >= 0.5;
    vec3 sunDirection = written ? uSceneLighting.sunDirectionAndValid.xyz : kDefaultSunDirection;
    vec3 sunIlluminance = written ? uSceneLighting.sunIlluminance.xyz : kDefaultSunIlluminance;

    vec3 n = normalize(worldNormal);
    float ndotl = max(dot(n, sunDirection), 0.0);

    // Y is up: faces looking up get sky light, faces looking down get ground bounce.
    vec3 skyAmbient = uSceneLighting.skyAmbientAndIntensity.rgb * uSceneLighting.skyAmbientAndIntensity.w;
    vec3 groundAmbient = uSceneLighting.groundAmbientAndPad.rgb * uSceneLighting.skyAmbientAndIntensity.w;
    vec3 ambient = written ? mix(groundAmbient, skyAmbient, n.y * 0.5 + 0.5) : kDefaultAmbient;

    // Ambient does not depend on the sun, so a dark sun still leaves a lit scene.
    return baseColorLinear * (ambient + sunIlluminance * (ndotl * kDirectScale));
}

// Scene shaders output this: linear HDR, no exposure, no curve, no sRGB encode.
vec3 ApplyDirectionalLight(vec3 baseColorLinear, vec3 worldNormal)
{
    return ShadeDirectionalLinear(baseColorLinear, worldNormal);
}

// For previews that draw straight into an 8-bit target: fixed exposure and sRGB encode.
vec3 ApplyDirectionalLightForDisplay(vec3 baseColorLinear, vec3 worldNormal)
{
    return LinearToSrgb(ShadeDirectionalLinear(baseColorLinear, worldNormal) * kPreviewExposure);
}

#endif // DIRECTIONAL_LIGHTING_GLSL
