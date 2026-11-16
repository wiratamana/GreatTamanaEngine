#pragma once

// Plain data of water bodies (ocean, lake, river). Pure CPU, no GPU or UI types.

#include <cstdint>
#include <string>
#include <vector>

namespace gte::water {

// The ocean surface height; the terrain colours (sand, seabed) key off the same value.
inline constexpr float kDefaultSeaLevel = 0.0f;

inline constexpr int kMaxBodies = 16;
inline constexpr int kMaxOceans = 1;
inline constexpr int kMinLakeVertices = 3;
inline constexpr int kMaxLakeVertices = 32;
inline constexpr int kMinRiverPoints = 2;
inline constexpr int kMaxRiverPoints = 64;
inline constexpr double kMaxWorldCoord = 200000.0;

enum class WaterBodyKind : std::uint8_t { Ocean, Lake, River };

const char* WaterBodyKindName(WaterBodyKind kind) noexcept;
bool TryParseWaterBodyKind(const std::string& name, WaterBodyKind& out) noexcept;

struct Point2 {
    double x = 0.0;
    double z = 0.0;
};

// One control point of a river: position, surface height and full width.
struct RiverPoint {
    double x = 0.0;
    double z = 0.0;
    float surfaceY = 0.0f;
    float width = 12.0f;
};

// Per-body look. Colour override is off by default, so the global water colours apply.
struct WaterBodyLook {
    bool overrideColors = false;
    float shallowColor[3] = { 0.10f, 0.55f, 0.60f };
    float deepColor[3] = { 0.02f, 0.12f, 0.25f };
    float foamColor[3] = { 0.95f, 0.97f, 1.0f };
    float waveScale = 1.0f; // Multiplies the global wave height (rivers and lakes are calmer).
};

struct WaterBody {
    std::uint32_t id = 0; // 0 = unassigned; the set gives a unique id.
    std::string name;
    WaterBodyKind kind = WaterBodyKind::Lake;
    float level = 0.0f;       // Lake and ocean surface height in metres.
    float depth = 6.0f;       // Carve depth below the surface (lake: deepest point, river: channel centre).
    float shoreWidth = 20.0f; // Lake: distance from the edge to full depth.
    float bankWidth = 60.0f;  // Distance outside the edge over which the land is pulled down to the water (soft bank).
    float rimHeight = 0.4f;   // Lake dam height above the surface where land is lower than the water.
    bool carveTerrain = true;
    bool sealRim = true;
    std::vector<Point2> polygon;     // Lake outline, 3..32 corners.
    std::vector<RiverPoint> river;   // River control points, 2..64, in flow direction.
    WaterBodyLook look;
};

// Four corners of an axis-aligned box lake.
std::vector<Point2> MakeBoxPolygon(double centerX, double centerZ, double sizeX, double sizeZ);

// Quality presets of the water look (cost versus detail).
enum class WaterQuality : std::uint8_t { Low, Medium, High };
const char* WaterQualityName(WaterQuality q) noexcept;
bool TryParseWaterQuality(const std::string& name, WaterQuality& out) noexcept;

} // namespace gte::water
