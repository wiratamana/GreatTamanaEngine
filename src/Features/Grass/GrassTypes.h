#pragma once

#include "../../Core/Extent.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>

namespace gte {

// Name the feature registers under.
inline constexpr const char* kGrassFeatureName = "Grass";

// Shared constants of the grass shaders. NOTE: must match Shaders/GrassCommon.glsl.
inline constexpr std::uint32_t kGrassLodCount = 3;
inline constexpr std::uint32_t kGrassShadowLodCount = 2;     // L0 and L1 regions cast into sun cascades.
inline constexpr std::uint32_t kGrassCommandCount = 2 * kGrassLodCount; // 3 colour + 3 shadow indirect commands.
inline constexpr float kGrassChunkSizeM = 16.0f;
inline constexpr std::uint32_t kGrassMaxGridCells = 48;      // Chunks per grid side.
inline constexpr std::uint32_t kGrassCullGroupSize = 64;     // local_size_x of GrassCull.comp.
inline constexpr std::uint32_t kGrassStatsWords = 32;
inline constexpr std::uint32_t kGrassCommandStride = 16;     // Bytes between draw commands.
inline constexpr std::uint64_t kGrassCommandBytes = std::uint64_t{ kGrassCommandStride } * kGrassCommandCount;
static_assert(kGrassCommandBytes == 96);
inline constexpr std::uint32_t kGrassInstanceBytes = 32;
inline constexpr std::uint32_t kGrassMaxTileTexels = 512;     // Height tile limit per side (the GPU copy has this fixed size).
inline constexpr std::uint32_t kGrassTileStride = 2;          // Floats per tile texel: height, then submerged flag and colour as raw bits.
inline constexpr std::uint32_t kGrassMinLod0Segments = 3;
inline constexpr std::uint32_t kGrassMaxLod0Segments = 6;

// Triangle-list vertex counts of one blade: LOD0 has `lod0Segments` quads, LOD1 two quads, LOD2 one triangle.
inline constexpr std::uint32_t GrassVertexCount(std::uint32_t lod, std::uint32_t lod0Segments) noexcept
{
    return lod == 0 ? lod0Segments * 6u : (lod == 1 ? 12u : 3u);
}

// Bits of GrassParamsGpu::grid[3].
inline constexpr std::int32_t kGrassFlagHiz = 1;
inline constexpr std::int32_t kGrassFlagStats = 4;

// Word indices of the stats storage. Words kSlot0..kSlot2 are required by the cull; the rest only count with kGrassFlagStats.
enum class GrassStatsWord : std::uint32_t {
    ChunksInGrid = 0,
    ChunksCulledDistance,
    ChunksCulledNoData,
    ChunksCulledWater,
    ChunksCulledFrustum,
    ChunksCulledHiz,
    ChunksLod0,
    ChunksLod1,
    ChunksLod2,
    Candidates,
    Accepted0,
    Accepted1,
    Accepted2,
    RejectNoData,
    RejectLod,
    RejectWater,
    RejectSlope,
    RejectNoise,
    Dropped0,
    Dropped1,
    Dropped2,
    Slot0,
    Slot1,
    Slot2,
    RejectFrustum,
    Count,
};
static_assert(static_cast<std::uint32_t>(GrassStatsWord::Count) <= kGrassStatsWords);

// Raw stats readback (the storage is zeroed by GrassInit.comp each frame).
struct GrassStatsGpu {
    std::uint32_t words[kGrassStatsWords];

