#include "BigWorldPlan.h"

#include <algorithm>
#include <cctype>
#include <charconv>
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

std::string FormatDouble(double value)
{
    char buffer[40];
    const auto [end, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general, 12);
    return ec == std::errc{} ? std::string(buffer, end) : std::string("0.5");
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

// Cloud march and shadow cost levers per quality tier.
void AddCloudTierValues(Quality quality, FeatureTweak& clouds)
{
    switch (quality) {
    case Quality::Lean:
        clouds.values.push_back({ "maxSteps", "56" });
        clouds.values.push_back({ "resolutionDivisor", "4" });
        clouds.values.push_back({ "shadowSteps", "8" });
        clouds.values.push_back({ "shadowUpdate", "2" });
        break;
    case Quality::Medium:
        clouds.values.push_back({ "maxSteps", "76" });
        clouds.values.push_back({ "resolutionDivisor", "3" });
        break;
    case Quality::Big:
        clouds.values.push_back({ "maxSteps", "96" });
        break;
    }
}

FeatureTweak* FindTweak(BigWorldPlan& plan, std::string_view feature) noexcept
{
    const auto it = std::find_if(plan.tweaks.begin(), plan.tweaks.end(),
        [feature](const FeatureTweak& t) { return t.feature == feature; });
    return it == plan.tweaks.end() ? nullptr : &*it;
}

void UpsertTweakKey(FeatureTweak& tweak, const char* key, std::string value, bool addIfMissing)
{
    const auto kv = std::find_if(tweak.values.begin(), tweak.values.end(),
        [key](const KeyValue& entry) { return entry.key == key; });
    if (kv == tweak.values.end() && !addIfMissing) {
        return;
    }
    if (std::find(tweak.restoreKeys.begin(), tweak.restoreKeys.end(), key) == tweak.restoreKeys.end()) {
        tweak.restoreKeys.emplace_back(key);
    }
    if (kv != tweak.values.end()) {
        kv->value = std::move(value);
    } else {
        tweak.values.push_back({ key, std::move(value) });
    }
}

void SyncPlanTweaks(BigWorldPlan& plan)
{
    if (FeatureTweak* const clouds = FindTweak(plan, "VolumetricCloud")) {
        UpsertTweakKey(*clouds, "coverage", FormatDouble(plan.cloudCoverage), false);
        UpsertTweakKey(*clouds, "shadowRangeKm", FormatDouble(plan.cloudShadowRangeKm), false);
        UpsertTweakKey(*clouds, "shadowSoftness", FormatDouble(plan.cloudShadowSoftness), false);
        UpsertTweakKey(*clouds, "shadowFill", FormatDouble(plan.cloudShadowFill), false);
        UpsertTweakKey(*clouds, "sunRays", FormatDouble(plan.cloudSunRays), false);
        UpsertTweakKey(*clouds, "sunRaysContrast", FormatDouble(plan.cloudSunRaysContrast), false);
        UpsertTweakKey(*clouds, "sunRaysDecay", FormatDouble(plan.cloudSunRaysDecay), false);
        UpsertTweakKey(*clouds, "sunRaysLength", FormatDouble(plan.cloudSunRaysLength), false);
    }
    if (FeatureTweak* const bloom = FindTweak(plan, "Bloom")) {
        UpsertTweakKey(*bloom, "threshold", FormatDouble(plan.bloomThreshold), false);
        UpsertTweakKey(*bloom, "intensity", FormatDouble(plan.bloomIntensity), false);
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

double TierCloudCoverage(Quality quality) noexcept
{
    switch (quality) {
    case Quality::Lean: return kLeanCloudCoverage;
    case Quality::Medium: return kMediumCloudCoverage;
    case Quality::Big: break;
    }
    return kBigCloudCoverage;
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
    plan.cloudCoverage = TierCloudCoverage(quality);
    plan.river = quality != Quality::Lean;

    FeatureTweak clouds;
    clouds.feature = "VolumetricCloud";
    clouds.restoreKeys = { "enabled", "coverage", "shadowRangeKm", "shadowSoftness", "shadowFill", "sunRays",
        "sunRaysContrast", "sunRaysDecay", "sunRaysLength",
        "maxSteps", "resolutionDivisor", "shadowSteps", "shadowUpdate" };
    clouds.values = {
        { "enabled", "true" },
        { "coverage", FormatDouble(plan.cloudCoverage) },
        { "shadowRangeKm", FormatDouble(plan.cloudShadowRangeKm) },
        { "shadowSoftness", FormatDouble(plan.cloudShadowSoftness) },
        { "shadowFill", FormatDouble(plan.cloudShadowFill) },
        { "sunRays", FormatDouble(plan.cloudSunRays) },
        { "sunRaysContrast", FormatDouble(plan.cloudSunRaysContrast) },
        { "sunRaysDecay", FormatDouble(plan.cloudSunRaysDecay) },
        { "sunRaysLength", FormatDouble(plan.cloudSunRaysLength) },
    };
    AddCloudTierValues(quality, clouds);
    plan.tweaks.push_back(std::move(clouds));

    FeatureTweak bloom;
    bloom.feature = "Bloom";
    bloom.restoreKeys = { "enabled", "threshold", "intensity" };
    bloom.values = {
        { "enabled", "true" },
        { "threshold", FormatDouble(plan.bloomThreshold) },
        { "intensity", FormatDouble(plan.bloomIntensity) },
    };
    plan.tweaks.push_back(std::move(bloom));

    FeatureTweak grass;
    grass.feature = "Grass";
    grass.restoreKeys = { "enabled", "densityLod0", "densityLod1", "densityLod2", "lod0Distance", "lod1Distance",
        "lod2Distance", "instanceCapLod0", "instanceCapLod1", "instanceCapLod2", "gridCells", "heightTileTexels",
        "heightTileTexelSize" };
    grass.values = { { "enabled", "true" }, { "preset", GrassPresetFor(quality) } };
    plan.tweaks.push_back(std::move(grass));
    return plan;
}

void SetCloudCoverage(BigWorldPlan& plan, double coverage)
{
    plan.cloudCoverage = Bounded(coverage, TierCloudCoverage(plan.quality), 0.0, 1.0);
    if (FeatureTweak* const clouds = FindTweak(plan, "VolumetricCloud")) {
        UpsertTweakKey(*clouds, "coverage", FormatDouble(plan.cloudCoverage), true);
    }
}

BigWorldPlan SanitizeBigWorldPlan(BigWorldPlan plan)
{
    const BigWorldPlan defaults;
    plan.cloudCoverage = Bounded(plan.cloudCoverage, TierCloudCoverage(plan.quality), 0.0, 1.0);
    plan.cloudShadowRangeKm = Bounded(plan.cloudShadowRangeKm, defaults.cloudShadowRangeKm, 4.0, 100.0);
    plan.cloudShadowSoftness = Bounded(plan.cloudShadowSoftness, defaults.cloudShadowSoftness, 0.0, 4.0);
    plan.cloudShadowFill = Bounded(plan.cloudShadowFill, defaults.cloudShadowFill, 0.0, 1.0);
    plan.cloudSunRays = Bounded(plan.cloudSunRays, defaults.cloudSunRays, 0.0, 3.0);
    plan.cloudSunRaysContrast = Bounded(plan.cloudSunRaysContrast, defaults.cloudSunRaysContrast, 0.2, 3.0);
    plan.cloudSunRaysDecay = Bounded(plan.cloudSunRaysDecay, defaults.cloudSunRaysDecay, 0.0, 6.0);
    plan.cloudSunRaysLength = Bounded(plan.cloudSunRaysLength, defaults.cloudSunRaysLength, 0.2, 3.0);
    plan.bloomThreshold = Bounded(plan.bloomThreshold, defaults.bloomThreshold, 0.0, 16.0);
    plan.bloomIntensity = Bounded(plan.bloomIntensity, defaults.bloomIntensity, 0.0, 4.0);
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
    SyncPlanTweaks(plan);
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
