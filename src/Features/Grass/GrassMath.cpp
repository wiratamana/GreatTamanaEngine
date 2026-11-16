#include "GrassMath.h"

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

GrassSettings SanitizeGrassSettings(const GrassSettings& in) noexcept
{
    const GrassSettings d{};
    GrassSettings s = in;

    s.debugView = static_cast<GrassDebugView>(std::min<std::uint32_t>(static_cast<std::uint32_t>(in.debugView), 3u));

    s.densityLod0 = Clamp(in.densityLod0, d.densityLod0, 0.0f, 200.0f);
    s.densityLod1 = std::min(Clamp(in.densityLod1, d.densityLod1, 0.0f, 200.0f), s.densityLod0);
    s.densityLod2 = std::min(Clamp(in.densityLod2, d.densityLod2, 0.0f, 200.0f), s.densityLod1);
    s.patchiness = Clamp(in.patchiness, d.patchiness, 0.0f, 1.0f);
    s.patchScaleM = Clamp(in.patchScaleM, d.patchScaleM, 2.0f, 500.0f);
    s.bareThreshold = Clamp(in.bareThreshold, d.bareThreshold, 0.0f, 1.0f);

    s.bladeHeight = Clamp(in.bladeHeight, d.bladeHeight, 0.05f, 3.0f);
    s.bladeWidth = Clamp(in.bladeWidth, d.bladeWidth, 0.002f, 0.5f);
    s.bladeTaper = Clamp(in.bladeTaper, d.bladeTaper, 0.0f, 1.0f);
    s.heightVariation = Clamp(in.heightVariation, d.heightVariation, 0.0f, 0.9f);
    s.lean = Clamp(in.lean, d.lean, 0.0f, 1.2f);
    s.maxBend = Clamp(in.maxBend, d.maxBend, 0.1f, 1.5f);
    s.lod1WidthScale = Clamp(in.lod1WidthScale, d.lod1WidthScale, 1.0f, 8.0f);
    s.lod2WidthScale = Clamp(in.lod2WidthScale, d.lod2WidthScale, 1.0f, 16.0f);

    s.maxSlopeDeg = Clamp(in.maxSlopeDeg, d.maxSlopeDeg, 1.0f, 89.0f);
    s.slopeFadeDeg = Clamp(in.slopeFadeDeg, d.slopeFadeDeg, 0.0f, 45.0f);
    s.waterMargin = Clamp(in.waterMargin, d.waterMargin, -2.0f, 20.0f);
    s.waterFadeBand = Clamp(in.waterFadeBand, d.waterFadeBand, 0.0f, 20.0f);

    s.lod2Distance = Clamp(in.lod2Distance, d.lod2Distance, 10.0f, kMaxLod2Distance);
    s.lod1Distance = Clamp(in.lod1Distance, d.lod1Distance, 6.0f, s.lod2Distance - 4.0f);
    s.lod0Distance = Clamp(in.lod0Distance, d.lod0Distance, 2.0f, s.lod1Distance - 2.0f);
    s.edgeFade = Clamp(in.edgeFade, d.edgeFade, 0.01f, 1.0f);

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
    for (int i = 0; i < 3; ++i) {
        s.colorRoot[i] = Clamp(in.colorRoot[i], d.colorRoot[i], 0.0f, 2.0f);
        s.colorTip[i] = Clamp(in.colorTip[i], d.colorTip[i], 0.0f, 2.0f);
    }
    s.sunVisibility = Clamp(in.sunVisibility, d.sunVisibility, 0.0f, 2.0f);

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

    std::int32_t flags = kGrassFlagTriangleList;
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
        Set4(p.water, tile->seaLevel, s.waterMargin, s.waterFadeBand, 0.0f);
    } else {
        Set4(p.tile, 0.0f, 0.0f, 1.0f, 0.0f);
        Set4(p.water, 0.0f, s.waterMargin, s.waterFadeBand, 0.0f);
    }

    Set4(p.lodDist, s.lod0Distance, s.lod1Distance, s.lod2Distance, s.edgeFade);
    Set4(p.density, s.densityLod0, s.densityLod1, s.densityLod2, s.patchiness);
    Set4(p.slope, s.maxSlopeDeg, s.slopeFadeDeg, s.patchScaleM, s.bareThreshold);
    Set4(p.blade, s.bladeHeight, s.bladeWidth, s.bladeTaper, s.heightVariation);
    Set4(p.bladeB, s.lean, s.lod1WidthScale, s.lod2WidthScale, s.maxBend);
    Set4(p.wind0, in.windDirX, in.windDirZ, in.windSpeed, in.timeSeconds);
    Set4(p.wind1, s.gustStrength, static_cast<float>(in.windOffsetX), static_cast<float>(in.windOffsetZ), s.windBend);
    Set4(p.wind2, s.flutterSpeed, s.flutterStrength, s.windWaveLengthM, s.windGustLengthM);
    Set4(p.sunDir, in.sunDirection[0], in.sunDirection[1], in.sunDirection[2], s.translucency);
    Set4(p.sunRadiance, in.sunRadiance[0], in.sunRadiance[1], in.sunRadiance[2], s.sunVisibility);
    Set4(p.skyAmbient, in.skyAmbient[0], in.skyAmbient[1], in.skyAmbient[2], 0.0f);
    Set4(p.groundAmbient, in.groundAmbient[0], in.groundAmbient[1], in.groundAmbient[2], 0.0f);
    Set4(p.colorRoot, s.colorRoot[0], s.colorRoot[1], s.colorRoot[2], s.rootAo);
    Set4(p.colorTip, s.colorTip[0], s.colorTip[1], s.colorTip[2], s.tintVariation);
    Set4(p.cameraWorld, static_cast<float>(in.eyeX), static_cast<float>(in.eyeZ), 0.0f, 0.0f);

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
    return p;
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
    tile.data.resize(static_cast<std::size_t>(size) * size * 2u);
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
            const std::size_t at = (static_cast<std::size_t>(j) * size + i) * 2u;
            tile.data[at] = h;
            tile.data[at + 1] = wet ? 1.0f : 0.0f;
        }
    }
    return tile;
}

} // namespace gte
