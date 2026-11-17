#pragma once

// What the Big World demo does: where the camera stands, which quality tier runs and which render
// feature settings are switched. Pure data, no engine types.

#include <string>
#include <string_view>
#include <vector>

namespace gte::bigworld {

enum class Quality { Lean, Medium, Big };

const char* QualityName(Quality quality) noexcept;

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

    // Water around the spawn. Distances are along the heading from the spot.
    bool lake = true;
    double lakeDistance = 110.0;
    double lakeSize = 120.0;
    bool river = false;
    double riverDistance = 160.0;
    double riverLength = 500.0;
    double riverWidth = 14.0;

    std::vector<FeatureTweak> tweaks;

    // Seconds each waiting phase may take before it fails (terrain) or warns (water, grass).
    double nodeWaitSeconds = 90.0;
    double waterWaitSeconds = 30.0;
    double grassWaitSeconds = 30.0;
    bool waitForGrass = true;
};

// The plan of a quality tier, with its feature tweaks (clouds on, grass preset).
BigWorldPlan MakeBigWorldPlan(Quality quality);

// Finite values, bounded ranges, no empty names. Never throws.
BigWorldPlan SanitizeBigWorldPlan(BigWorldPlan plan);

struct PlanPoint {
    double x = 0.0;
    double z = 0.0;
};

// A point `distance` metres from the spot along the heading (plus `side` metres to the right).
PlanPoint PointAhead(const BigWorldPlan& plan, double distance, double side = 0.0) noexcept;

} // namespace gte::bigworld
