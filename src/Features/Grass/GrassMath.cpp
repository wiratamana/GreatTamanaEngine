#include "GrassMath.h"

#include "../../Math/Mat4d.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gte {

namespace {

constexpr float kMaxLod2Distance = 360.0f; // The largest grid (48 chunks) reaches this far with one chunk to spare.
constexpr std::uint32_t kMaxRegionCap = 2000000;
constexpr std::uint64_t kMaxTotalInstances = 4000000; // 128 MiB at 32 bytes.
constexpr double kPiD = 3.14159265358979323846;

float Finite(float v, float fallback) noexcept
{
    return std::isfinite(v) ? v : fallback;
}

float Clamp(float v, float fallback, float lo, float hi) noexcept
{
    return std::clamp(Finite(v, fallback), lo, hi);
}

float Smooth(float a, float b, float x) noexcept
{
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void Set4(float (&dst)[4], float x, float y, float z, float w) noexcept
{
    dst[0] = x;
    dst[1] = y;
    dst[2] = z;
    dst[3] = w;
}

} // namespace

std::uint32_t MinGrassGridCells(float lod2Distance) noexcept
{
    // The camera sits somewhere inside its centre chunk, so the grid needs one chunk more on each side.
    const float cells = 2.0f * (lod2Distance + kGrassChunkSizeM) / kGrassChunkSizeM;
    return static_cast<std::uint32_t>(std::ceil(cells)) + 1u;
}

std::uint32_t MinGrassTileTexels(float lod2Distance, float texelSize) noexcept
{
    return static_cast<std::uint32_t>(std::ceil(2.0f * (lod2Distance + 2.0f * kGrassChunkSizeM) / std::max(texelSize, 0.1f)));
}

GrassSettings SanitizeGrassSettings(const GrassSettings& in, bool enforceOrder) noexcept
{
    const GrassSettings d{};
    GrassSettings s = in;

    s.debugView = static_cast<GrassDebugView>(std::min<std::uint32_t>(static_cast<std::uint32_t>(in.debugView), 4u));

    s.densityLod0 = Clamp(in.densityLod0, d.densityLod0, 0.0f, 200.0f);
    s.densityLod1 = Clamp(in.densityLod1, d.densityLod1, 0.0f, 200.0f);
    s.densityLod2 = Clamp(in.densityLod2, d.densityLod2, 0.0f, 200.0f);
    if (enforceOrder) {
        s.densityLod1 = std::min(s.densityLod1, s.densityLod0);
        s.densityLod2 = std::min(s.densityLod2, s.densityLod1);
    }
    s.patchiness = Clamp(in.patchiness, d.patchiness, 0.0f, 1.0f);
    s.patchScaleM = Clamp(in.patchScaleM, d.patchScaleM, 2.0f, 500.0f);
    s.bareThreshold = Clamp(in.bareThreshold, d.bareThreshold, 0.0f, 1.0f);

    s.bladeHeight = Clamp(in.bladeHeight, d.bladeHeight, 0.05f, 3.0f);
    s.bladeWidth = Clamp(in.bladeWidth, d.bladeWidth, 0.002f, 0.5f);
    s.bladeTaper = Clamp(in.bladeTaper, d.bladeTaper, 0.0f, 1.0f);
    s.heightVariation = Clamp(in.heightVariation, d.heightVariation, 0.0f, 0.9f);
    s.lean = Clamp(in.lean, d.lean, 0.0f, 1.2f);
    s.maxBend = Clamp(in.maxBend, d.maxBend, 0.1f, 1.5f);
    s.widthCompensation = Clamp(in.widthCompensation, d.widthCompensation, 0.0f, 1.0f);
    s.maxWidthScale = Clamp(in.maxWidthScale, d.maxWidthScale, 1.0f, 16.0f);
    s.lod0Segments = std::clamp(in.lod0Segments, kGrassMinLod0Segments, kGrassMaxLod0Segments);

    s.maxSlopeDeg = Clamp(in.maxSlopeDeg, d.maxSlopeDeg, 1.0f, 89.0f);
    s.slopeFadeDeg = Clamp(in.slopeFadeDeg, d.slopeFadeDeg, 0.0f, 45.0f);
    s.waterMargin = Clamp(in.waterMargin, d.waterMargin, -2.0f, 20.0f);
    s.waterFadeBand = Clamp(in.waterFadeBand, d.waterFadeBand, 0.0f, 20.0f);
    s.sandHeight = Clamp(in.sandHeight, d.sandHeight, 0.0f, 200.0f);
    s.alpineHeight = Clamp(in.alpineHeight, d.alpineHeight, -1000.0f, 100000.0f);
    s.alpineFade = Clamp(in.alpineFade, d.alpineFade, 0.0f, 5000.0f);

    s.lod2Distance = Clamp(in.lod2Distance, d.lod2Distance, 10.0f, kMaxLod2Distance);
    s.lod1Distance = Clamp(in.lod1Distance, d.lod1Distance, 6.0f, enforceOrder ? s.lod2Distance - 4.0f : kMaxLod2Distance);
    s.lod0Distance = Clamp(in.lod0Distance, d.lod0Distance, 2.0f, enforceOrder ? s.lod1Distance - 2.0f : kMaxLod2Distance);
    s.edgeFade = Clamp(in.edgeFade, d.edgeFade, 0.01f, 1.0f);
    s.densityBlend = Clamp(in.densityBlend, d.densityBlend, 0.05f, 1.0f);
    s.farHeightFade = Clamp(in.farHeightFade, d.farHeightFade, 0.0f, 0.6f);

    s.windBend = Clamp(in.windBend, d.windBend, 0.0f, 4.0f);
    s.flutterSpeed = Clamp(in.flutterSpeed, d.flutterSpeed, 0.0f, 60.0f);
    s.flutterStrength = Clamp(in.flutterStrength, d.flutterStrength, 0.0f, 4.0f);
    s.gustStrength = Clamp(in.gustStrength, d.gustStrength, 0.0f, 3.0f);
    s.windWaveLengthM = Clamp(in.windWaveLengthM, d.windWaveLengthM, 2.0f, 400.0f);
    s.windGustLengthM = Clamp(in.windGustLengthM, d.windGustLengthM, 2.0f, 400.0f);
    s.windSpeedFallback = Clamp(in.windSpeedFallback, d.windSpeedFallback, 0.0f, 100.0f);
    s.windDirectionDegFallback = Clamp(in.windDirectionDegFallback, d.windDirectionDegFallback, -720.0f, 720.0f);

    s.translucency = Clamp(in.translucency, d.translucency, 0.0f, 4.0f);
    s.rootAo = Clamp(in.rootAo, d.rootAo, 0.0f, 1.0f);
    s.tintVariation = Clamp(in.tintVariation, d.tintVariation, 0.0f, 1.0f);
    s.terrainTint = Clamp(in.terrainTint, d.terrainTint, 0.0f, 1.0f);
    s.farTerrainBlend = Clamp(in.farTerrainBlend, d.farTerrainBlend, 0.0f, 1.0f);
    s.groundNormalBlend = Clamp(in.groundNormalBlend, d.groundNormalBlend, 0.0f, 1.0f);
    s.selfShadow = Clamp(in.selfShadow, d.selfShadow, 0.0f, 1.0f);
    for (int i = 0; i < 3; ++i) {
        s.colorRoot[i] = Clamp(in.colorRoot[i], d.colorRoot[i], 0.0f, 2.0f);
        s.colorTip[i] = Clamp(in.colorTip[i], d.colorTip[i], 0.0f, 2.0f);
    }
    s.jitterStrength = Clamp(in.jitterStrength, d.jitterStrength, 0.0f, 1.5f);
    s.clumpStrength = Clamp(in.clumpStrength, d.clumpStrength, 0.0f, 1.0f);
    s.minPatchDensity = Clamp(in.minPatchDensity, d.minPatchDensity, 0.0f, 1.0f);
    s.patchTint = Clamp(in.patchTint, d.patchTint, 0.0f, 1.0f);
    s.weedShare = Clamp(in.weedShare, d.weedShare, 0.0f, 0.5f);
    s.dryShare = Clamp(in.dryShare, d.dryShare, 0.0f, 0.5f);
    s.flowerShare = Clamp(in.flowerShare, d.flowerShare, 0.0f, 0.1f);
    s.lookModern = Clamp(in.lookModern, d.lookModern, 0.0f, 1.0f);
    s.sunVisibility = Clamp(in.sunVisibility, d.sunVisibility, 0.0f, 2.0f);
    s.shadowStrength = Clamp(in.shadowStrength, d.shadowStrength, 0.0f, 1.0f);
    s.cloudFill = Clamp(in.cloudFill, d.cloudFill, 0.0f, 1.0f);
    s.shadowMaxTexelM = Clamp(in.shadowMaxTexelM, d.shadowMaxTexelM, 0.01f, 4.0f);
    s.shadowBiasTexels = Clamp(in.shadowBiasTexels, d.shadowBiasTexels, -4.0f, 8.0f);
    s.shadowWidthScale = Clamp(in.shadowWidthScale, d.shadowWidthScale, 0.0f, 8.0f);
    s.shadowPadM = Clamp(in.shadowPadM, d.shadowPadM, 0.0f, 32.0f);

    const std::uint32_t minCells = MinGrassGridCells(s.lod2Distance);
    s.gridCells = std::clamp(std::max(in.gridCells, minCells), 2u, kGrassMaxGridCells);

    // The tile must reach lod2 + 2 chunks each way; a coarser texel keeps it inside the fixed GPU copy.
    s.heightTileTexelSize = Clamp(in.heightTileTexelSize, d.heightTileTexelSize, 0.5f, 4.0f);
    const float reach = 2.0f * (s.lod2Distance + 2.0f * kGrassChunkSizeM);
    s.heightTileTexelSize = std::max(s.heightTileTexelSize, reach / static_cast<float>(kGrassMaxTileTexels));
    s.heightTileTexels = std::clamp(
        std::max(in.heightTileTexels, MinGrassTileTexels(s.lod2Distance, s.heightTileTexelSize)), 64u, kGrassMaxTileTexels);

    s.instanceCapLod0 = std::clamp(in.instanceCapLod0, 1024u, kMaxRegionCap);
    s.instanceCapLod1 = std::clamp(in.instanceCapLod1, 1024u, kMaxRegionCap);
    s.instanceCapLod2 = std::clamp(in.instanceCapLod2, 1024u, kMaxRegionCap);
    while (std::uint64_t{ s.instanceCapLod0 } + s.instanceCapLod1 + s.instanceCapLod2 > kMaxTotalInstances) {
        s.instanceCapLod0 = s.instanceCapLod0 * 3u / 4u;
        s.instanceCapLod1 = s.instanceCapLod1 * 3u / 4u;
        s.instanceCapLod2 = s.instanceCapLod2 * 3u / 4u;
    }
    return s;
}

GrassSettings MakeGrassPreset(GrassPreset preset) noexcept
{
    GrassSettings s; // The defaults are the Big preset.
    switch (preset) {
    case GrassPreset::Lean:
        s.densityLod0 = 16.0f;
        s.densityLod1 = 2.5f;
        s.densityLod2 = 0.3f;
        s.lod0Distance = 20.0f;
        s.lod1Distance = 60.0f;
        s.lod2Distance = 120.0f;
        s.instanceCapLod0 = 100000;
        s.instanceCapLod1 = 80000;
        s.instanceCapLod2 = 60000;
        s.heightTileTexels = 384;
        s.heightTileTexelSize = 1.5f;
        break;
    case GrassPreset::Medium:
        s.densityLod0 = 32.0f;
        s.densityLod1 = 5.0f;
        s.densityLod2 = 0.6f;
        s.lod0Distance = 30.0f;
        s.lod1Distance = 80.0f;
        s.lod2Distance = 150.0f;
        s.instanceCapLod0 = 200000;
        s.instanceCapLod1 = 160000;
        s.instanceCapLod2 = 120000;
        s.heightTileTexels = 384;
        s.heightTileTexelSize = 1.5f;
        break;
    case GrassPreset::Big:
        break;
    case GrassPreset::Huge:
        s.densityLod0 = 64.0f;
        s.densityLod1 = 12.0f;
        s.densityLod2 = 1.6f;
        s.lod0Distance = 60.0f;
        s.lod1Distance = 180.0f;
        s.lod2Distance = 360.0f;
        s.instanceCapLod0 = 500000;
        s.instanceCapLod1 = 450000;
        s.instanceCapLod2 = 350000;
        break;
    case GrassPreset::Count:
        break;
    }
    return SanitizeGrassSettings(s);
}

float GrassDensityAtDistance(const GrassSettings& s, float distance) noexcept
{
    if (!(s.densityLod0 > 0.0f)) {
        return 0.0f;
    }
    const float blend = s.densityBlend;
    const float e0 = s.lod0Distance;
    const float e1 = s.lod1Distance;
    const float s0 = e0 * (1.0f - blend);
    const float s1 = std::max(e0, e1 * (1.0f - blend));
    const float t0 = Smooth(s0, e0, distance);
    const float t1 = Smooth(s1, e1, distance);
    const float l0 = std::log(std::max(s.densityLod0, 1e-4f));
    const float l1 = std::log(std::max(s.densityLod1, 1e-4f));
    const float l2 = std::log(std::max(s.densityLod2, 1e-4f));
    const float a = l0 + (l1 - l0) * t0;
    return std::exp(a + (l2 - a) * t1);
}

void ApplyGrassPreset(GrassSettings& target, GrassPreset preset) noexcept
{
    const GrassSettings p = MakeGrassPreset(preset);
    target.densityLod0 = p.densityLod0;
    target.densityLod1 = p.densityLod1;
    target.densityLod2 = p.densityLod2;
    target.lod0Distance = p.lod0Distance;
    target.lod1Distance = p.lod1Distance;
    target.lod2Distance = p.lod2Distance;
    target.instanceCapLod0 = p.instanceCapLod0;
    target.instanceCapLod1 = p.instanceCapLod1;
    target.instanceCapLod2 = p.instanceCapLod2;
    target.gridCells = p.gridCells;
    target.heightTileTexelSize = p.heightTileTexelSize;
    target.heightTileTexels = p.heightTileTexels;
}

const char* GrassPresetName(GrassPreset preset) noexcept
{
    switch (preset) {
    case GrassPreset::Lean: return "Lean";
    case GrassPreset::Medium: return "Medium";
    case GrassPreset::Big: return "Big";
    case GrassPreset::Huge: return "Huge";
    case GrassPreset::Count: break;
    }
    return "Big";
}

GrassGrid MakeGrassGrid(double eyeX, double eyeZ, std::uint32_t cells) noexcept
{
    GrassGrid grid;
    grid.cells = cells;
    const double half = static_cast<double>(cells / 2);
    grid.originChunkX = static_cast<std::int32_t>(std::floor(eyeX / kGrassChunkSizeM) - half);
    grid.originChunkZ = static_cast<std::int32_t>(std::floor(eyeZ / kGrassChunkSizeM) - half);
    return grid;
}

void ExtractGrassFrustumPlanes(const Mat4& m, float (&planes)[6][4]) noexcept
{
    // Gribb-Hartmann on rows of a clip = M * p matrix with depth 0..1: left/right/bottom/top = r3 +- r0/r1, near = r2, far = r3 - r2.
    const auto row = [&](int r, int c) { return m(r, c); };
    float raw[6][4];
    for (int c = 0; c < 4; ++c) {
        raw[0][c] = row(3, c) + row(0, c);
        raw[1][c] = row(3, c) - row(0, c);
        raw[2][c] = row(3, c) + row(1, c);
        raw[3][c] = row(3, c) - row(1, c);
        raw[4][c] = row(2, c);
        raw[5][c] = row(3, c) - row(2, c);
    }
    for (int p = 0; p < 6; ++p) {
        const float length = std::sqrt(raw[p][0] * raw[p][0] + raw[p][1] * raw[p][1] + raw[p][2] * raw[p][2]);
        const float inv = length > 1e-12f ? 1.0f / length : 0.0f;
        for (int c = 0; c < 4; ++c) {
            planes[p][c] = raw[p][c] * inv;
        }
    }
}

std::uint64_t GrassInstanceBytes(const GrassSettings& s) noexcept
{
    return (std::uint64_t{ s.instanceCapLod0 } + s.instanceCapLod1 + s.instanceCapLod2) * kGrassInstanceBytes;
}

std::uint32_t GrassSlicesPerChunk(float densityLod0) noexcept
{
    constexpr std::uint32_t kCandidatesPerGroup = 2048;
    const std::uint32_t candidates = GrassCandidatesPerChunk(densityLod0);
    return std::clamp((candidates + kCandidatesPerGroup - 1u) / kCandidatesPerGroup, 1u, 16u);
}

GrassParamsGpu MakeGrassParams(const GrassSettings& s, const GrassFrameInputs& in) noexcept
{
    GrassParamsGpu p{};
    std::memcpy(p.viewProjection, in.viewProjection.Data(), sizeof(p.viewProjection));
    std::memcpy(p.drawViewProjection, in.drawViewProjection.Data(), sizeof(p.drawViewProjection));
    ExtractGrassFrustumPlanes(in.viewProjection, p.planes);

    const GrassGrid grid = MakeGrassGrid(in.eyeX, in.eyeZ, s.gridCells);
    const double gridX = static_cast<double>(grid.originChunkX) * kGrassChunkSizeM;
    const double gridZ = static_cast<double>(grid.originChunkZ) * kGrassChunkSizeM;
    Set4(p.camRel, static_cast<float>(in.eyeX - gridX), static_cast<float>(in.eyeY), static_cast<float>(in.eyeZ - gridZ),
        kGrassChunkSizeM);

    std::int32_t flags = 0;
    if (in.hizUsable && s.hizCulling) {
        flags |= kGrassFlagHiz;
    }
    if (s.collectStats) {
        flags |= kGrassFlagStats;
    }
    p.grid[0] = grid.originChunkX;
    p.grid[1] = grid.originChunkZ;
    p.grid[2] = static_cast<std::int32_t>(grid.cells);
    p.grid[3] = flags;

    const GrassHeightTile* tile = in.tile;
    if (tile != nullptr && tile->Valid()) {
        Set4(p.tile, static_cast<float>(tile->originX - gridX), static_cast<float>(tile->originZ - gridZ), tile->texelSize,
            static_cast<float>(tile->size));
        Set4(p.water, tile->seaLevel, s.waterMargin, s.waterFadeBand, s.sandHeight);
    } else {
        Set4(p.tile, 0.0f, 0.0f, 1.0f, 0.0f);
        Set4(p.water, 0.0f, s.waterMargin, s.waterFadeBand, s.sandHeight);
    }

    Set4(p.lodDist, s.lod0Distance, s.lod1Distance, s.lod2Distance, s.edgeFade);
    Set4(p.density, s.densityLod0, s.densityLod1, s.densityLod2, s.patchiness);
    Set4(p.slope, s.maxSlopeDeg, s.slopeFadeDeg, s.patchScaleM, s.bareThreshold);
    Set4(p.blade, s.bladeHeight, s.bladeWidth, s.bladeTaper, s.heightVariation);
    Set4(p.bladeB, s.lean, s.widthCompensation, s.maxWidthScale, s.maxBend);
    Set4(p.wind0, in.windDirX, in.windDirZ, in.windSpeed, in.timeSeconds);
    Set4(p.wind1, s.gustStrength, static_cast<float>(in.windOffsetX), static_cast<float>(in.windOffsetZ), s.windBend);
    Set4(p.wind2, s.flutterSpeed, s.flutterStrength, s.windWaveLengthM, s.windGustLengthM);
    Set4(p.sunDir, in.sunDirection[0], in.sunDirection[1], in.sunDirection[2], s.translucency);
    Set4(p.sunRadiance, in.sunRadiance[0], in.sunRadiance[1], in.sunRadiance[2], s.sunVisibility);
    Set4(p.skyAmbient, in.skyAmbient[0], in.skyAmbient[1], in.skyAmbient[2], s.cloudFill);
    const float shadowFrustumPad = (s.castShadows && in.shadowCasting) ? s.shadowPadM : 0.0f;
    Set4(p.groundAmbient, in.groundAmbient[0], in.groundAmbient[1], in.groundAmbient[2], shadowFrustumPad);
    Set4(p.colorRoot, s.colorRoot[0], s.colorRoot[1], s.colorRoot[2], s.rootAo);
    Set4(p.colorTip, s.colorTip[0], s.colorTip[1], s.colorTip[2], s.tintVariation);
    Set4(p.cameraWorld, static_cast<float>(in.eyeX), static_cast<float>(in.eyeZ), s.alpineHeight, s.alpineFade);

    p.instanceBase[0] = 0;
    p.instanceBase[1] = s.instanceCapLod0;
    p.instanceBase[2] = s.instanceCapLod0 + s.instanceCapLod1;
    p.instanceBase[3] = 0;
    p.instanceCap[0] = s.instanceCapLod0;
    p.instanceCap[1] = s.instanceCapLod1;
    p.instanceCap[2] = s.instanceCapLod2;
    p.instanceCap[3] = 0;
    p.misc[0] = static_cast<std::uint32_t>(s.debugView);
    p.misc[1] = s.seed;
    p.misc[2] = 0;
    p.misc[3] = 0;

    Set4(p.hiz, in.hizAtlas[0], in.hizAtlas[1], in.hizBase[0], in.hizBase[1]);
    Set4(p.depthMap, in.hizDepthK, in.hizDepthB, in.hizLevels, 0.0f);
    Set4(p.look, s.terrainTint, s.groundNormalBlend, s.selfShadow, static_cast<float>(s.lod0Segments));
    Set4(p.extra, s.densityBlend, s.farHeightFade, s.farTerrainBlend, static_cast<float>(GrassSlicesPerChunk(s.densityLod0)));
    Set4(p.densityLog, std::log(std::max(s.densityLod0, 1e-4f)), std::log(std::max(s.densityLod1, 1e-4f)),
        std::log(std::max(s.densityLod2, 1e-4f)), s.densityLod0 > 0.0f ? 1.0f : 0.0f);
    Set4(p.variety, s.clumpStrength, s.minPatchDensity, s.weedShare, s.dryShare);
    Set4(p.variety2, s.flowerShare, s.lookModern, s.jitterStrength, s.patchTint);
    return p;
}
GrassShadowParamsGpu MakeGrassShadowParams(const SunShadowReceiver* receiver, const CloudShadowMap* cloud,
    const Vec3& sunDir, const GrassSettings& settings) noexcept
{
    constexpr float kSunMatchMinDot = 0.9999f;
    constexpr float kGrassSunOffsetTexels = 1.0f;

    GrassShadowParamsGpu p{};
    const float strengthSetting = Clamp(settings.shadowStrength, 1.0f, 0.0f, 1.0f);
    if (!(strengthSetting > 0.0f)) {
        return p;
    }

    const bool sunValid = std::isfinite(sunDir.x) && std::isfinite(sunDir.y) && std::isfinite(sunDir.z)
        && LengthSquared(sunDir) > 1e-12f;
    const Vec3 liveSun = sunValid ? Normalize(sunDir) : Vec3::Up();

    if (settings.receiveCloudShadow && cloud != nullptr && CloudShadowMapUsable(*cloud)
        && std::isfinite(cloud->strength) && cloud->strength > 0.0f && std::isfinite(cloud->sunDirection.x)
        && std::isfinite(cloud->sunDirection.z) && LengthSquared(cloud->sunDirection) > 1e-12f) {
        const Vec3 cloudSun = Normalize(cloud->sunDirection);
        Set4(p.cloudGrid, cloud->centerX, cloud->centerZ, 1.0f / cloud->extentM,
            std::clamp(cloud->strength * strengthSetting, 0.0f, 1.0f));
        Set4(p.cloudLayer, cloud->layerBaseM, cloud->layerTopM, 1.0f, static_cast<float>(kCloudShadowMapSize));
        Set4(p.cloudSun, cloudSun.x, cloudSun.y, cloudSun.z, 0.0f);
    }

    if (!settings.receiveSunShadow || !sunValid || receiver == nullptr || !SunShadowReceiverUsable(*receiver)
        || !std::isfinite(receiver->materialStrength) || !(receiver->materialStrength > 0.0f)
        || !std::isfinite(receiver->directionTowardSun.x) || !std::isfinite(receiver->directionTowardSun.y)
        || !std::isfinite(receiver->directionTowardSun.z) || LengthSquared(receiver->directionTowardSun) <= 1e-12f
        || Dot(Normalize(receiver->directionTowardSun), liveSun) < kSunMatchMinDot) {
        return p;
    }

    const std::uint32_t count = std::min(receiver->cascadeCount, kSunShadowReceiverMaxCascades);
    for (std::uint32_t c = 0; c < count; ++c) {
        std::memcpy(p.cascadeViewProjection[c], receiver->viewProjection[c].Data(), sizeof(p.cascadeViewProjection[c]));
        p.cascadeSplitFar[c] = receiver->splitFar[c];
        p.cascadeBlendWidth[c] = std::max(Finite(receiver->blendWidth[c], 0.0f), 0.0f);
        p.cascadeTexelWorld[c] = receiver->texelWorld[c];
        p.cascadeDepthRange[c] = receiver->depthRange[c];
    }
    for (std::uint32_t c = count; c < kSunShadowReceiverMaxCascades; ++c) {
        p.cascadeSplitFar[c] = receiver->splitFar[count - 1u];
    }

    const float fadeEnd = std::isfinite(receiver->shadowDistance) && receiver->shadowDistance > 0.0f
        ? receiver->shadowDistance
        : receiver->splitFar[count - 1u];
    const float fadeStart = Clamp(receiver->fadeStart, 0.0f, 0.0f, fadeEnd);
    Set4(p.eyeAndFadeStart, receiver->cameraPosition.x, receiver->cameraPosition.y, receiver->cameraPosition.z, fadeStart);
    Set4(p.forwardAndFadeEnd, receiver->cameraForward.x, receiver->cameraForward.y, receiver->cameraForward.z, fadeEnd);
    const float invMap = 1.0f / static_cast<float>(receiver->resolution);
    const float effStrength = std::clamp(receiver->materialStrength * strengthSetting, 0.0f, 1.0f);
    Set4(p.shadowParams, effStrength, Clamp(receiver->constBiasTexels, 1.5f, 0.0f, 32.0f),
        Clamp(receiver->filterRadiusTexels, 1.5f, 0.0f, 16.0f), invMap);
    Set4(p.biasExtra, Clamp(receiver->slopeBiasTexels, 1.0f, 0.0f, 32.0f),
        Clamp(receiver->normalOffsetTexels, 1.0f, 0.0f, 32.0f), kGrassSunOffsetTexels, 0.0f);
    p.counts[0] = count;
    p.counts[1] = std::clamp(receiver->filterSamples, 1u, 8u);
    return p;
}

GrassShadowCastPlan MakeGrassShadowCastPlan(const SunShadowReceiver* receiver, const Vec3& sunDir,
    double eyeX, double eyeY, double eyeZ, const GrassSettings& settings) noexcept
{
    constexpr float kSunMatchMinDot = 0.9999f;
    GrassShadowCastPlan plan{};
    if (!settings.castShadows || receiver == nullptr || !SunShadowReceiverUsable(*receiver)
        || !std::isfinite(eyeX) || !std::isfinite(eyeY) || !std::isfinite(eyeZ)) {
        return plan;
    }
    const bool sunValid = std::isfinite(sunDir.x) && std::isfinite(sunDir.y) && std::isfinite(sunDir.z)
        && LengthSquared(sunDir) > 1e-12f;
    if (!sunValid || !std::isfinite(receiver->directionTowardSun.x)
        || !std::isfinite(receiver->directionTowardSun.y) || !std::isfinite(receiver->directionTowardSun.z)
        || LengthSquared(receiver->directionTowardSun) <= 1e-12f
        || Dot(Normalize(receiver->directionTowardSun), Normalize(sunDir)) < kSunMatchMinDot) {
        return plan;
    }

    const float maxTexelM = Clamp(settings.shadowMaxTexelM, 0.40f, 0.01f, 4.0f);
    const float biasTexels = Clamp(settings.shadowBiasTexels, 1.0f, -4.0f, 8.0f);
    const float widthScale = Clamp(settings.shadowWidthScale, 1.5f, 0.0f, 8.0f);
    const float maxSliceStart = std::max(Finite(settings.lod1Distance, 120.0f), 0.0f);
    const Mat4d eyeTranslation = Mat4d::Translation(eyeX, eyeY, eyeZ);

    const std::uint32_t cascadeCount = std::min(receiver->cascadeCount, kSunShadowReceiverMaxCascades);
    for (std::uint32_t c = 0; c < cascadeCount; ++c) {
        const float texel = receiver->texelWorld[c];
        if (!(texel > 0.0f) || texel > maxTexelM) {
            continue;
        }
        const float sliceStart = c == 0
            ? 0.0f
            : std::max(0.0f, receiver->splitFar[c - 1u] - std::max(Finite(receiver->blendWidth[c - 1u], 0.0f), 0.0f));
        if (sliceStart >= maxSliceStart) {
            continue;
        }
        const Mat4d vpRel = Mat4d::FromMat4(receiver->viewProjection[c]) * eyeTranslation;
        if (!vpRel.IsFinite()) {
            continue;
        }
        GrassShadowCascadeDraw& dst = plan.cascades[plan.count++];
        dst.cascadeIndex = c;
        dst.viewProjectionRelative = vpRel.ToMat4();
        dst.minWidth = widthScale * texel;
        dst.biasM = biasTexels * texel;
    }
    return plan;
}


float GrassSyntheticHeight(const GrassSyntheticSpec& spec, double x, double z) noexcept
{
    float h = spec.baseHeight;
    if (spec.kind != GrassSyntheticKind::Flat) {
        const double k = 2.0 * kPiD / std::max(spec.wavelength, 1.0f);
        const double a = std::sin(x * k) * std::cos(z * k * 1.3);
        const double b = std::sin((x + z) * k * 2.7 + 1.0);
        h += spec.amplitude * static_cast<float>(0.6 * a + 0.25 * b);
    }
    if (spec.kind == GrassSyntheticKind::Cliff) {
        h += 25.0f * Smooth(spec.cliffX, spec.cliffX + 12.0f, static_cast<float>(x));
    }
    if (spec.lake) {
        const double dx = x - spec.lakeX;
        const double dz = z - spec.lakeZ;
        const float d = static_cast<float>(std::sqrt(dx * dx + dz * dz)) / std::max(spec.lakeRadius, 1.0f);
        if (d < 1.0f) {
            const float bowl = spec.lakeLevel - spec.lakeDepth * (1.0f - d * d);
            h = bowl + (h - bowl) * Smooth(0.55f, 1.0f, d);
        }
    }
    return h;
}

GrassHeightTile MakeSyntheticGrassTile(const GrassSyntheticSpec& spec, double centerX, double centerZ, std::uint32_t size,
    float texelSize, std::uint64_t revision)
{
    GrassHeightTile tile;
    tile.size = size;
    tile.texelSize = texelSize;
    const double span = static_cast<double>(size) * texelSize;
    tile.originX = std::floor((centerX - span * 0.5) / texelSize) * texelSize;
    tile.originZ = std::floor((centerZ - span * 0.5) / texelSize) * texelSize;
    tile.seaLevel = spec.seaLevel;
    tile.revision = revision;
    tile.data.assign(static_cast<std::size_t>(size) * size * kGrassTileStride, 0.0f);
    for (std::uint32_t j = 0; j < size; ++j) {
        for (std::uint32_t i = 0; i < size; ++i) {
            const double x = tile.originX + static_cast<double>(i) * texelSize;
            const double z = tile.originZ + static_cast<double>(j) * texelSize;
            const float h = GrassSyntheticHeight(spec, x, z);
            bool wet = false;
            if (spec.lake) {
                const double dx = x - spec.lakeX;
                const double dz = z - spec.lakeZ;
                wet = std::sqrt(dx * dx + dz * dz) < spec.lakeRadius && h <= spec.lakeLevel + 0.05f;
            }
            const std::size_t at = (static_cast<std::size_t>(j) * size + i) * kGrassTileStride;
            tile.data[at] = h;
            tile.data[at + 1] = GrassTileFlagsColor(wet, GrassPackTerrainColor(0.05f, 0.12f, 0.03f));
        }
    }
    return tile;
}

bool SampleGrassTile(const GrassHeightTile& tile, double x, double z, float& height, float& submerged) noexcept
{
    if (!tile.Valid()) {
        return false;
    }
    const double u = (x - tile.originX) / static_cast<double>(tile.texelSize);
    const double v = (z - tile.originZ) / static_cast<double>(tile.texelSize);
    const double last = static_cast<double>(tile.size - 1);
    if (u < 0.0 || v < 0.0 || u > last || v > last) {
        return false;
    }
    const auto i0 = std::min(static_cast<std::uint32_t>(u), tile.size - 2);
    const auto j0 = std::min(static_cast<std::uint32_t>(v), tile.size - 2);
    const auto fu = static_cast<float>(u - i0);
    const auto fv = static_cast<float>(v - j0);
    const auto at = [&](std::uint32_t i, std::uint32_t j, std::uint32_t channel) {
        const float value = tile.data[(static_cast<std::size_t>(j) * tile.size + i) * kGrassTileStride + channel];
        return channel == 1 ? (GrassTileSubmerged(value) ? 1.0f : 0.0f) : value;
    };
    const auto lerp2 = [&](std::uint32_t channel) {
        const float a = at(i0, j0, channel) + fu * (at(i0 + 1, j0, channel) - at(i0, j0, channel));
        const float b = at(i0, j0 + 1, channel) + fu * (at(i0 + 1, j0 + 1, channel) - at(i0, j0 + 1, channel));
        return a + fv * (b - a);
    };
    height = lerp2(0);
    submerged = lerp2(1);
    return true;
}

} // namespace gte
