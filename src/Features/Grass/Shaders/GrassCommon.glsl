// Shared by every grass shader: params block, instance record, hash and noise.
// NOTE: the params block must match GrassParamsGpu in GrassTypes.h (656 bytes, std430 = std140 here).
#ifndef GRASS_COMMON_GLSL
#define GRASS_COMMON_GLSL

// 32 bytes, written by GrassCull.comp and read by Grass.vert. NOTE: must match GrassInstanceGpu in GrassTypes.h.
struct GrassInstance {
    vec3 root;    // Eye-relative root, metres.
    uint heightWidth; // 2 x half: height, width.
    uint frame;   // 4 x snorm8: sin(yaw), cos(yaw), bend direction x, bend direction z.
    uint ground;  // 4 x unorm8: ground normal x, z (* 0.5 + 0.5), sin(tip angle), sun visibility.
    uint flutter; // 2 x half: tip flutter along the blade side, along the bend.
    uint colorRnd; // Terrain colour r, g, b in bits 24..31, 16..23, 8..15; blade random in bits 0..7.
};

// All space is eye-relative except where a name says "grid" (grid-relative, small floats) or "world".
layout(std430, binding = 0) readonly buffer GrassParamsBlock {
    mat4 viewProjection;      // Unjittered eye-relative view-projection (culling, Hi-Z).
    mat4 drawViewProjection;  // Jittered eye-relative view-projection (drawing).
    vec4 planes[6];           // Eye-relative frustum planes, xyz = normal, w = distance; inside when dot + w >= 0.
    vec4 camRel;              // x,z = camera grid-relative xz (m), y = camera world y (m), w = chunk size (m).
    ivec4 grid;               // xy = world chunk index of grid cell (0,0), z = cells per side, w = flags.
    vec4 tile;                // xy = grid-relative xz of texel (0,0) centre, z = texel size (m), w = texels per side.
    vec4 water;               // x = sea level, y = margin above water (m), z = fade band (m), w = bare beach height above the sea (m).
    vec4 lodDist;             // x,y,z = LOD0/LOD1/LOD2 max distance (m), w = edge fade fraction of the last LOD.
    vec4 density;             // xyz = blades per m^2 per LOD, w = patchiness 0..1.
    vec4 slope;               // x = max slope (deg), y = slope fade (deg), z = patch noise scale (m), w = bare threshold.
    vec4 blade;               // x = height, y = width, z = taper 0..1, w = height variation 0..1.
    vec4 bladeB;              // x = lean (rad), y = width compensation exponent, z = max width scale, w = max bend (rad).
    vec4 wind0;               // xy = wind direction (unit xz, where air goes), z = speed (m/s), w = total time (s).
    vec4 wind1;               // x = gust strength, yz = wind pattern offset xz (m), w = wind bend strength.
    vec4 wind2;               // x = flutter speed (rad/s), y = flutter strength, z = wave length (m), w = gust length (m).
    vec4 sunDir;              // xyz = toward the sun, w = translucency strength.
    vec4 sunRadiance;         // rgb = sun illuminance (already tinted), w = sun visibility multiplier.
    vec4 skyAmbient;          // rgb = sky ambient (already scaled by intensity), w = sky diffuse fill in shadow.
    vec4 groundAmbient;       // rgb = ground ambient (already scaled by intensity), w = shadow caster frustum pad (m).
    vec4 colorRoot;           // rgb = root albedo (linear), w = root AO 0..1.
    vec4 colorTip;            // rgb = tip albedo (linear), w = per-blade tint variation 0..1.
    vec4 cameraWorld;         // xy = camera world xz (m), z = alpine line: grass thins out above this world Y, w = its fade band (m).
    uvec4 instanceBase;       // xyz = first instance index of each LOD region.
    uvec4 instanceCap;        // xyz = capacity of each LOD region.
    uvec4 misc;               // x = debug mode (0 normal, 1 LOD, 2 sway, 3 chunks, 4 sun vis), y = placement seed.
    vec4 hiz;                 // xy = atlas size, zw = level 0 size (same as ClusterCull).
    vec4 depthMap;            // x = k, y = b of d = k + b / w, z = pyramid level count.
    vec4 look;                // x = terrain tint strength, y = ground normal blend, z = self shadow strength, w = LOD0 segments.
    vec4 extra;               // x = density blend share, y = far height fade share, z = far terrain colour blend, w = groups per chunk.
    vec4 densityLog;          // xyz = ln of the three densities, w = 1 when the LOD0 density is above 0.
} params;

