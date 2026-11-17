#pragma once

// All tunables of the procedural world in one flat, copyable struct (no pointers, JSON/file friendly).

#include <cstdint>

namespace gte::terrain {

struct TerrainSettings {
    // --- Shape: heights are metres, sea level is 0 ---
    std::uint32_t seed = 1337;
    float heightScale = 1200.0f;     // Highest possible peak above sea level, metres.
    float baseFrequency = 0.0003f;   // Master frequency (1/m) of the mountain layer; other layers scale from it.
    float domainWarpStrength = 220.0f; // Metres of coordinate offset; 0 disables the warp.
    float domainWarpFrequency = 0.0006f;
    std::int32_t octaves = 6;        // Peaks-and-valleys octaves.
    float gain = 0.42f;
    float lacunarity = 2.0f;

    // --- Continentalness: ocean versus land ---
    float continentalFrequencyScale = 0.12f; // Multiplier of baseFrequency.
    float continentalSeaLevel = -0.05f;      // Noise value (-1..1) at the coast; higher means more ocean.
    float continentalShaping = 0.25f;        // Coast width in noise units; small is a cliff, large is a beach.

    // --- Peaks and valleys (ridged) ---
    float peaksFrequencyScale = 1.0f;
    float peaksWeight = 1.0f;        // 0 gives flat land, 1 full mountains.

    // --- Fine detail ---
    float detailFrequencyScale = 60.0f;
    float detailAmplitude = 1.5f;    // Metres. Fades out by itself on coarse LOD levels.
    std::int32_t detailOctaves = 3;

    // --- Hydraulic erosion (droplets) ---
    bool erosionEnabled = true;
    std::int32_t erosionDropletCount = 1800; // Per standard 96x96 sample tile; scaled by tile area.
    float erosionRate = 0.3f;
    float depositionRate = 0.3f;
    float sedimentCapacity = 4.0f;
    float erosionInertia = 0.05f;
    float erosionEvaporation = 0.01f;
    float erosionGravity = 4.0f;
    std::int32_t erosionMaxLifetime = 30;
    std::int32_t erosionBrushRadius = 2;
    std::int32_t erosionMaxLevel = 1;        // LOD levels above this skip erosion.
    std::int32_t erosionEdgeFalloff = 8;     // Samples from the node border where erosion fades to zero.
    float erosionStrength = 1.5f;            // Multiplier on the carved and deposited height; 1 is the raw simulation.

    // --- Material thresholds ---
    float grassMaxSlopeDeg = 32.0f;
    float rockMinSlopeDeg = 42.0f;
    float snowMinHeightFraction = 0.55f;     // Of heightScale.
    float snowSlopeLimitDeg = 58.0f;         // Snow does not stick on faces steeper than this.
    float sandMaxHeight = 7.0f;              // Metres above sea level that still counts as beach.

    // --- Geometry and LOD ---
    std::int32_t chunkQuads = 64;            // Quads per node side; vertices per side = chunkQuads + 1.
    float lod0Spacing = 2.0f;                // Metres between vertices on the finest level.
    std::int32_t lodLevelCount = 8;          // Levels 0..count-1; the coarsest level are the roots.
    float lodDistanceFactor = 2.0f;          // A node splits when the camera is closer than factor * node size.
    std::int32_t worldHalfExtentChunks = 128; // Half world size in finest-level nodes.

    // --- Optimisation ---
    bool frustumCulling = true;
    bool hiZCulling = true;
    float maxViewDistance = 12000.0f;        // Metres.

    friend bool operator==(const TerrainSettings&, const TerrainSettings&) = default;
};

// Which kind of rebuild a change of settings needs. Values are bit flags.
enum class SettingsChange : std::uint32_t {
    None = 0,
    Height = 1u << 0,       // Heights differ: regenerate every node.
    Geometry = 1u << 1,     // Node layout differs: regenerate every node and resize pools.
    Material = 1u << 2,     // Only colours differ: recolour cached nodes.
    Optimization = 1u << 3, // Only culling and distance differ: nothing to rebuild.
};

constexpr SettingsChange operator|(SettingsChange a, SettingsChange b) noexcept
{
    return static_cast<SettingsChange>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
constexpr SettingsChange& operator|=(SettingsChange& a, SettingsChange b) noexcept
{
    return a = a | b;
}
constexpr bool HasChange(SettingsChange set, SettingsChange flag) noexcept
{
    return (static_cast<std::uint32_t>(set) & static_cast<std::uint32_t>(flag)) != 0;
}

// Clamps every field into a safe range; NaN and infinity fall back to the default value.
TerrainSettings Sanitize(const TerrainSettings& in) noexcept;

// Stable hash over every field (FNV-1a on the value bits).
std::uint64_t Hash(const TerrainSettings& s) noexcept;

// What `to` needs compared to `from`. Both are expected to be sanitized.
SettingsChange ClassifyChange(const TerrainSettings& from, const TerrainSettings& to) noexcept;

// Derived layout numbers shared by every terrain module.
struct TerrainLayout {
    std::int32_t quads = 64;
    std::int32_t verticesPerSide = 65;
    double nodeSizeLevel0 = 128.0;    // Metres covered by one finest-level node.
    double spacingLevel0 = 2.0;
    std::int32_t rootLevel = 7;
    double worldHalfExtent = 16384.0; // Metres.
};

TerrainLayout MakeLayout(const TerrainSettings& s) noexcept;

// Side length in metres of a node at `level`.
inline double NodeSize(const TerrainLayout& layout, int level) noexcept
{
    return layout.nodeSizeLevel0 * static_cast<double>(1u << level);
}

// Vertex spacing in metres at `level`.
inline double NodeSpacing(const TerrainLayout& layout, int level) noexcept
{
    return layout.spacingLevel0 * static_cast<double>(1u << level);
}

// Lowest and highest height the generator can output, metres.
inline float MinHeight(const TerrainSettings& s) noexcept { return -0.25f * s.heightScale; }
inline float MaxHeight(const TerrainSettings& s) noexcept { return s.heightScale; }

} // namespace gte::terrain
