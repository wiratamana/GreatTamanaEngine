// Reads the cloud shadow map (see Game/Lighting/CloudShadowMap.h): the share of the sun that the cloud layer lets
// through at a world position. Shared by every shader that lights something with the sun.
#ifndef CLOUD_SHADOW_LOOKUP_GLSL
#define CLOUD_SHADOW_LOOKUP_GLSL

const float kCloudShadowNearScale = 8.0;
const float kCloudShadowFarFade = 0.1;
const float kCloudShadowNearFade = 0.15;

// Cubic B-spline (4 x 4 texels) over one square region starting at `rowOffset`.
float CloudShadowRegionCubic(sampler2D shadowMap, vec2 uv, int rowOffset, float size)
{
    vec2 p = uv * size - 0.5;
    ivec2 i0 = ivec2(floor(p));
    vec2 f = p - vec2(i0);
    vec2 f2 = f * f;
    vec2 f3 = f2 * f;
    vec4 wx = vec4(1.0 - 3.0 * f.x + 3.0 * f2.x - f3.x, 4.0 - 6.0 * f2.x + 3.0 * f3.x, 1.0 + 3.0 * f.x + 3.0 * f2.x - 3.0 * f3.x, f3.x) / 6.0;
    vec4 wy = vec4(1.0 - 3.0 * f.y + 3.0 * f2.y - f3.y, 4.0 - 6.0 * f2.y + 3.0 * f3.y, 1.0 + 3.0 * f.y + 3.0 * f2.y - 3.0 * f3.y, f3.y) / 6.0;
    ivec2 hi = ivec2(size) - 1;
    float transmittance = 0.0;
    for (int j = 0; j < 4; ++j) {
        int cy = clamp(i0.y + j - 1, 0, hi.y) + rowOffset;
        float row = 0.0;
        for (int i = 0; i < 4; ++i) {
            int cx = clamp(i0.x + i - 1, 0, hi.x);
            row += wx[i] * texelFetch(shadowMap, ivec2(cx, cy), 0).r;
        }
        transmittance += wy[j] * row;
    }
    return clamp(transmittance, 0.0, 1.0);
}

// Bilinear read (4 fetches) over one square region starting at `rowOffset`.
float CloudShadowRegionBilinear(sampler2D shadowMap, vec2 uv, int rowOffset, float size)
{
    vec2 p = uv * size - 0.5;
    ivec2 i0 = ivec2(floor(p));
    vec2 f = p - vec2(i0);
    ivec2 hi = ivec2(size) - 1;
    ivec2 c00 = clamp(i0, ivec2(0), hi);
    ivec2 c10 = clamp(i0 + ivec2(1, 0), ivec2(0), hi);
    ivec2 c01 = clamp(i0 + ivec2(0, 1), ivec2(0), hi);
    ivec2 c11 = clamp(i0 + ivec2(1, 1), ivec2(0), hi);
    float a = texelFetch(shadowMap, ivec2(c00.x, c00.y + rowOffset), 0).r;
    float b = texelFetch(shadowMap, ivec2(c10.x, c10.y + rowOffset), 0).r;
    float c = texelFetch(shadowMap, ivec2(c01.x, c01.y + rowOffset), 0).r;
    float d = texelFetch(shadowMap, ivec2(c11.x, c11.y + rowOffset), 0).r;
    return clamp(mix(mix(a, b, f.x), mix(c, d, f.x), f.y), 0.0, 1.0);
}