    std::uint32_t Get(GrassStatsWord word) const noexcept { return words[static_cast<std::uint32_t>(word)]; }
};
static_assert(sizeof(GrassStatsGpu) == 128);
static_assert(std::is_trivially_copyable_v<GrassStatsGpu>);

// One indirect draw command (4 uints). firstInstance is always 0.
struct GrassDrawCommand {
    std::uint32_t vertexCount;
    std::uint32_t instanceCount;
    std::uint32_t firstVertex;
    std::uint32_t firstInstance;
};
static_assert(sizeof(GrassDrawCommand) == kGrassCommandStride);

// One blade, written by GrassCull.comp and read by Grass.vert. All wind and bend maths is done by the cull.
struct GrassInstanceGpu {
    float root[3];            // Eye-relative root.
    std::uint32_t heightWidth; // 2 x half: height, width (m).
    std::uint32_t frame;      // 4 x snorm8: sin(yaw), cos(yaw), bend dir x, bend dir z.
    std::uint32_t ground;     // 4 x unorm8: ground normal x, z (* 0.5 + 0.5), sin(tip angle), sun visibility.
    std::uint32_t flutter;    // 2 x half: tip flutter along the blade side and along the bend.
    std::uint32_t colorRnd;   // Terrain colour r, g, b (bits 24..31, 16..23, 8..15), blade random (bits 4..7), patch/type (bits 0..3).
};
static_assert(sizeof(GrassInstanceGpu) == kGrassInstanceBytes);

// Push constants of Grass.vert / Grass.frag (one draw per LOD).
struct GrassPushGpu {
    std::uint32_t lod;
    std::uint32_t instanceBase;
    std::uint32_t reserved0;
    std::uint32_t reserved1;
};
static_assert(sizeof(GrassPushGpu) == 16);
static_assert(offsetof(GrassPushGpu, reserved0) == 8);

// Push constants of GrassShadow.vert (one depth-only draw per cascade and LOD).
// NOTE: must match GrassShadowPush in Shaders/GrassShadow.vert (80 bytes).
struct GrassShadowPushGpu {
    float cascadeVpRel[16]; // Eye-relative world to cascade clip, column-major.
    std::uint32_t lod;
    std::uint32_t instanceBase;
    float minWidth;         // Minimum blade width in metres (~shadowWidthScale * texelWorld).
    float biasM;            // Caster shift along sun direction in metres (shadowBiasTexels * texelWorld).
};
static_assert(sizeof(GrassShadowPushGpu) == 80);
static_assert(std::is_trivially_copyable_v<GrassShadowPushGpu>);
static_assert(offsetof(GrassShadowPushGpu, lod) == 64);
static_assert(offsetof(GrassShadowPushGpu, instanceBase) == 68);
static_assert(offsetof(GrassShadowPushGpu, minWidth) == 72);
static_assert(offsetof(GrassShadowPushGpu, biasM) == 76);

// Params storage block of every grass shader. NOTE: must match GrassParamsBlock in Shaders/GrassCommon.glsl.
// Space: "eye" = relative to the camera; "grid" = relative to the corner of chunk grid cell (0,0) (small numbers).
struct GrassParamsGpu {
    float viewProjection[16];     // 0    Unjittered, eye-relative, column-major (culling and Hi-Z).
    float drawViewProjection[16]; // 64   Jittered, eye-relative, column-major (drawing).
    float planes[6][4];           // 128  Eye-relative frustum planes, inside when dot(n, p) + w >= 0.
    float camRel[4];              // 224  x,z = camera grid-relative xz, y = camera world y, w = chunk size (m).
    std::int32_t grid[4];         // 240  xy = world chunk index of cell (0,0), z = cells per side, w = flags.
    float tile[4];                // 256  xy = grid-relative xz of texel (0,0) centre, z = texel size, w = texels per side.
    float water[4];               // 272  x = sea level, y = margin, z = fade band, w = spare.
    float lodDist[4];             // 288  x,y,z = LOD0/1/2 max distance, w = far edge fade fraction.
    float density[4];             // 304  xyz = blades per m^2 at the LOD0 / LOD1 / LOD2 distances, w = patchiness.
    float slope[4];               // 320  x = max slope deg, y = slope fade deg, z = patch scale m, w = bare threshold.
    float blade[4];               // 336  x = height, y = width, z = taper, w = height variation.
    float bladeB[4];              // 352  x = lean rad, y = width compensation exponent, z = max width scale, w = max bend rad.
    float wind0[4];               // 368  xy = wind dir (unit xz), z = speed m/s, w = total time s.
    float wind1[4];               // 384  x = gust strength, yz = pattern offset xz (m), w = wind bend strength.
    float wind2[4];               // 400  x = flutter speed rad/s, y = flutter strength, z = wave length m, w = gust length m.
    float sunDir[4];              // 416  xyz = toward the sun, w = translucency strength.
    float sunRadiance[4];         // 432  rgb = sun illuminance (tinted), w = sun visibility multiplier.
    float skyAmbient[4];          // 448  rgb = sky ambient x intensity, w = sky diffuse fill in shadow.
    float groundAmbient[4];       // 464  rgb = ground ambient x intensity, w = shadow caster frustum pad (m).
    float colorRoot[4];           // 480  rgb = root albedo (linear), w = root AO.
    float colorTip[4];            // 496  rgb = tip albedo (linear), w = tint variation.
    float cameraWorld[4];         // 512  xy = camera world xz.
    std::uint32_t instanceBase[4]; // 528 xyz = first instance of each LOD region.
    std::uint32_t instanceCap[4]; // 544  xyz = capacity of each LOD region.
    std::uint32_t misc[4];        // 560  x = debug mode (0 normal, 1 LOD, 2 sway, 3 chunks, 4 sun vis), y = placement seed.
    float hiz[4];                 // 576  xy = atlas size, zw = level 0 size.
    float depthMap[4];            // 592  x = k, y = b of d = k + b / w, z = level count.
    float look[4];                // 608  x = terrain tint strength, y = ground normal blend, z = self shadow strength, w = LOD0 segments.
    float extra[4];               // 624  x = density blend share, y = far height fade share, z = far terrain colour blend, w = workgroups per chunk.
    float densityLog[4];          // 640  xyz = ln of the three densities (floored at 1e-4), w = 1 when LOD0 density is above 0.
    float variety[4];             // 656  x = clump strength, y = min patch density, z = weed share, w = dry share.
    float variety2[4];            // 672  x = flower share, y = modern shading blend, z = jitter strength, w = patch tint.
    float shapeWind[4];           // 688  x = min pixel width, y = blade shape blend, z = wind detail, w = wind shade.
    float lodFar[4];              // 704  x = stochastic LOD transition band (m), y = far flatten start (m), z = far luminance, w = min blade width per metre of distance (draw pass).
};
static_assert(sizeof(GrassParamsGpu) == 720);
static_assert(std::is_trivially_copyable_v<GrassParamsGpu>);
static_assert(offsetof(GrassParamsGpu, drawViewProjection) == 64);
static_assert(offsetof(GrassParamsGpu, planes) == 128);
static_assert(offsetof(GrassParamsGpu, camRel) == 224);
static_assert(offsetof(GrassParamsGpu, grid) == 240);
static_assert(offsetof(GrassParamsGpu, tile) == 256);
static_assert(offsetof(GrassParamsGpu, water) == 272);
static_assert(offsetof(GrassParamsGpu, lodDist) == 288);
static_assert(offsetof(GrassParamsGpu, density) == 304);
static_assert(offsetof(GrassParamsGpu, slope) == 320);
static_assert(offsetof(GrassParamsGpu, blade) == 336);
static_assert(offsetof(GrassParamsGpu, bladeB) == 352);
static_assert(offsetof(GrassParamsGpu, wind0) == 368);
static_assert(offsetof(GrassParamsGpu, wind1) == 384);
static_assert(offsetof(GrassParamsGpu, wind2) == 400);
static_assert(offsetof(GrassParamsGpu, sunDir) == 416);
static_assert(offsetof(GrassParamsGpu, sunRadiance) == 432);
static_assert(offsetof(GrassParamsGpu, skyAmbient) == 448);
static_assert(offsetof(GrassParamsGpu, groundAmbient) == 464);
static_assert(offsetof(GrassParamsGpu, colorRoot) == 480);
static_assert(offsetof(GrassParamsGpu, colorTip) == 496);
static_assert(offsetof(GrassParamsGpu, cameraWorld) == 512);
static_assert(offsetof(GrassParamsGpu, instanceBase) == 528);
static_assert(offsetof(GrassParamsGpu, instanceCap) == 544);
static_assert(offsetof(GrassParamsGpu, misc) == 560);
static_assert(offsetof(GrassParamsGpu, hiz) == 576);
static_assert(offsetof(GrassParamsGpu, depthMap) == 592);
static_assert(offsetof(GrassParamsGpu, look) == 608);
static_assert(offsetof(GrassParamsGpu, extra) == 624);
static_assert(offsetof(GrassParamsGpu, densityLog) == 640);
static_assert(offsetof(GrassParamsGpu, variety) == 656);
static_assert(offsetof(GrassParamsGpu, variety2) == 672);
static_assert(offsetof(GrassParamsGpu, shapeWind) == 688);
static_assert(offsetof(GrassParamsGpu, lodFar) == 704);

// Number of candidate blades per chunk for a density. Same rounding as CandidateCount() in GrassCull.comp.
inline std::uint32_t GrassCandidatesPerChunk(float bladesPerM2) noexcept
{
    const float count = bladesPerM2 * kGrassChunkSizeM * kGrassChunkSizeM;
    const auto whole = static_cast<std::uint32_t>(count);
    return count > static_cast<float>(whole) ? whole + 1u : whole;
}

// Shadow and cloud parameters of GrassCull.comp. NOTE: must match GrassShadowBlock in GrassCull.comp (448 bytes).
struct GrassShadowParamsGpu {
    float cascadeViewProjection[4][16]; // 0    World to cascade clip (column-major).
    float cascadeSplitFar[4];           // 256  Planar view depth where cascade i ends.
    float cascadeBlendWidth[4];         // 272  Blend band at the end of cascade i.
    float cascadeTexelWorld[4];         // 288  World size of one texel in cascade i.
    float cascadeDepthRange[4];         // 304  Ortho depth span of cascade i (world units).
    float eyeAndFadeStart[4];           // 320  xyz = receiver camera world position, w = distance fade start.
    float forwardAndFadeEnd[4];         // 336  xyz = receiver camera forward, w = shadow max distance.
    float shadowParams[4];              // 352  x = strength, y = const bias (texels), z = filter radius (texels), w = 1 / map size.
    float biasExtra[4];                 // 368  x = slope bias (texels), y = normal offset (texels), z = sun offset (texels), w = spare.
    std::uint32_t counts[4];            // 384  x = cascade count (0 = off), y = filter taps, z = flags, w = spare.
    float cloudGrid[4];                 // 400  xy = cloud map centre xz (m), z = 1 / extent (m), w = strength (0 = off).
    float cloudLayer[4];                // 416  x = layer base (m), y = layer top (m), z = sharpness, w = map size (texels).
    float cloudSun[4];                  // 432  xyz = unit direction toward sun for cloud ray, w = spare.
};
static_assert(sizeof(GrassShadowParamsGpu) == 448);
static_assert(std::is_trivially_copyable_v<GrassShadowParamsGpu>);
static_assert(offsetof(GrassShadowParamsGpu, cascadeSplitFar) == 256);
static_assert(offsetof(GrassShadowParamsGpu, cascadeBlendWidth) == 272);
static_assert(offsetof(GrassShadowParamsGpu, cascadeTexelWorld) == 288);
static_assert(offsetof(GrassShadowParamsGpu, cascadeDepthRange) == 304);
static_assert(offsetof(GrassShadowParamsGpu, eyeAndFadeStart) == 320);
static_assert(offsetof(GrassShadowParamsGpu, forwardAndFadeEnd) == 336);
static_assert(offsetof(GrassShadowParamsGpu, shadowParams) == 352);
static_assert(offsetof(GrassShadowParamsGpu, biasExtra) == 368);
static_assert(offsetof(GrassShadowParamsGpu, counts) == 384);
static_assert(offsetof(GrassShadowParamsGpu, cloudGrid) == 400);
static_assert(offsetof(GrassShadowParamsGpu, cloudLayer) == 416);
static_assert(offsetof(GrassShadowParamsGpu, cloudSun) == 432);

// Debug view of the blade colour.
enum class GrassDebugView : std::uint32_t { Normal = 0, LodColor = 1, Sway = 2, ChunkBounds = 3, SunVis = 4 };

// User-facing settings. Lives in the feature object; the panel and the HTTP endpoint edit it.
struct GrassSettings {
    bool enabled = true;
    bool hizCulling = false;
    bool collectStats = true;
    GrassDebugView debugView = GrassDebugView::Normal; // LodColor: LOD0 green, LOD1 yellow, LOD2 red. ChunkBounds: 16 m chunk checker and borders.

