// Shared Lambert term, hemisphere ambient and sun cascade shadows for every lit material shader.
// Define DIRECTIONAL_LIGHTING_NO_SHADOWS before including to drop the cascade samplers.
#ifndef DIRECTIONAL_LIGHTING_GLSL
#define DIRECTIONAL_LIGHTING_GLSL

// Scene-services binding 8; must match SceneLightingUniformData (448 bytes).
layout(set = 1, binding = 8, std140) uniform SceneGlobalUniformBlock {
    vec4 sunDirectionAndValid;         // xyz = toward sun, w = 1 when written
    vec4 sunIlluminance;               // xyz = sun color * intensity, w = padding
    vec4 skyAmbientAndIntensity;       // rgb = sky ambient, w = ambient intensity
    vec4 groundAmbientAndPad;          // rgb = ground ambient, w = padding
    vec4 shadowParams;                 // x cascade count (0 = none), y strength, z 1 / map size, w filter radius (texels)
    vec4 shadowBias;                   // x const bias, y normal offset, z slope bias (texels), w filter samples
    vec4 cameraPositionAndFadeStart;   // xyz eye, w fade start
    vec4 cameraForwardAndFadeEnd;      // xyz forward, w fade end
    vec4 cascadeSplitFar;              // view depth where cascade i ends
    vec4 cascadeBlendWidth;            // blend band at the end of cascade i
    vec4 cascadeTexelWorld;            // world size of one texel
    vec4 cascadeDepthRange;            // ortho depth span, world units
    mat4 cascadeViewProjection0;       // world to cascade clip
    mat4 cascadeViewProjection1;
    mat4 cascadeViewProjection2;
    mat4 cascadeViewProjection3;
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

#ifdef DIRECTIONAL_LIGHTING_NO_SHADOWS

float SunShadowAmount(vec3 position, vec3 normal, float nDotL)
{
    return 0.0;
}

#else

// Cascade depth maps: scene slots 0..3.
layout(set = 1, binding = 0) uniform sampler2D uShadowCascade0;
layout(set = 1, binding = 1) uniform sampler2D uShadowCascade1;
layout(set = 1, binding = 2) uniform sampler2D uShadowCascade2;
layout(set = 1, binding = 3) uniform sampler2D uShadowCascade3;

float ShadowDepthAt(int k, vec2 uv)
{
    if (k == 0) { return textureLod(uShadowCascade0, uv, 0.0).r; }
    if (k == 1) { return textureLod(uShadowCascade1, uv, 0.0).r; }
    if (k == 2) { return textureLod(uShadowCascade2, uv, 0.0).r; }
    return textureLod(uShadowCascade3, uv, 0.0).r;
}

mat4 CascadeMatrix(int k)
{
    if (k == 0) { return uSceneLighting.cascadeViewProjection0; }
    if (k == 1) { return uSceneLighting.cascadeViewProjection1; }
    if (k == 2) { return uSceneLighting.cascadeViewProjection2; }
    return uSceneLighting.cascadeViewProjection3;
}

// Shadowed fraction 0..1, or -1 when the point lies outside cascade k.
// Same algorithm as SampleCascade() in the sun shadow mask pass.
float SampleCascadeShadow(int k, vec3 position, vec3 normal, float nDotL, float rotation)
{
    float texel = uSceneLighting.cascadeTexelWorld[k];
    float range = uSceneLighting.cascadeDepthRange[k];
    float tanTheta = clamp(sqrt(max(1.0 - nDotL * nDotL, 0.0)) / max(nDotL, 0.05), 0.0, 10.0);

    vec3 offsetPosition = position + normal * (uSceneLighting.shadowBias.y * texel);
    vec4 clip = CascadeMatrix(k) * vec4(offsetPosition, 1.0);
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || ndc.z < 0.0 || ndc.z > 1.0) {
        return -1.0;
    }

    float biasNdc = texel * (uSceneLighting.shadowBias.x + uSceneLighting.shadowBias.z * tanTheta) / range;
    int samples = max(int(uSceneLighting.shadowBias.w + 0.5), 1);
    float radiusUv = uSceneLighting.shadowParams.w * uSceneLighting.shadowParams.z;

    float occluded = 0.0;
    for (int i = 0; i < samples; ++i) {
        vec2 offset = vec2(0.0);
        if (samples > 1) {
            float r = sqrt((float(i) + 0.5) / float(samples));
            float theta = float(i) * 2.39996323 + rotation;
            offset = r * vec2(cos(theta), sin(theta)) * radiusUv;
        }
        occluded += (ndc.z - biasNdc > ShadowDepthAt(k, uv + offset)) ? 1.0 : 0.0;
    }
    return occluded / float(samples);
}

