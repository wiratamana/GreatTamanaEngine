#include "TerrainSettings.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace gte::terrain {

namespace {

float ClampF(float v, float lo, float hi, float fallback) noexcept
{
    if (!std::isfinite(v)) {
        return fallback;
    }
    return std::clamp(v, lo, hi);
}

std::int32_t ClampI(std::int32_t v, std::int32_t lo, std::int32_t hi) noexcept
{
    return std::clamp(v, lo, hi);
}

std::int32_t RoundChunkQuads(std::int32_t q) noexcept
{
    if (q <= 24) {
        return 16;
    }
    if (q <= 48) {
        return 32;
    }
    return q <= 96 ? 64 : 128;
}

// Hash helper: feeds the raw value bits of every field in a fixed order.
struct Fnv {
    std::uint64_t h = 1469598103934665603ull;
    void Bytes(std::uint64_t v) noexcept
    {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (i * 8)) & 0xFFu;
            h *= 1099511628211ull;
        }
    }
    void Add(float v) noexcept { Bytes(std::bit_cast<std::uint32_t>(v)); }
    void Add(std::uint32_t v) noexcept { Bytes(v); }
    void Add(std::int32_t v) noexcept { Bytes(static_cast<std::uint32_t>(v)); }
    void Add(bool v) noexcept { Bytes(v ? 1u : 0u); }
};

} // namespace

TerrainSettings Sanitize(const TerrainSettings& in) noexcept
{
    const TerrainSettings d{};
    TerrainSettings s = in;
    s.heightScale = ClampF(s.heightScale, 10.0f, 9000.0f, d.heightScale);
    s.baseFrequency = ClampF(s.baseFrequency, 1.0e-6f, 0.05f, d.baseFrequency);
    s.domainWarpStrength = ClampF(s.domainWarpStrength, 0.0f, 5000.0f, d.domainWarpStrength);
    s.domainWarpFrequency = ClampF(s.domainWarpFrequency, 1.0e-6f, 0.1f, d.domainWarpFrequency);
    s.octaves = ClampI(s.octaves, 1, 10);
    s.gain = ClampF(s.gain, 0.05f, 0.95f, d.gain);
    s.lacunarity = ClampF(s.lacunarity, 1.2f, 4.0f, d.lacunarity);

    s.continentalFrequencyScale = ClampF(s.continentalFrequencyScale, 0.005f, 4.0f, d.continentalFrequencyScale);
    s.continentalSeaLevel = ClampF(s.continentalSeaLevel, -0.9f, 0.9f, d.continentalSeaLevel);
    s.continentalShaping = ClampF(s.continentalShaping, 0.02f, 1.0f, d.continentalShaping);

    s.peaksFrequencyScale = ClampF(s.peaksFrequencyScale, 0.05f, 8.0f, d.peaksFrequencyScale);
    s.peaksWeight = ClampF(s.peaksWeight, 0.0f, 1.5f, d.peaksWeight);

    s.detailFrequencyScale = ClampF(s.detailFrequencyScale, 1.0f, 500.0f, d.detailFrequencyScale);
    s.detailAmplitude = ClampF(s.detailAmplitude, 0.0f, 200.0f, d.detailAmplitude);
    s.detailOctaves = ClampI(s.detailOctaves, 1, 6);

    s.erosionDropletCount = ClampI(s.erosionDropletCount, 0, 200000);
    s.erosionRate = ClampF(s.erosionRate, 0.0f, 1.0f, d.erosionRate);
    s.depositionRate = ClampF(s.depositionRate, 0.0f, 1.0f, d.depositionRate);
    s.sedimentCapacity = ClampF(s.sedimentCapacity, 0.1f, 32.0f, d.sedimentCapacity);
    s.erosionInertia = ClampF(s.erosionInertia, 0.0f, 0.95f, d.erosionInertia);
    s.erosionEvaporation = ClampF(s.erosionEvaporation, 0.0f, 0.5f, d.erosionEvaporation);
    s.erosionGravity = ClampF(s.erosionGravity, 0.1f, 32.0f, d.erosionGravity);
    s.erosionMaxLifetime = ClampI(s.erosionMaxLifetime, 2, 200);
    s.erosionBrushRadius = ClampI(s.erosionBrushRadius, 1, 6);
    s.erosionMaxLevel = ClampI(s.erosionMaxLevel, 0, 11);
    s.erosionEdgeFalloff = ClampI(s.erosionEdgeFalloff, 2, 16);
    s.erosionStrength = ClampF(s.erosionStrength, 0.0f, 32.0f, d.erosionStrength);

    s.grassMaxSlopeDeg = ClampF(s.grassMaxSlopeDeg, 0.0f, 89.0f, d.grassMaxSlopeDeg);
    s.rockMinSlopeDeg = ClampF(s.rockMinSlopeDeg, s.grassMaxSlopeDeg, 90.0f, d.rockMinSlopeDeg);
    s.snowMinHeightFraction = ClampF(s.snowMinHeightFraction, 0.0f, 1.2f, d.snowMinHeightFraction);
    s.snowSlopeLimitDeg = ClampF(s.snowSlopeLimitDeg, 10.0f, 90.0f, d.snowSlopeLimitDeg);
    s.sandMaxHeight = ClampF(s.sandMaxHeight, 0.0f, 200.0f, d.sandMaxHeight);

    s.chunkQuads = RoundChunkQuads(s.chunkQuads);
    s.lod0Spacing = ClampF(s.lod0Spacing, 0.25f, 64.0f, d.lod0Spacing);
    s.lodLevelCount = ClampI(s.lodLevelCount, 1, 12);
    s.lodDistanceFactor = ClampF(s.lodDistanceFactor, 1.5f, 16.0f, d.lodDistanceFactor);
    const std::int32_t rootSpan = 1 << (s.lodLevelCount - 1);
    const std::int32_t half = ClampI(s.worldHalfExtentChunks, rootSpan, 1 << 14);
    s.worldHalfExtentChunks = ((half + rootSpan - 1) / rootSpan) * rootSpan;

    s.maxViewDistance = ClampF(s.maxViewDistance, 50.0f, 200000.0f, d.maxViewDistance);
    return s;
}

