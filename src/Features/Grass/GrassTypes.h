#pragma once

#include "../../Core/Extent.h"

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
inline constexpr float kGrassChunkSizeM = 16.0f;
inline constexpr std::uint32_t kGrassMaxGridCells = 48;      // Chunks per grid side.
inline constexpr std::uint32_t kGrassCullGroupSize = 64;     // local_size_x of GrassCull.comp.
inline constexpr std::uint32_t kGrassCandidateRound = 256;   // Candidates per workgroup round.
inline constexpr std::uint32_t kGrassStatsWords = 32;
inline constexpr std::uint32_t kGrassCommandStride = 16;     // Bytes between draw commands.
inline constexpr std::uint32_t kGrassInstanceBytes = 32;
inline constexpr std::uint32_t kGrassMaxTileTexels = 512;     // Height tile limit per side (the GPU copy has this fixed size).

// Strip vertex counts: 5 segments, 2 segments, one triangle. List counts are used when the draw is a triangle list.
inline constexpr std::uint32_t kGrassVertsStrip[kGrassLodCount] = {12, 6, 3};
inline constexpr std::uint32_t kGrassVertsList[kGrassLodCount] = {30, 12, 3};

// Bits of GrassParamsGpu::grid[3].
inline constexpr std::int32_t kGrassFlagHiz = 1;
inline constexpr std::int32_t kGrassFlagTriangleList = 2;
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

// One blade, written by GrassCull.comp and read by Grass.vert.
struct GrassInstanceGpu {
    float posRot[4]; // xyz = eye-relative root, w = yaw (rad).
    float shape[4];  // x = height, y = width, z = lean (rad), w = random 0..1.
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
    float density[4];             // 304  xyz = blades per m^2 per LOD, w = patchiness.
    float slope[4];               // 320  x = max slope deg, y = slope fade deg, z = patch scale m, w = bare threshold.
    float blade[4];               // 336  x = height, y = width, z = taper, w = height variation.
    float bladeB[4];              // 352  x = lean rad, y = LOD1 width scale, z = LOD2 width scale, w = max bend rad.
    float wind0[4];               // 368  xy = wind dir (unit xz), z = speed m/s, w = total time s.
    float wind1[4];               // 384  x = gust strength, yz = pattern offset xz (m), w = wind bend strength.
    float wind2[4];               // 400  x = flutter speed rad/s, y = flutter strength, z = wave length m, w = gust length m.
    float sunDir[4];              // 416  xyz = toward the sun, w = translucency strength.
    float sunRadiance[4];         // 432  rgb = sun illuminance (tinted), w = sun visibility multiplier.
    float skyAmbient[4];          // 448  rgb = sky ambient x intensity.
    float groundAmbient[4];       // 464  rgb = ground ambient x intensity.
    float colorRoot[4];           // 480  rgb = root albedo (linear), w = root AO.
    float colorTip[4];            // 496  rgb = tip albedo (linear), w = tint variation.
    float cameraWorld[4];         // 512  xy = camera world xz.
    std::uint32_t instanceBase[4]; // 528 xyz = first instance of each LOD region.
    std::uint32_t instanceCap[4]; // 544  xyz = capacity of each LOD region.
    std::uint32_t misc[4];        // 560  x = debug mode (0 normal, 1 LOD colour, 2 sway), y = placement seed.
    float hiz[4];                 // 576  xy = atlas size, zw = level 0 size.
    float depthMap[4];            // 592  x = k, y = b of d = k + b / w, z = level count.
};
static_assert(sizeof(GrassParamsGpu) == 608);
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

// Number of candidate blades per chunk for a density. Same rounding as CandidateCount() in GrassCull.comp.
inline std::uint32_t GrassCandidatesPerChunk(float bladesPerM2) noexcept
{
    const float count = bladesPerM2 * kGrassChunkSizeM * kGrassChunkSizeM;
    const auto whole = static_cast<std::uint32_t>(count);
    return count > static_cast<float>(whole) ? whole + 1u : whole;
}

// Debug view of the blade colour.
enum class GrassDebugView : std::uint32_t { Normal = 0, LodColor = 1, Sway = 2, ChunkBounds = 3 };

// User-facing settings. Lives in the feature object; the panel and the HTTP endpoint edit it.
struct GrassSettings {
    bool enabled = true;
    bool hizCulling = true;
    bool collectStats = true;
    GrassDebugView debugView = GrassDebugView::Normal; // LodColor: LOD0 green, LOD1 yellow, LOD2 red. ChunkBounds: 16 m chunk checker and borders.

