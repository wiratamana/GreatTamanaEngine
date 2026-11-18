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

// Cloud shadow and screen-space sun-shaft defaults for the demo.
inline constexpr double kCloudShadowRangeKm = 8.0;      // ~62.5 m per 128x128 shadow texel for field dappling.
inline constexpr double kCloudShadowSoftness = 0.6;     // Tighter blur in texels to match the 8 km shadow map.
inline constexpr double kCloudSunRays = 1.2;            // Screen-space sun shafts through cloud gaps.
inline constexpr double kCloudSunRaysContrast = 2.5;    // Suppresses uniform sky veil so partial cloud gaps form beams.
inline constexpr double kCloudSunRaysDecay = 1.0;       // Radial falloff of sun-shaft taps away from the sun.
inline constexpr double kCloudSunRaysLength = 1.5;      // Reach of the sun shafts across the screen in view heights.

// Bloom defaults for the demo: threshold above 1.0 white and softer intensity so sunlit clouds do not veil the sky.
inline constexpr double kBloomThreshold = 2.0;
inline constexpr double kBloomIntensity = 0.2;

const char* QualityName(Quality quality) noexcept;
double TierCloudCoverage(Quality quality) noexcept;

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
    double cloudSunRaysContrast = kCloudSunRaysContrast;
    double cloudSunRaysDecay = kCloudSunRaysDecay;
    double cloudSunRaysLength = kCloudSunRaysLength;
    double bloomThreshold = kBloomThreshold;
    double bloomIntensity = kBloomIntensity;

    // Camera. Heading 0 looks along +Z, 90 along +X.
    double spotX = -2000.0;
    double spotZ = -800.0;
    double heightAboveGround = 6.0;
    double headingDegrees = 90.0;
    double lookDownDegrees = 6.0;
    bool freeCamera = true; // The user can fly after the demo is ready.

    // Terrain.
    std::string terrainProfile = "lean";
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

// The plan of a quality tier, with its feature tweaks (clouds, bloom, grass).
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