std::uint64_t Hash(const TerrainSettings& s) noexcept
{
    Fnv f;
    f.Add(s.seed); f.Add(s.heightScale); f.Add(s.baseFrequency);
    f.Add(s.domainWarpStrength); f.Add(s.domainWarpFrequency);
    f.Add(s.octaves); f.Add(s.gain); f.Add(s.lacunarity);
    f.Add(s.continentalFrequencyScale); f.Add(s.continentalSeaLevel); f.Add(s.continentalShaping);
    f.Add(s.peaksFrequencyScale); f.Add(s.peaksWeight);
    f.Add(s.detailFrequencyScale); f.Add(s.detailAmplitude); f.Add(s.detailOctaves);
    f.Add(s.erosionEnabled); f.Add(s.erosionDropletCount); f.Add(s.erosionRate); f.Add(s.depositionRate);
    f.Add(s.sedimentCapacity); f.Add(s.erosionInertia); f.Add(s.erosionEvaporation); f.Add(s.erosionGravity);
    f.Add(s.erosionMaxLifetime); f.Add(s.erosionBrushRadius); f.Add(s.erosionMaxLevel); f.Add(s.erosionEdgeFalloff);
    f.Add(s.erosionStrength);
    f.Add(s.grassMaxSlopeDeg); f.Add(s.rockMinSlopeDeg); f.Add(s.snowMinHeightFraction);
    f.Add(s.snowSlopeLimitDeg); f.Add(s.sandMaxHeight);
    f.Add(s.chunkQuads); f.Add(s.lod0Spacing); f.Add(s.lodLevelCount); f.Add(s.lodDistanceFactor);
    f.Add(s.worldHalfExtentChunks);
    f.Add(s.frustumCulling); f.Add(s.hiZCulling); f.Add(s.maxViewDistance);
    return f.h;
}