const uint kGrassFlagHiz = 1u;           // Hi-Z pyramid is valid and bound.
const uint kGrassFlagStats = 4u;         // Count rejections into the stats words.

const uint kGrassLodCount = 3u;
const uint kGrassCommandCount = 6u;
const uint kGrassStatsWords = 32u;

// Stats word indices (GrassStatsWord in GrassTypes.h).
const uint kStatChunksInGrid = 0u;
const uint kStatChunksCulledDistance = 1u;
const uint kStatChunksCulledNoData = 2u;
const uint kStatChunksCulledWater = 3u;
const uint kStatChunksCulledFrustum = 4u;
const uint kStatChunksCulledHiz = 5u;
const uint kStatChunksLod0 = 6u;
const uint kStatChunksLod1 = 7u;
const uint kStatChunksLod2 = 8u;
const uint kStatCandidates = 9u;
const uint kStatAccepted0 = 10u;
const uint kStatAccepted1 = 11u;
const uint kStatAccepted2 = 12u;
const uint kStatRejectNoData = 13u;
const uint kStatRejectLod = 14u;
const uint kStatRejectWater = 15u;
const uint kStatRejectSlope = 16u;
const uint kStatRejectNoise = 17u;
const uint kStatDropped0 = 18u;
const uint kStatDropped1 = 19u;
const uint kStatDropped2 = 20u;
const uint kStatSlot0 = 21u; // Slot reservation counters: required, not optional.
const uint kStatSlot1 = 22u;
const uint kStatSlot2 = 23u;

// Blade triangle lists: LOD0 look.w quads (6 vertices each), LOD1 two quads, LOD2 one triangle.
const uint kGrassVertsList1 = 12u;
const uint kGrassVertsList2 = 3u;

uint GrassPcg(uint v)
{
    uint s = v * 747796405u + 2891336453u;
    uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}

uint GrassHash2(uint a, uint b)
{
    return GrassPcg(a ^ GrassPcg(b + 0x9E3779B9u));
}

uint GrassHash3(uint a, uint b, uint c)
{
    return GrassPcg(a ^ GrassPcg(b ^ GrassPcg(c)));
}

float GrassUnit(uint h)
{
    return float(h >> 8u) * (1.0 / 16777216.0);
}

float GrassCellHash(ivec2 cell)
{
    return GrassUnit(GrassHash2(uint(cell.x), uint(cell.y)));
}

// Smooth value noise in 0..1, one lattice cell per unit of x.
float GrassValueNoise(vec2 x)
{
    vec2 i = floor(x);
    vec2 f = x - i;
    vec2 u = f * f * (3.0 - 2.0 * f);
    ivec2 c = ivec2(i);
    float a = GrassCellHash(c);
    float b = GrassCellHash(c + ivec2(1, 0));
    float d = GrassCellHash(c + ivec2(0, 1));
    float e = GrassCellHash(c + ivec2(1, 1));
    return mix(mix(a, b, u.x), mix(d, e, u.x), u.y);
}

// Blades per m^2 at a distance (as a natural log): densityLod0 up close, falling (geometrically, smoothly) to densityLod1
// at the LOD0 distance and to densityLod2 at the LOD1 distance. A blade is present when its index is below the candidate count.
float GrassLogDensityAt(float dist)
{
    float blend = params.extra.x;
    float e0 = params.lodDist.x;
    float e1 = params.lodDist.y;
    float s0 = e0 * (1.0 - blend);
    float s1 = max(e0, e1 * (1.0 - blend));
    float t0 = smoothstep(s0, e0, dist);
    float t1 = smoothstep(s1, e1, dist);
    return mix(mix(params.densityLog.x, params.densityLog.y, t0), params.densityLog.z, t1);
}

float GrassDensityAt(float dist)
{
    return params.densityLog.w > 0.5 ? exp(GrassLogDensityAt(dist)) : 0.0;
}

#endif