    // Density in blades per m^2: densityLod0 up close, falling smoothly to densityLod1 at lod0Distance and to densityLod2 at
    // lod1Distance. Thinner densities are index prefixes of denser ones, so blades only ever leave, never move.
    float densityLod0 = 40.0f;
    float densityLod1 = 6.0f;
    float densityLod2 = 0.8f;
    float patchiness = 0.6f;     // 0 = even cover, 1 = bare patches between meadows.
    float patchScaleM = 45.0f;   // Size of one meadow patch.
    float bareThreshold = 0.35f; // Noise level where bare ground starts.

    float bladeHeight = 0.6f;
    float bladeWidth = 0.045f;
    float bladeTaper = 0.85f;    // 0 = constant width, 1 = pointed tip.
    float heightVariation = 0.35f;
    float lean = 0.25f;          // Rest lean (rad).
    float maxBend = 1.2f;        // Largest tip angle (rad).
    float widthCompensation = 0.45f; // Thinned blades widen by (full density / local density) to this power.
    float maxWidthScale = 5.0f;      // Largest widening.
    std::uint32_t lod0Segments = 4;  // Quads along a near blade (3..6); 6 list vertices each.
    float minPixelWidth = 1.1f;      // Minimum screen-space blade width in pixels (0 = off).
    float bladeShape = 1.0f;         // Blend from flat ribbon (0) to twist, droop-by-height and base-heavy width (1).