SettingsChange ClassifyChange(const TerrainSettings& a, const TerrainSettings& b) noexcept
{
    SettingsChange result = SettingsChange::None;

    const bool geometry = a.chunkQuads != b.chunkQuads || a.lod0Spacing != b.lod0Spacing
        || a.lodLevelCount != b.lodLevelCount || a.worldHalfExtentChunks != b.worldHalfExtentChunks;
    if (geometry) {
        result |= SettingsChange::Geometry | SettingsChange::Height;
    }

    const bool erosionDiff = a.erosionEnabled != b.erosionEnabled || a.erosionDropletCount != b.erosionDropletCount
        || a.erosionRate != b.erosionRate || a.depositionRate != b.depositionRate
        || a.sedimentCapacity != b.sedimentCapacity || a.erosionInertia != b.erosionInertia
        || a.erosionEvaporation != b.erosionEvaporation || a.erosionGravity != b.erosionGravity
        || a.erosionMaxLifetime != b.erosionMaxLifetime || a.erosionBrushRadius != b.erosionBrushRadius
        || a.erosionMaxLevel != b.erosionMaxLevel || a.erosionEdgeFalloff != b.erosionEdgeFalloff
        || a.erosionStrength != b.erosionStrength;
    const bool shapeDiff = a.seed != b.seed || a.heightScale != b.heightScale || a.baseFrequency != b.baseFrequency
        || a.domainWarpStrength != b.domainWarpStrength || a.domainWarpFrequency != b.domainWarpFrequency
        || a.octaves != b.octaves || a.gain != b.gain || a.lacunarity != b.lacunarity
        || a.continentalFrequencyScale != b.continentalFrequencyScale || a.continentalSeaLevel != b.continentalSeaLevel
        || a.continentalShaping != b.continentalShaping || a.peaksFrequencyScale != b.peaksFrequencyScale
        || a.peaksWeight != b.peaksWeight || a.detailFrequencyScale != b.detailFrequencyScale
        || a.detailAmplitude != b.detailAmplitude || a.detailOctaves != b.detailOctaves;
    if (shapeDiff || erosionDiff) {
        result |= SettingsChange::Height;
    }

    const bool materialDiff = a.grassMaxSlopeDeg != b.grassMaxSlopeDeg || a.rockMinSlopeDeg != b.rockMinSlopeDeg
        || a.snowMinHeightFraction != b.snowMinHeightFraction || a.snowSlopeLimitDeg != b.snowSlopeLimitDeg
        || a.sandMaxHeight != b.sandMaxHeight;
    if (materialDiff) {
        result |= SettingsChange::Material;
    }

    const bool optimizationDiff = a.frustumCulling != b.frustumCulling || a.hiZCulling != b.hiZCulling
        || a.lodDistanceFactor != b.lodDistanceFactor || a.maxViewDistance != b.maxViewDistance;
    if (optimizationDiff) {
        result |= SettingsChange::Optimization;
    }
    return result;
}

TerrainLayout MakeLayout(const TerrainSettings& s) noexcept
{
    TerrainLayout l;
    l.quads = s.chunkQuads;
    l.verticesPerSide = s.chunkQuads + 1;
    l.spacingLevel0 = static_cast<double>(s.lod0Spacing);
    l.nodeSizeLevel0 = static_cast<double>(s.chunkQuads) * l.spacingLevel0;
    l.rootLevel = s.lodLevelCount - 1;
    l.worldHalfExtent = static_cast<double>(s.worldHalfExtentChunks) * l.nodeSizeLevel0;
    return l;
}

} // namespace gte::terrain
