#pragma once

// What the Big World demo does: where the camera stands, which quality tier runs and which render
// feature settings are switched. Pure data, no engine types.

#include <string>
#include <string_view>
#include <vector>

namespace gte::bigworld {

enum class Quality { Lean, Medium, Big };

// Default cloud coverage per tier: lower than the 0.8 feature default so clouds leave sunlit gaps.
inline constexpr double kLeanCloudCoverage = 0.45;
inline constexpr double kMediumCloudCoverage = 0.5;
inline constexpr double kBigCloudCoverage = 0.5;

// Cloud shadow and sun-shaft defaults for the demo.
inline constexpr double kCloudShadowRangeKm = 8.0;     // ~62.5 m per 128x128 shadow texel for field dappling.
inline constexpr double kCloudShadowSoftness = 0.6;    // Tighter blur in texels to match the 8 km shadow map.
inline constexpr double kCloudSunRays = 0.6;           // Screen-space sun glare through cloud gaps.
inline constexpr double kLeanCloudShaftMist = 0.0;     // Off on lean because volumetric light is disabled.
inline constexpr double kScenicCloudShaftMist = 0.15;  // Thin near-field mist when volumetric light stays on.

// Bloom threshold for the demo: above 1.0 white so sunlit clouds do not veil the sky.
inline constexpr double kBloomThreshold = 2.0;

// VolumetricLight budget for tiers that use the "scenic" terrain profile.
inline constexpr double kVolumetricMaxDistance = 400.0; // Metres; clamped to the farthest sun shadow cascade.
inline constexpr double kVolumetricDensity = 0.004;     // Extinction per metre over the 400 m range.
inline constexpr double kVolumetricAnisotropyG = 0.75;  // Forward scattering toward the sun.
inline constexpr int kVolumetricShadowSamples = 2;      // Shadow taps per froxel.
inline constexpr bool kVolumetricTemporal = true;
inline constexpr double kVolumetricStrength = 1.0;

const char* QualityName(Quality quality) noexcept;
const char* TierTerrainProfile(Quality quality) noexcept;
bool TierUsesVolumetricLight(Quality quality) noexcept;
double TierCloudCoverage(Quality quality) noexcept;
double TierCloudShaftMist(Quality quality) noexcept;

// Case-insensitive "lean", "medium" or "big". False when unknown.
bool TryParseQuality(std::string_view text, Quality& out) noexcept;

struct KeyValue {
    std::string key;
    std::string value;
};

// Settings of one render feature: the keys saved before the demo starts and the values the demo sets.
struct FeatureTweak {
    std::string feature;
    std::vector<std::string> restoreKeys;
    std::vector<KeyValue> values;
    bool required = true; // A missing feature fails the demo when set.
};

struct BigWorldPlan {
    Quality quality = Quality::Big;
    double cloudCoverage = kBigCloudCoverage;
    double cloudShadowRangeKm = kCloudShadowRangeKm;
    double cloudShadowSoftness = kCloudShadowSoftness;
    double cloudSunRays = kCloudSunRays;
    double cloudShaftMist = kScenicCloudShaftMist;
    double bloomThreshold = kBloomThreshold;

    bool volumetricLight = true; // True on medium and big ("scenic"); false on lean ("lean").
    double volumetricMaxDistance = kVolumetricMaxDistance;
    double volumetricDensity = kVolumetricDensity;
    double volumetricAnisotropyG = kVolumetricAnisotropyG;
    int volumetricShadowSamples = kVolumetricShadowSamples;
    bool volumetricTemporal = kVolumetricTemporal;
    double volumetricStrength = kVolumetricStrength;

    // Camera. Heading 0 looks along +Z, 90 along +X.
    double spotX = -2000.0;
    double spotZ = -800.0;
    double heightAboveGround = 6.0;
    double headingDegrees = 90.0;
    double lookDownDegrees = 6.0;
    bool freeCamera = true; // The user can fly after the demo is ready.

    // Terrain.
    std::string terrainProfile = "scenic";
    bool renderBudget = true;
    bool flatOceanQuad = true;
    int minResidentNodes = 12;

    // Water around the spawn. Distances are along the heading from the spot, sides are to its right.
    bool lake = true;
    double lakeDistance = 110.0;
    double lakeSize = 120.0;
    int lakeCorners = 14;
    double lakeBankWidth = 30.0;
    bool river = false;
    double riverStart = -260.0; // Distance along the heading where the river begins (negative = behind the spot).
    double riverEnd = 320.0;
    double riverSide = 140.0;
    double riverWiggle = 22.0;
    double riverWidth = 14.0;

    std::vector<FeatureTweak> tweaks;

    // Seconds each waiting phase may take before it fails (terrain) or warns (water, grass).
    double nodeWaitSeconds = 90.0;
    double waterWaitSeconds = 30.0;
    double grassWaitSeconds = 30.0;
    bool waitForGrass = true;
};

// The plan of a quality tier, with its feature tweaks (clouds, bloom, volumetric light, grass).
BigWorldPlan MakeBigWorldPlan(Quality quality);

// Overrides the cloud coverage of `plan` (clamped to 0..1) and updates its VolumetricCloud tweak.
void SetCloudCoverage(BigWorldPlan& plan, double coverage);

// Finite values, bounded ranges, no empty names. Never throws.
BigWorldPlan SanitizeBigWorldPlan(BigWorldPlan plan);

struct PlanPoint {
    double x = 0.0;
    double z = 0.0;
};

// A point `distance` metres from the spot along the heading (plus `side` metres to the right).
PlanPoint PointAhead(const BigWorldPlan& plan, double distance, double side = 0.0) noexcept;

// Closed outline of the lake: a rounded, uneven blob around its centre (no repeated first point).
std::vector<PlanPoint> LakeOutline(const BigWorldPlan& plan);

// River centre line from the plan start to its end: a gentle wiggle that ends in a straight run.
std::vector<PlanPoint> RiverPath(const BigWorldPlan& plan);

} // namespace gte::bigworld
