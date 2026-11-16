#include "TerrainFeatureProfile.h"

#include "../HizDemoProfile.h"
#include "../PresetFeatureSettings.h"
#include "../PresetSceneSupport.h"
#include "../../Core/Plugins/RenderFeatureCompositor.h"

#include <algorithm>
#include <array>
#include <exception>
#include <string_view>
#include <utility>

namespace gte {

namespace {

constexpr const char* kLogCategory = "TerrainFeatures";

// Culling feature keys the terrain scene reads back and rewrites.
constexpr std::array<std::string_view, 5> kSettingKeys{ "enabled", "mode", "cone", "collectStats", "debugView" };

// The fixed-cost features that hurt most on an integrated GPU.
constexpr std::array<std::string_view, 3> kLeanPrefixes{ "VolumetricLight", "Ssgi", "Gtao" };

bool StartsWithAny(std::string_view name, std::span<const std::string_view> prefixes) noexcept
{
    return std::any_of(prefixes.begin(), prefixes.end(), [name](std::string_view p) { return name.starts_with(p); });
}

} // namespace

const char* TerrainCullModeName(const terrain::TerrainSettings& settings) noexcept
{
    if (!settings.frustumCulling) {
        return "NoCull";
    }
    return settings.hiZCulling ? "HiZ" : "Frustum";
}

std::vector<std::string> PlanTerrainFeatureSwitch(
    terrain::TerrainProfile profile, std::span<const RenderFeatureDebugEntry> features)
{
    std::vector<std::string> off;
    if (profile == terrain::TerrainProfile::Full) {
        return off;
    }
    for (const RenderFeatureDebugEntry& entry : features) {
        if (!entry.enabled) {
            continue;
        }
        const bool lean = StartsWithAny(entry.name, kLeanPrefixes);
        const bool geometryOnly = profile == terrain::TerrainProfile::GeometryOnly && IsGeometryOnlyFeature(entry.name);
        if (lean || geometryOnly) {
            off.push_back(entry.name);
        }
    }
    return off;
}

bool SetTerrainCullMode(fx::IFeatureDebugEndpoint* gpuDrivenScene, const terrain::TerrainSettings& settings) noexcept
try
{
    if (gpuDrivenScene == nullptr) {
        return false;
    }
    const std::array<fx::FeatureDebugArg, 1> mode{ { { "mode", TerrainCullModeName(settings) } } };
    return gpuDrivenScene->Set(mode).ok;
}
catch (...)
{
    return false;
}

bool RestoreTerrainFeatures(RenderFeatureCompositor* compositor, fx::IFeatureDebugEndpoint* gpuDrivenScene,
    TerrainFeatureState& state) noexcept
{
    if (!state.active) {
        return true;
    }
    const bool switched = SetFeaturesEnabled(compositor, kLogCategory, state.disabledFeatures, std::span<const std::string>{});
    const bool restored = RestoreFeatureSettings(gpuDrivenScene, kLogCategory, "GpuDrivenScene", state.savedSettings);
    state = TerrainFeatureState{};
    return switched && restored;
}

bool ApplyTerrainFeatures(RenderFeatureCompositor* compositor, fx::IFeatureDebugEndpoint* gpuDrivenScene,
    terrain::TerrainProfile profile, const terrain::TerrainSettings& settings, TerrainFeatureState& state,
    std::string& error)
{
    if (gpuDrivenScene == nullptr) {
        error = "the GpuDrivenScene feature is missing";
        return false;
    }
    if (profile != terrain::TerrainProfile::Full && compositor == nullptr) {
        error = "the render feature compositor is missing";
        return false;
    }
    TerrainFeatureState next;
    try {
        next.savedSettings = SnapshotFeatureSettings(*gpuDrivenScene, kSettingKeys);

        const std::array<fx::FeatureDebugArg, 5> wanted{ {
            { "enabled", "true" },
            { "mode", TerrainCullModeName(settings) },
            { "cone", "false" },
            { "collectStats", "true" },
            { "debugView", "Off" },
        } };
        const fx::FeatureDebugResult applied = gpuDrivenScene->Set(wanted);
        if (!applied.ok) {
            error = "GpuDrivenScene refused the settings: " + applied.message;
            return false;
        }
        next.active = true;

        if (compositor != nullptr && profile != terrain::TerrainProfile::Full) {
            for (const std::string& name : PlanTerrainFeatureSwitch(profile, compositor->DebugSnapshot())) {
                if (!TrySetFeatureEnabled(*compositor, name, false)) {
                    error = "could not turn off the feature \"" + name + "\"";
                    RestoreTerrainFeatures(compositor, gpuDrivenScene, next);
                    return false;
                }
                next.disabledFeatures.push_back(name);
            }
        }
        state = std::move(next);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        RestoreTerrainFeatures(compositor, gpuDrivenScene, next);
        return false;
    }
}

} // namespace gte