    // Density in blades per m^2. LOD1 and LOD2 are index prefixes of LOD0, so they thin out the same blades.
    float densityLod0 = 32.0f;
    float densityLod1 = 5.0f;
    float densityLod2 = 0.6f;
    float patchiness = 0.6f;     // 0 = even cover, 1 = bare patches between meadows.
    float patchScaleM = 45.0f;   // Size of one meadow patch.
    float bareThreshold = 0.35f; // Noise level where bare ground starts.

    float bladeHeight = 0.6f;
    float bladeWidth = 0.045f;
    float bladeTaper = 0.85f;    // 0 = constant width, 1 = pointed tip.
    float heightVariation = 0.35f;
    float lean = 0.25f;          // Rest lean (rad).
    float maxBend = 1.2f;        // Largest tip angle (rad).
    float lod1WidthScale = 2.2f; // Fewer blades are wider so cover stays even.
    float lod2WidthScale = 4.5f;

    float maxSlopeDeg = 40.0f;
    float slopeFadeDeg = 8.0f;
    float waterMargin = 0.6f;    // Metres above the water level where grass starts.
    float waterFadeBand = 1.5f;
    float alpineHeight = 450.0f; // World Y above which grass thins out (rock and snow).
    float alpineFade = 150.0f;   // Metres over which it fades to nothing.

    float lod0Distance = 30.0f;
    float lod1Distance = 80.0f;
    float lod2Distance = 150.0f;
    float edgeFade = 0.2f;       // Share of the last LOD where blades thin out.

    float windBend = 1.0f;
    float flutterSpeed = 9.0f;   // rad/s.
    float flutterStrength = 1.0f;
    float gustStrength = 0.5f;
    float windWaveLengthM = 28.0f;
    float windGustLengthM = 14.0f;
    float windSpeedFallback = 4.0f;       // Used when no shared wind is published.
    float windDirectionDegFallback = 45.0f; // 0 = +X, 90 = +Z (same as the cloud setting).

    float translucency = 0.6f;
    float rootAo = 0.35f;
    float tintVariation = 0.5f;
    float colorRoot[3] = {0.025f, 0.06f, 0.012f};
    float colorTip[3] = {0.17f, 0.30f, 0.05f};

    std::uint32_t gridCells = 22;        // Chunks per side (at most kGrassMaxGridCells); must cover 2 x (lod2 + 16 m).
    std::uint32_t heightTileTexels = 384; // Texels per side of the height tile the editor layer should build (at most 512).
    float heightTileTexelSize = 1.5f;     // Metres per texel of that tile (0.5..4); the tile must reach 2 x (lod2 + 32 m).
    std::uint32_t instanceCapLod0 = 400000;
    std::uint32_t instanceCapLod1 = 300000;
    std::uint32_t instanceCapLod2 = 200000;
    std::uint32_t seed = 1;

    bool followCloudWind = true;  // Use the wind the clouds drift with; off uses the fallback values above.
    float sunVisibility = 1.0f;   // Flat multiplier on the sun term (no shadows reach grass yet).
};

// Height tile the editor layer submits: terrain height and a submerged flag per texel on a regular grid.
// Texel (i, j) sits at world (originX + i * texelSize, originZ + j * texelSize); data index (j * size + i) * 2.
struct GrassHeightTile {
    std::uint32_t size = 0;      // Texels per side.
    float texelSize = 1.0f;      // Metres.
    double originX = 0.0;        // World x of texel (0, 0).
    double originZ = 0.0;
    float seaLevel = 0.0f;       // World Y of the ocean (the shader also drops grass below it).
    std::uint64_t revision = 0;  // Change it whenever the contents change.
    std::vector<float> data;     // 2 floats per texel: height, submerged flag (0 or 1).

    bool Valid() const noexcept
    {
        return size >= 2 && texelSize > 0.0f && data.size() == static_cast<std::size_t>(size) * size * 2u;
    }
};

// Why the editor layer rebuilt the height tile.
enum class GrassTileReason : std::uint8_t { Initial, Moved, Stream, Carve, Water, Settings, Count };

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
    std::uint32_t gridCells = 0;
    std::uint64_t tileRevision = 0;
    std::uint64_t instanceBytes = 0;
    bool statsValid = false;      // `stats` came from a readback two frames old.
    std::uint64_t statsFrame = 0;
    GrassStatsGpu stats{};
};

} // namespace gte