    float maxSlopeDeg = 50.0f;
    float slopeFadeDeg = 12.0f;
    float waterMargin = 0.6f;    // Metres above the water level where grass starts.
    float waterFadeBand = 1.5f;
    float sandHeight = 7.0f;     // Metres above the ocean that stay bare (beach sand); fades out over 3 m.
    float alpineHeight = 450.0f; // World Y above which grass thins out (rock and snow).
    float alpineFade = 150.0f;   // Metres over which it fades to nothing.

    float lod0Distance = 40.0f;  // Mesh switch LOD0 -> LOD1 and the end of the first density fall.
    float lod1Distance = 120.0f;
    float lod2Distance = 300.0f;
    float edgeFade = 0.2f;       // Share of the last LOD where blades thin out.
    float densityBlend = 0.5f;   // Share of a LOD range over which the density falls (0.05 = nearly a step).
    float farHeightFade = 0.15f; // Share of lod2Distance over which blades shrink to nothing.
    float lodFadeBand = 6.0f;    // Stochastic LOD transition band width around lod0Distance and lod1Distance in metres (0 = hard switch).

    float windBend = 1.0f;
    float flutterSpeed = 9.0f;   // rad/s.
    float flutterStrength = 1.0f;
    float gustStrength = 0.5f;
    float windWaveLengthM = 28.0f;
    float windGustLengthM = 14.0f;
    float windDetail = 0.7f;     // Blend of fine 3.5 m gust octave and per-blade stiffness (0 = uniform wind).
    float windShade = 0.25f;     // Brightness wave from tip angle in modern shading (0 = off).
    float windSpeedFallback = 4.0f;       // Used when no shared wind is published.
    float windDirectionDegFallback = 45.0f; // 0 = +X, 90 = +Z (same as the cloud setting).

