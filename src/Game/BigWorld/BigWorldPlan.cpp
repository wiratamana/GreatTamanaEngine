#include "BigWorldPlan.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace gte::bigworld {

namespace {

constexpr double kPi = 3.14159265358979323846;

double Finite(double value, double fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

double Bounded(double value, double fallback, double lo, double hi) noexcept
{
    return std::clamp(Finite(value, fallback), lo, hi);
}

bool EqualsNoCase(std::string_view a, std::string_view b) noexcept
{
    return a.size() == b.size()
        && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

const char* GrassPresetFor(Quality quality) noexcept
{
    switch (quality) {
    case Quality::Lean: return "Lean";
    case Quality::Medium: return "Medium";
    case Quality::Big: break;
    }
    return "Big";
}

} // namespace

const char* QualityName(Quality quality) noexcept
{
    switch (quality) {
    case Quality::Lean: return "lean";
    case Quality::Medium: return "medium";
    case Quality::Big: break;
    }
    return "big";
}

bool TryParseQuality(std::string_view text, Quality& out) noexcept
{
    for (const Quality q : { Quality::Lean, Quality::Medium, Quality::Big }) {
        if (EqualsNoCase(text, QualityName(q))) {
            out = q;
            return true;
        }
    }
    return false;
}

BigWorldPlan MakeBigWorldPlan(Quality quality)
{
    BigWorldPlan plan;
    plan.quality = quality;
    plan.river = quality != Quality::Lean;

    FeatureTweak clouds;
    clouds.feature = "VolumetricCloud";
    clouds.restoreKeys = { "enabled" };
    clouds.values = { { "enabled", "true" } };
    plan.tweaks.push_back(std::move(clouds));

    FeatureTweak grass;
    grass.feature = "Grass";
    grass.restoreKeys = { "enabled", "densityLod0", "densityLod1", "densityLod2", "lod0Distance", "lod1Distance",
        "lod2Distance", "instanceCapLod0", "instanceCapLod1", "instanceCapLod2", "gridCells", "heightTileTexels",
        "heightTileTexelSize" };
    grass.values = { { "enabled", "true" }, { "preset", GrassPresetFor(quality) } };
    plan.tweaks.push_back(std::move(grass));
    return plan;
}

BigWorldPlan SanitizeBigWorldPlan(BigWorldPlan plan)
{
    const BigWorldPlan defaults;
    plan.spotX = Bounded(plan.spotX, defaults.spotX, -15000.0, 15000.0);
    plan.spotZ = Bounded(plan.spotZ, defaults.spotZ, -15000.0, 15000.0);
    plan.heightAboveGround = Bounded(plan.heightAboveGround, defaults.heightAboveGround, 1.0, 2000.0);
    plan.headingDegrees = Bounded(plan.headingDegrees, defaults.headingDegrees, -720.0, 720.0);
    plan.lookDownDegrees = Bounded(plan.lookDownDegrees, defaults.lookDownDegrees, -60.0, 85.0);
    plan.lakeDistance = Bounded(plan.lakeDistance, defaults.lakeDistance, 20.0, 2000.0);
    plan.lakeSize = Bounded(plan.lakeSize, defaults.lakeSize, 20.0, 1000.0);
    plan.riverDistance = Bounded(plan.riverDistance, defaults.riverDistance, 20.0, 2000.0);
    plan.riverLength = Bounded(plan.riverLength, defaults.riverLength, 50.0, 3000.0);
    plan.riverWidth = Bounded(plan.riverWidth, defaults.riverWidth, 2.0, 100.0);
    plan.minResidentNodes = std::clamp(plan.minResidentNodes, 1, 400);
    plan.nodeWaitSeconds = Bounded(plan.nodeWaitSeconds, defaults.nodeWaitSeconds, 1.0, 600.0);
    plan.waterWaitSeconds = Bounded(plan.waterWaitSeconds, defaults.waterWaitSeconds, 1.0, 600.0);
    plan.grassWaitSeconds = Bounded(plan.grassWaitSeconds, defaults.grassWaitSeconds, 1.0, 600.0);
    if (plan.terrainProfile.empty()) {
        plan.terrainProfile = defaults.terrainProfile;
    }
    std::erase_if(plan.tweaks, [](const FeatureTweak& t) { return t.feature.empty() || t.values.empty(); });
    return plan;
}

PlanPoint PointAhead(const BigWorldPlan& plan, double distance, double side) noexcept
{
    const double heading = plan.headingDegrees * kPi / 180.0;
    const double forwardX = std::sin(heading);
    const double forwardZ = std::cos(heading);
    // Right of the heading when seen from above with +Z north and +X east.
    return { plan.spotX + forwardX * distance + forwardZ * side, plan.spotZ + forwardZ * distance - forwardX * side };
}

} // namespace gte::bigworld
