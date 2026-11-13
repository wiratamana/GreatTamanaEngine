#ifndef AIR_SUN_VISIBILITY_GLSL
#define AIR_SUN_VISIBILITY_GLSL

// One sun visibility for every air effect: sun cascades, then the cloud shadow map.
// Include OrthoMapSetVisibility.glsl and CloudShadowLookup.glsl first.

// The cascade shadow fades out over this share of the last cascade, so air never jumps from shadowed to lit.
const float kAirCascadeFadeShare = 0.25;

struct AirSun {
    float air;      // Sun visibility of plain air.
    float haze;     // Sun visibility of haze and mist (clouds may darken it further).
    float terrain;  // Cascade term alone, the share of sun the terrain lets through.
    float known;    // 0..1, how far the cascades cover this point (0 beyond the last one).
};

// Cascade term; `blend` scales how much of the shadow counts. `known` is 0 where no cascade reaches.
float AirCascadeVisibility(vec3 world, float rotation, float blend, out float known)
{
    known = 0.0;
    int count = clamp(int(ORTHO_COUNT), 0, 4);
    if (count == 0 || blend <= 0.0) {
        return 1.0;
    }
    float lastFar = ORTHO_SPLIT_FAR(count - 1);
    float planar = dot(world - ORTHO_EYE, ORTHO_FORWARD);
    known = (1.0 - smoothstep((1.0 - kAirCascadeFadeShare) * lastFar, lastFar, planar)) * clamp(blend, 0.0, 1.0);
    if (known <= 0.0) {
        return 1.0;
    }
    return mix(1.0, OrthoMapSetVisibility(world, rotation), known);
}

// `world` is the sample in world space, `cloudPosition` the same point relative to the eye in x, z.
AirSun AirSunVisibilityAt(sampler2D cloudMap, vec4 cloudGrid, vec4 cloudLayer, vec3 sun, vec3 world, vec3 cloudPosition,
    float rotation, float blend)
{
    AirSun result;
    result.terrain = AirCascadeVisibility(world, rotation, blend, result.known);
    result.air = result.terrain;
    result.haze = result.terrain;
    if (cloudGrid.w > 0.0) {
        vec2 cloud = CloudAirHazeSunVisibility(cloudMap, cloudGrid, cloudLayer, sun, cloudPosition);
        result.air *= cloud.x;
        result.haze *= cloud.y;
    }
    return result;
}

#endif