    float translucency = 0.6f;
    float rootAo = 0.35f;
    float tintVariation = 0.5f;
    float terrainTint = 0.6f;       // How far blades take the colour of the terrain under them (0 = own colours).
    float farTerrainBlend = 0.8f;   // Far blades converge to the terrain colour so the field melts into the ground.
    float groundNormalBlend = 0.5f; // Share of the ground normal in the blade normal.
    float selfShadow = 0.7f;        // Sun fades on ground facing away from the sun.
    float colorRoot[3] = { 0.022f, 0.060f, 0.009f };
    float colorTip[3] = { 0.10f, 0.20f, 0.035f };
    float jitterStrength = 0.6f;    // R2 lattice jitter radius in 1/sqrt(density) units (0 = regular R2 lattice).
    float clumpStrength = 0.6f;     // Tuft pull, shared tuft height and outward fountain lean (0 = off).
    float minPatchDensity = 0.22f;  // Minimum density floor in poor meadow patches, paired with shorter grass (0 = bare patches).
    float patchTint = 1.0f;         // Meadow and tuft hue shift toward olive (0 = uniform palette).
    float weedShare = 0.06f;        // Share of broadleaf rosette weeds (0 = off).
    float dryShare = 0.12f;         // Base share of dry straw blades, weighted toward poor patches (0 = off).
    float flowerShare = 0.004f;     // Share of rare wildflowers in near LOD0 grass (0 = off).
    float lookModern = 1.0f;        // Blend from legacy shading (0) to two-sided translucent + sheen + distance flatten (1).
    float farFlatten = 25.0f;       // Distance in metres where root-to-tip gradient and root AO flattening starts.
    float farLuminance = 1.15f;     // Multiplier on lit grass colour beyond 100 m to match terrain luminance (1 = off).

