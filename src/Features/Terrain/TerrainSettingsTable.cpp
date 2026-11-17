#include "TerrainSettingsTable.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace gte::terrain {

namespace {

using S = TerrainSettings;

constexpr FieldDesc F(const char* name, const char* label, const char* group, double lo, double hi, const char* format,
    float S::* member, bool logarithmic, const char* tip)
{
    return { name, label, group, FieldKind::Float, lo, hi, logarithmic, format, tip, member, nullptr, nullptr, nullptr };
}

constexpr FieldDesc I(const char* name, const char* label, const char* group, double lo, double hi, std::int32_t S::* member,
    const char* tip)
{
    return { name, label, group, FieldKind::Int, lo, hi, false, "%d", tip, nullptr, member, nullptr, nullptr };
}

constexpr FieldDesc B(const char* name, const char* label, const char* group, bool S::* member, const char* tip)
{
    return { name, label, group, FieldKind::Bool, 0.0, 1.0, false, "", tip, nullptr, nullptr, member, nullptr };
}

constexpr const char* kSeed = "Seed";
constexpr const char* kShape = "Height and frequency";
constexpr const char* kWarp = "Domain warp";
constexpr const char* kFractal = "Octaves, gain, lacunarity";
constexpr const char* kContinent = "Continentalness";
constexpr const char* kPeaks = "Peaks and valleys";
constexpr const char* kDetail = "Detail";
constexpr const char* kErosion = "Hydraulic erosion";
constexpr const char* kMaterial = "Materials";
constexpr const char* kGeometry = "Geometry and LOD";
constexpr const char* kOptimization = "Optimization";

constexpr std::array<const char*, 11> kGroups = { kSeed, kShape, kWarp, kFractal, kContinent, kPeaks, kDetail, kErosion, kMaterial,
    kGeometry, kOptimization };

constexpr std::array<std::int32_t, 4> kChunkQuads = { 16, 32, 64, 128 };

constexpr FieldDesc kFields[] = {
    { "seed", "Seed", kSeed, FieldKind::Seed, 0.0, 4294967295.0, false, "%u", "Same seed gives the same world.", nullptr, nullptr,
        nullptr, &S::seed },

    F("heightScale", "Height scale (m)", kShape, 50.0, 6000.0, "%.0f", &S::heightScale, false, "Highest possible peak above sea level."),
    F("baseFrequency", "Base frequency", kShape, 0.00005, 0.003, "%.5f", &S::baseFrequency, true,
        "Master frequency of the mountains (1/m). Lower is bigger mountains."),

    F("domainWarpStrength", "Warp strength (m)", kWarp, 0.0, 1500.0, "%.0f", &S::domainWarpStrength, false,
        "How far the coordinates are pushed. 0 turns the warp off."),
    F("domainWarpFrequency", "Warp frequency", kWarp, 0.0001, 0.005, "%.5f", &S::domainWarpFrequency, true, "Size of the warp swirls."),

    I("octaves", "Octaves", kFractal, 1.0, 10.0, &S::octaves, "Layers of the ridged mountain noise."),
    F("gain", "Gain", kFractal, 0.1, 0.9, "%.2f", &S::gain, false, "Strength of each next octave."),
    F("lacunarity", "Lacunarity", kFractal, 1.5, 3.5, "%.2f", &S::lacunarity, false, "Frequency step between octaves."),

    F("continentalFrequencyScale", "Frequency scale", kContinent, 0.01, 1.0, "%.3f", &S::continentalFrequencyScale, true,
        "Size of oceans and land masses (times base frequency)."),
    F("continentalSeaLevel", "Sea level (noise)", kContinent, -0.6, 0.6, "%.2f", &S::continentalSeaLevel, false,
        "Higher means more ocean."),
    F("continentalShaping", "Coast width", kContinent, 0.02, 1.0, "%.2f", &S::continentalShaping, false,
        "Small is a cliff coast, large is a beach."),

    F("peaksFrequencyScale", "Frequency scale", kPeaks, 0.1, 4.0, "%.2f", &S::peaksFrequencyScale, true,
        "Frequency of the ridges (times base frequency)."),
    F("peaksWeight", "Weight", kPeaks, 0.0, 1.5, "%.2f", &S::peaksWeight, false, "0 is flat land, 1 is full mountains."),

    F("detailFrequencyScale", "Frequency scale", kDetail, 5.0, 300.0, "%.0f", &S::detailFrequencyScale, true,
        "Small bumps. Higher is finer."),
    F("detailAmplitude", "Amplitude (m)", kDetail, 0.0, 50.0, "%.1f", &S::detailAmplitude, false, "Fades out on far nodes."),
    I("detailOctaves", "Octaves", kDetail, 1.0, 6.0, &S::detailOctaves, "Layers of the detail noise."),

    B("erosionEnabled", "Enabled", kErosion, &S::erosionEnabled, "Droplet erosion on the finest levels."),
    I("erosionDropletCount", "Droplets per tile", kErosion, 0.0, 20000.0, &S::erosionDropletCount,
        "Rain drops per 96x96 tile. More is slower."),
    F("erosionRate", "Erosion rate", kErosion, 0.0, 1.0, "%.2f", &S::erosionRate, false, "How fast a drop carves."),
    F("depositionRate", "Deposition rate", kErosion, 0.0, 1.0, "%.2f", &S::depositionRate, false, "How fast sediment is dropped."),
    F("sedimentCapacity", "Sediment capacity", kErosion, 0.1, 16.0, "%.1f", &S::sedimentCapacity, false,
        "How much a drop can carry."),
    F("erosionInertia", "Inertia", kErosion, 0.0, 0.9, "%.2f", &S::erosionInertia, false, "Drops keep their direction."),
    F("erosionEvaporation", "Evaporation", kErosion, 0.0, 0.2, "%.3f", &S::erosionEvaporation, false, "Water lost per step."),
    F("erosionGravity", "Gravity", kErosion, 0.5, 16.0, "%.1f", &S::erosionGravity, false, "Speed gained on slopes."),
    I("erosionMaxLifetime", "Max lifetime", kErosion, 5.0, 100.0, &S::erosionMaxLifetime, "Steps per drop."),
    I("erosionBrushRadius", "Brush radius", kErosion, 1.0, 5.0, &S::erosionBrushRadius, "Samples touched by one carve."),
    I("erosionMaxLevel", "Max LOD level", kErosion, 0.0, 4.0, &S::erosionMaxLevel, "Coarser levels skip erosion."),
    I("erosionEdgeFalloff", "Edge falloff", kErosion, 2.0, 16.0, &S::erosionEdgeFalloff,
        "Samples from the node border where erosion fades out."),
    F("erosionStrength", "Strength", kErosion, 0.0, 8.0, "%.2f", &S::erosionStrength, false,
        "Multiplier on the carved height. Above 2 shows node borders."),

    F("grassMaxSlopeDeg", "Grass max slope (deg)", kMaterial, 5.0, 70.0, "%.0f", &S::grassMaxSlopeDeg, false,
        "Flatter than this is grass or sand."),
    F("rockMinSlopeDeg", "Rock min slope (deg)", kMaterial, 20.0, 85.0, "%.0f", &S::rockMinSlopeDeg, false,
        "Steeper than this is bare rock. Never below the grass slope."),
    F("snowMinHeightFraction", "Snow line (of height)", kMaterial, 0.1, 1.1, "%.2f", &S::snowMinHeightFraction, false,
        "Fraction of the height scale where snow starts."),
    F("snowSlopeLimitDeg", "Snow max slope (deg)", kMaterial, 30.0, 90.0, "%.0f", &S::snowSlopeLimitDeg, false,
        "Snow does not stick on steeper faces."),
    F("sandMaxHeight", "Beach height (m)", kMaterial, 0.0, 60.0, "%.1f", &S::sandMaxHeight, false,
        "Height above sea level that is still sand."),

    { "chunkQuads", "Quads per node", kGeometry, FieldKind::Choice, 16.0, 128.0, false, "%d",
        "Quads on one node side. Changing it reopens the scene.", nullptr, &S::chunkQuads, nullptr, nullptr },
    F("lod0Spacing", "Finest spacing (m)", kGeometry, 0.5, 8.0, "%.2f", &S::lod0Spacing, true,
        "Metres between vertices on the finest level."),
    I("lodLevelCount", "LOD levels", kGeometry, 3.0, 10.0, &S::lodLevelCount, "Number of detail levels."),
    F("lodDistanceFactor", "LOD distance factor", kGeometry, 1.5, 6.0, "%.2f", &S::lodDistanceFactor, false,
        "A node splits when the camera is closer than factor times its size."),
    I("worldHalfExtentChunks", "Half world (nodes)", kGeometry, 128.0, 2048.0, &S::worldHalfExtentChunks,
        "Half the world side in finest nodes. Rounded to the root size."),

    B("frustumCulling", "Frustum culling", kOptimization, &S::frustumCulling, "Off draws every resident node (cost test only)."),
    B("hiZCulling", "Hi-Z culling", kOptimization, &S::hiZCulling, "Hide nodes behind hills with the depth pyramid."),
    F("maxViewDistance", "Max view distance (m)", kOptimization, 500.0, 60000.0, "%.0f", &S::maxViewDistance, true,
        "Nodes farther than this are not drawn."),
};

} // namespace

