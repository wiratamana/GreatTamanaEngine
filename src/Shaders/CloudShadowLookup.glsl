// Reads the cloud shadow map (see Game/Lighting/CloudShadowMap.h): the share of the sun that the cloud layer lets
// through at a world position. Shared by every shader that lights something with the sun.
#ifndef CLOUD_SHADOW_LOOKUP_GLSL
#define CLOUD_SHADOW_LOOKUP_GLSL

// `grid`: xy map centre x, z (m), z 1 / map width (m), w strength (0 = no map).
// `layer`: x layer base (m), y layer top (m), w map size in texels.
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
    vec2 uv = (position.xz + sun.xz * along - grid.xy) * grid.z + 0.5;
    float edge = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
    float fade = smoothstep(0.0, 0.1, edge);
    if (fade <= 0.0) { return 1.0; }
    // Cubic B-spline by hand (4 x 4 texels): a smooth shadow edge, and it does not depend on the filter of the image.
    float size = layer.w;
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
        float row = 0.0;
        for (int i = 0; i < 4; ++i) {
            row += wx[i] * texelFetch(shadowMap, clamp(i0 + ivec2(i - 1, j - 1), ivec2(0), hi), 0).r;
        }
        transmittance += wy[j] * row;
    }
    return mix(1.0, clamp(transmittance, 0.0, 1.0), strength * fade * (1.0 - inside));
}

// Same lookup with a bilinear read (4 fetches) for shaders that call it many times per pixel, such as air marches.
// `layer`.z is a sharpness exponent (values below 1 count as 1): the transmittance is raised to it.
float CloudSunTransmittanceFast(sampler2D shadowMap, vec4 grid, vec4 layer, vec3 sun, vec3 position)
{
    float strength = grid.w;
    if (strength <= 0.0 || sun.y <= 0.02) { return 1.0; }
    float inside = clamp((position.y - layer.x) / max(layer.y - layer.x, 1.0), 0.0, 1.0);
    if (inside >= 1.0) { return 1.0; }
    float along = (0.5 * (layer.x + layer.y) - position.y) / sun.y;
    vec2 uv = (position.xz + sun.xz * along - grid.xy) * grid.z + 0.5;
    float edge = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
    float fade = smoothstep(0.0, 0.1, edge);
    if (fade <= 0.0) { return 1.0; }
    float size = layer.w;
    vec2 p = uv * size - 0.5;
    ivec2 i0 = ivec2(floor(p));
    vec2 f = p - vec2(i0);
    ivec2 hi = ivec2(size) - 1;
    float a = texelFetch(shadowMap, clamp(i0, ivec2(0), hi), 0).r;
    float b = texelFetch(shadowMap, clamp(i0 + ivec2(1, 0), ivec2(0), hi), 0).r;
    float c = texelFetch(shadowMap, clamp(i0 + ivec2(0, 1), ivec2(0), hi), 0).r;
    float d = texelFetch(shadowMap, clamp(i0 + ivec2(1, 1), ivec2(0), hi), 0).r;
    float transmittance = mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
    transmittance = pow(clamp(transmittance, 0.0, 1.0), max(layer.z, 1.0));
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