// 0 = fully lit, 1 = fully shadowed. Cascade pick, blend and distance fade as in the mask pass.
float SunShadowAmount(vec3 position, vec3 normal, float nDotL)
{
    int count = int(uSceneLighting.shadowParams.x + 0.5);
    if (count <= 0) { return 0.0; }

    vec3 eye = uSceneLighting.cameraPositionAndFadeStart.xyz;
    float fadeStart = uSceneLighting.cameraPositionAndFadeStart.w;
    float fadeEnd = uSceneLighting.cameraForwardAndFadeEnd.w;
    float viewDepth = dot(position - eye, uSceneLighting.cameraForwardAndFadeEnd.xyz);
    if (viewDepth >= fadeEnd) { return 0.0; }

    int c = count - 1;
    for (int i = 0; i < count; ++i) {
        if (viewDepth < uSceneLighting.cascadeSplitFar[i]) { c = i; break; }
    }

    float lit = max(nDotL, 0.05);
    float rotation = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float shadow = SampleCascadeShadow(c, position, normal, lit, rotation);
    if (shadow < 0.0) {
        shadow = (c + 1 < count) ? max(SampleCascadeShadow(c + 1, position, normal, lit, rotation), 0.0) : 0.0;
    }
    if (c + 1 < count) {
        float width = uSceneLighting.cascadeBlendWidth[c];
        float start = uSceneLighting.cascadeSplitFar[c] - width;
        if (width > 0.0 && viewDepth > start) {
            float next = SampleCascadeShadow(c + 1, position, normal, lit, rotation);
            if (next >= 0.0) {
                shadow = mix(shadow, next, clamp((viewDepth - start) / width, 0.0, 1.0));
            }
        }
    }
    return shadow * (1.0 - smoothstep(fadeStart, fadeEnd, viewDepth));
}

#endif // DIRECTIONAL_LIGHTING_NO_SHADOWS

// Linear scene radiance of a surface lit by the sun and the hemisphere ambient.
// Shadows remove only the sun term.
vec3 ShadeDirectionalLinear(vec3 baseColorLinear, vec3 worldNormal, bool useShadow, vec3 worldPosition)
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

    float shadow = (useShadow && written && ndotl > 0.0) ? SunShadowAmount(worldPosition, n, ndotl) : 0.0;
    float sunVisibility = 1.0 - uSceneLighting.shadowParams.y * shadow;

    // Ambient does not depend on the sun, so a dark sun still leaves a lit scene.
    return baseColorLinear * (ambient + sunIlluminance * (ndotl * kDirectScale * sunVisibility));
}

// Scene shaders output linear HDR: no exposure, no curve, no sRGB encode. No shadows.
vec3 ApplyDirectionalLight(vec3 baseColorLinear, vec3 worldNormal)
{
    return ShadeDirectionalLinear(baseColorLinear, worldNormal, false, vec3(0.0));
}

// Same as ApplyDirectionalLight, with sun cascade shadows at worldPosition.
vec3 ApplyDirectionalLightShadowed(vec3 baseColorLinear, vec3 worldNormal, vec3 worldPosition)
{
    return ShadeDirectionalLinear(baseColorLinear, worldNormal, true, worldPosition);
}

// For previews that draw straight into an 8-bit target: fixed exposure and sRGB encode.
vec3 ApplyDirectionalLightForDisplay(vec3 baseColorLinear, vec3 worldNormal)
{
    return LinearToSrgb(ShadeDirectionalLinear(baseColorLinear, worldNormal, false, vec3(0.0)) * kPreviewExposure);
}

#endif // DIRECTIONAL_LIGHTING_GLSL