std::span<const std::int32_t> ChunkQuadChoices() noexcept
{
    return kChunkQuads;
}

std::span<const char* const> SettingGroups() noexcept
{
    return kGroups;
}

std::span<const FieldDesc> SettingFields() noexcept
{
    return kFields;
}

double GetFieldValue(const TerrainSettings& settings, const FieldDesc& field) noexcept
{
    switch (field.kind) {
    case FieldKind::Float: return static_cast<double>(settings.*field.floatMember);
    case FieldKind::Int:
    case FieldKind::Choice: return static_cast<double>(settings.*field.intMember);
    case FieldKind::Bool: return (settings.*field.boolMember) ? 1.0 : 0.0;
    case FieldKind::Seed: return static_cast<double>(settings.*field.seedMember);
    }
    return 0.0;
}

void SetFieldValue(TerrainSettings& settings, const FieldDesc& field, double value) noexcept
{
    if (!std::isfinite(value)) {
        return;
    }
    switch (field.kind) {
    case FieldKind::Float: settings.*field.floatMember = static_cast<float>(value); break;
    case FieldKind::Int:
    case FieldKind::Choice: settings.*field.intMember = static_cast<std::int32_t>(std::lround(value)); break;
    case FieldKind::Bool: settings.*field.boolMember = value >= 0.5; break;
    case FieldKind::Seed: settings.*field.seedMember = static_cast<std::uint32_t>(std::llround(std::clamp(value, 0.0, 4294967295.0))); break;
    }
}

} // namespace gte::terrain