    std::uint32_t gridCells = 41;        // Chunks per side (at most kGrassMaxGridCells); must cover 2 x (lod2 + 16 m).
    std::uint32_t heightTileTexels = 512; // Texels per side of the height tile the editor layer should build (at most 512).
    float heightTileTexelSize = 2.0f;     // Metres per texel of that tile (0.5..4); the tile must reach 2 x (lod2 + 32 m).
    std::uint32_t instanceCapLod0 = 250000;
    std::uint32_t instanceCapLod1 = 220000;
    std::uint32_t instanceCapLod2 = 180000;
    std::uint32_t seed = 1;

    bool followCloudWind = true;  // Use the wind the clouds drift with; off uses the fallback values above.
    float sunVisibility = 1.0f;   // Flat multiplier on the direct sun term.
    bool receiveSunShadow = true;
    bool receiveCloudShadow = true;
    float shadowStrength = 1.0f;    // Fraction of cascade and cloud shadow applied to the blade sun term.
    float cloudFill = 0.10f;        // Subtle diffuse sky fill added to shadowed blades.
    bool castShadows = true;
    float shadowMaxTexelM = 0.40f;  // Skip cascades coarser than this texel size (m).
    float shadowBiasTexels = 1.0f;  // Shift shadow blades toward the sun (texels).
    float shadowWidthScale = 1.5f;  // Minimum shadow blade width (texels).
    float shadowPadM = 0.0f;        // Extra camera-frustum chunk padding while casting shadows (m); colour draws pay for it too.
};

// Named starting points for the settings. They only change density, distances and buffer sizes.
enum class GrassPreset : std::uint32_t { Lean = 0, Medium, Big, Huge, Count };

// Terrain colour of a tile texel: linear 0..1 per channel in 8 bits each (r lowest).
inline std::uint32_t GrassPackTerrainColor(float r, float g, float b) noexcept
{
    const auto byte = [](float v) {
        return static_cast<std::uint32_t>(v <= 0.0f ? 0.0f : (v >= 1.0f ? 255.0f : v * 255.0f + 0.5f));
    };
    return byte(r) | (byte(g) << 8) | (byte(b) << 16);
}

// Second tile float of a texel as raw bits: bit 24 = submerged, bits 0..23 = terrain colour (GrassPackTerrainColor).
inline float GrassTileFlagsColor(bool submerged, std::uint32_t colorBits) noexcept
{
    return std::bit_cast<float>((submerged ? 0x01000000u : 0u) | (colorBits & 0x00FFFFFFu));
}
inline bool GrassTileSubmerged(float flagsColor) noexcept
{
    return (std::bit_cast<std::uint32_t>(flagsColor) & 0x01000000u) != 0u;
}

// Height tile the editor layer submits: terrain height, a submerged flag and the terrain colour per texel on a regular grid.
// Texel (i, j) sits at world (originX + i * texelSize, originZ + j * texelSize); data index (j * size + i) * kGrassTileStride.
struct GrassHeightTile {
    std::uint32_t size = 0;      // Texels per side.
    float texelSize = 1.0f;      // Metres.
    double originX = 0.0;        // World x of texel (0, 0).
    double originZ = 0.0;
    float seaLevel = 0.0f;       // World Y of the ocean (the shader also drops grass below it).
    std::uint64_t revision = 0;  // Change it whenever the contents change.
    std::vector<float> data;     // kGrassTileStride floats per texel: height, then GrassTileFlagsColor() (submerged + colour bits).