// `grid`: xy map centre x, z (m), z 1 / far map width (m), w strength (0 = no map).
// `layer`: x layer base (m), y layer top (m), w map side in texels (far rows 0..w-1, near rows w..2w-1).
// `sun`: unit vector toward the sun. 1 = fully lit.
float CloudSunTransmittance(sampler2D shadowMap, vec4 grid, vec4 layer, vec3 sun, vec3 position)
{
    float strength = grid.w;
    if (strength <= 0.0 || sun.y <= 0.02) { return 1.0; }
    // A point inside the layer is shaded by the part above it only.
    float inside = clamp((position.y - layer.x) / max(layer.y - layer.x, 1.0), 0.0, 1.0);
    if (inside >= 1.0) { return 1.0; }
    // The map is read where the sun ray of the point crosses the middle of the layer.
    float along = (0.5 * (layer.x + layer.y) - position.y) / sun.y;
    vec2 delta = position.xz + sun.xz * along - grid.xy;
    vec2 uv = delta * grid.z + 0.5;
    float edge = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
    float fade = smoothstep(0.0, kCloudShadowFarFade, edge);
    if (fade <= 0.0) { return 1.0; }
    float size = layer.w;
    int nearRow = int(size);
    vec2 nuv = delta * (grid.z * kCloudShadowNearScale) + 0.5;
    float nedge = min(min(nuv.x, 1.0 - nuv.x), min(nuv.y, 1.0 - nuv.y));
    float nfade = (nearRow > 0 && textureSize(shadowMap, 0).y >= 2 * nearRow)
        ? smoothstep(0.0, kCloudShadowNearFade, nedge)
        : 0.0;
    float transmittance = (nfade >= 1.0)
        ? CloudShadowRegionCubic(shadowMap, nuv, nearRow, size)
        : CloudShadowRegionCubic(shadowMap, uv, 0, size);
    if (nfade > 0.0 && nfade < 1.0) {
        transmittance = mix(transmittance, CloudShadowRegionCubic(shadowMap, nuv, nearRow, size), nfade);
    }
    return mix(1.0, transmittance, strength * fade * (1.0 - inside));
}

// Bilinear lookup for air marches and diffuse fill; `layer.z` is a sharpness exponent (< 1 counts as 1).
float CloudSunTransmittanceFast(sampler2D shadowMap, vec4 grid, vec4 layer, vec3 sun, vec3 position)
{
    float strength = grid.w;
    if (strength <= 0.0 || sun.y <= 0.02) { return 1.0; }
    float inside = clamp((position.y - layer.x) / max(layer.y - layer.x, 1.0), 0.0, 1.0);
    if (inside >= 1.0) { return 1.0; }
    float along = (0.5 * (layer.x + layer.y) - position.y) / sun.y;
    vec2 delta = position.xz + sun.xz * along - grid.xy;
    vec2 uv = delta * grid.z + 0.5;
    float edge = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
    float fade = smoothstep(0.0, kCloudShadowFarFade, edge);
    if (fade <= 0.0) { return 1.0; }
    float size = layer.w;
    int nearRow = int(size);
    vec2 nuv = delta * (grid.z * kCloudShadowNearScale) + 0.5;
    float nedge = min(min(nuv.x, 1.0 - nuv.x), min(nuv.y, 1.0 - nuv.y));
    float nfade = (nearRow > 0 && textureSize(shadowMap, 0).y >= 2 * nearRow)
        ? smoothstep(0.0, kCloudShadowNearFade, nedge)
        : 0.0;
    float transmittance = (nfade >= 1.0)
        ? CloudShadowRegionBilinear(shadowMap, nuv, nearRow, size)
        : CloudShadowRegionBilinear(shadowMap, uv, 0, size);
    if (nfade > 0.0 && nfade < 1.0) {
        transmittance = mix(transmittance, CloudShadowRegionBilinear(shadowMap, nuv, nearRow, size), nfade);
    }
    transmittance = pow(transmittance, max(layer.z, 1.0));
    return mix(1.0, transmittance, strength * fade * (1.0 - inside));
}

// Sun visibility of AIR (light shafts): the shadow only counts in the low air under the layer, and it never drops
// below a floor, so the sky between the clouds keeps its light.
const float kAirShadowFloor = 0.5;
float CloudAirSunVisibility(sampler2D shadowMap, vec4 grid, vec4 layer, vec3 sun, vec3 position)
{
    float shadow = CloudSunTransmittanceFast(shadowMap, grid, layer, sun, position);
    float low = 1.0 - smoothstep(0.4 * layer.x, 1.2 * layer.x, position.y);
    return mix(1.0, max(shadow, kAirShadowFloor), low);
}

// Sun visibility of the beam haze: the same low-air rule as above, but the shadow may go much darker, so the light
// curtains and the shadowed air between them contrast. x = visibility of air, y = visibility of the beam haze.
const float kHazeShadowFloor = 0.06;
vec2 CloudAirHazeSunVisibility(sampler2D shadowMap, vec4 grid, vec4 layer, vec3 sun, vec3 position)
{
    float shadow = CloudSunTransmittanceFast(shadowMap, grid, layer, sun, position);
    float low = 1.0 - smoothstep(0.4 * layer.x, 1.2 * layer.x, position.y);
    return mix(vec2(1.0), vec2(max(shadow, kAirShadowFloor), max(shadow, kHazeShadowFloor)), low);
}

#endif // CLOUD_SHADOW_LOOKUP_GLSL
