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

// Cloud cost levers of the lower tiers: a coarser march grid saves most for the least visible change.
void AddCloudTierValues(Quality quality, FeatureTweak& clouds)
{
    switch (quality) {
    case Quality::Lean:
        clouds.values.push_back({ "maxSteps", "48" });
        clouds.values.push_back({ "resolutionDivisor", "6" });
        clouds.values.push_back({ "shadowSteps", "6" });
        clouds.values.push_back({ "shadowUpdate", "5" });
        break;
    case Quality::Medium:
        clouds.values.push_back({ "maxSteps", "64" });
        clouds.values.push_back({ "resolutionDivisor", "6" });
        break;
    case Quality::Big:
        break;
    }
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
    clouds.restoreKeys = { "enabled", "maxSteps", "resolutionDivisor", "shadowSteps", "shadowUpdate" };
    clouds.values = { { "enabled", "true" } };
    AddCloudTierValues(quality, clouds);
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
    plan.lakeCorners = std::clamp(plan.lakeCorners, 6, 32);
    plan.lakeBankWidth = Bounded(plan.lakeBankWidth, defaults.lakeBankWidth, 0.0, 200.0);
    plan.riverStart = Bounded(plan.riverStart, defaults.riverStart, -3000.0, 3000.0);
    plan.riverEnd = Bounded(plan.riverEnd, defaults.riverEnd, -3000.0, 3000.0);
    if (plan.riverEnd - plan.riverStart < 50.0) {
        plan.riverEnd = plan.riverStart + 50.0;
    }
    plan.riverSide = Bounded(plan.riverSide, defaults.riverSide, -2000.0, 2000.0);
    plan.riverWiggle = Bounded(plan.riverWiggle, defaults.riverWiggle, 0.0, 300.0);
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

std::vector<PlanPoint> LakeOutline(const BigWorldPlan& plan)
{
    const int corners = std::clamp(plan.lakeCorners, 6, 32);
    const PlanPoint center = PointAhead(plan, plan.lakeDistance);
    const double half = plan.lakeSize * 0.5;
    std::vector<PlanPoint> outline;
    outline.reserve(static_cast<std::size_t>(corners));
    for (int i = 0; i < corners; ++i) {
        const double a = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(corners);
        // Three low harmonics make an uneven shore; the radius never drops below 55 percent of the half size.
        const double r = half * std::max(0.55, 1.0 + 0.20 * std::sin(2.0 * a + 0.7) + 0.13 * std::sin(3.0 * a + 2.1)
                                                    + 0.07 * std::sin(5.0 * a));
        outline.push_back({ center.x + r * std::cos(a) * 1.15, center.z + r * std::sin(a) * 0.85 });
    }
    return outline;
}

std::vector<PlanPoint> RiverPath(const BigWorldPlan& plan)
{
    constexpr int kPoints = 7;
    std::vector<PlanPoint> path;
    path.reserve(kPoints);
    for (int i = 0; i < kPoints; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(kPoints - 1);
        const double along = plan.riverStart + (plan.riverEnd - plan.riverStart) * t;
        // The wiggle fades out toward the end so the river meets the shore straight.
        const double side = plan.riverSide + plan.riverWiggle * std::sin(t * 2.0 * kPi * 1.25 + 0.6) * (1.0 - t * t);
        path.push_back(PointAhead(plan, along, side));
    }
    return path;
}

} // namespace gte::bigworld