    bool Valid() const noexcept
    {
        return size >= 2 && texelSize > 0.0f && data.size() == static_cast<std::size_t>(size) * size * kGrassTileStride;
    }
};

// Why the editor layer rebuilt the height tile.
enum class GrassTileReason : std::uint8_t { Initial, Moved, Stream, Carve, Water, Settings, Terrain, Count };

// What the editor layer's tile provider reports (read by the endpoint).
struct GrassTileSourceReport {
    bool terrainOpen = false;
    bool building = false;
    bool complete = false;            // Every texel near the camera came from a finest-level node.
    std::uint64_t revision = 0;
    std::uint32_t size = 0;
    float texelSize = 0.0f;
    double originX = 0.0;
    double originZ = 0.0;
    double lastBuildMs = 0.0;         // Worker thread time of the last build.
    double maxBuildMs = 0.0;
    double lastMainMs = 0.0;          // Main thread time of the last request (node collection) plus the submit.
    float coverageLevel0 = 0.0f;      // Share of texels within reach that came from level-0 nodes.
    float coverageNodes = 0.0f;       // Share that came from any resident node (the rest is noise fallback).
    float submergedShare = 0.0f;
    std::uint64_t builds = 0;
    std::uint64_t skippedIncomplete = 0; // Builds dropped because too little terrain was resident.
    std::uint64_t reasons[static_cast<std::size_t>(GrassTileReason::Count)] = {};
};

// One point compared across the three height sources (metres).
struct GrassProbeReport {
    bool ok = false;
    std::string message;
    double tileHeight = 0.0;  // What the grass shader reads.
    double meshHeight = 0.0;  // The drawn terrain triangles.
    double noiseHeight = 0.0; // Noise plus carve, no erosion (what /terrain/height reports).
    int nodeLevel = -1;
    bool tileSubmerged = false;
    bool waterHere = false;
    double waterSurface = 0.0;
};

using GrassTileReportFn = std::function<GrassTileSourceReport()>;
using GrassProbeFn = std::function<GrassProbeReport(double x, double z)>;

enum class GrassFrameStatus : std::uint8_t {
    NotRendered,
    Ok,
    Disabled,
    NoValidCamera,
    NoTile,
    ShadersNotReady,
    TargetFailed,
    PassDisabled,
};

// What the endpoint and the panel read, one per view scope.
struct GrassViewSnapshot {
    GrassFrameStatus status = GrassFrameStatus::NotRendered;
    std::uint64_t frameIndex = 0;
    Extent2D extent{};
    bool hizUsed = false;
    bool windShared = false;      // The shared cloud wind was read this frame.
    float windDirX = 0.0f;        // Wind the blades used: unit direction, speed (m/s) and pattern offset (m).
    float windDirZ = 0.0f;
    float windSpeed = 0.0f;
    float windOffsetX = 0.0f;
    float windOffsetZ = 0.0f;
    std::uint32_t gridCells = 0;
    std::uint64_t tileRevision = 0;
    std::uint64_t instanceBytes = 0;
    std::uint32_t shadowCascades = 0;
    bool shadowCloud = false;
    std::uint32_t shadowCastCascades = 0;
    std::uint64_t shadowCastBlades = 0;
    bool statsValid = false;      // `stats` came from a readback two frames old.
    std::uint64_t statsFrame = 0;
    GrassStatsGpu stats{};
};

} // namespace gte
