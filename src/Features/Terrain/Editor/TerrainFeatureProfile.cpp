#include "TerrainFeatureProfile.h"

#include "../../../Editor/PresetFeatureSettings.h"
#include "../../../Editor/PresetSceneSupport.h"
#include "../../../Core/Plugins/RenderFeatureCompositor.h"

#include <algorithm>
#include <array>
#include <cstddef>
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

// Features the geometry-only profile turns off on top of the lean ones.
constexpr std::array<std::string_view, 6> kGeometryOnlyPrefixes{
    "Shadow", "VolumetricLight", "Atmosphere", "Ssgi", "Gtao", "ToneMapping" };

// Quality the render budget sets: shadows only near the camera on small maps, fewer aerial perspective samples.
struct BudgetKey {
    const char* key;
    const char* value;
};
struct BudgetEntry {
    std::string_view feature;
    std::array<BudgetKey, 3> keys;
    std::size_t count;
};
constexpr std::array<BudgetEntry, 2> kRenderBudget{ {
    { "Shadow", { { { "cascadeCount", "2" }, { "maxMapResolution", "1024" }, { "maxDistance", "800" } } }, 3 },
    { "Atmosphere", { { { "aerialPerspectiveSamplesPerSlice", "4" }, { "", "" }, { "", "" } } }, 1 },
} };

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
        const bool geometryOnly = profile == terrain::TerrainProfile::GeometryOnly && StartsWithAny(entry.name, kGeometryOnlyPrefixes);
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
    const FeatureEndpointLookup& findEndpoint, TerrainFeatureState& state) noexcept
{
    if (!state.active) {
        return true;
    }
    const bool switched = SetFeaturesEnabled(compositor, kLogCategory, state.disabledFeatures, std::span<const std::string>{});
    bool restored = RestoreFeatureSettings(gpuDrivenScene, kLogCategory, "GpuDrivenScene", state.savedSettings);
    for (const SavedFeatureSettings& saved : state.savedBudget) {
        fx::IFeatureDebugEndpoint* endpoint = findEndpoint ? findEndpoint(saved.feature) : nullptr;
        restored = RestoreFeatureSettings(endpoint, kLogCategory, saved.feature, saved.args) && restored;
    }
    state = TerrainFeatureState{};
    return switched && restored;
}

bool ApplyTerrainFeatures(RenderFeatureCompositor* compositor, fx::IFeatureDebugEndpoint* gpuDrivenScene,
    terrain::TerrainProfile profile, const terrain::TerrainSettings& settings, bool renderBudget,
    const FeatureEndpointLookup& findEndpoint, TerrainFeatureState& state, std::string& error)
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

        if (renderBudget && profile != terrain::TerrainProfile::Full && findEndpoint) {
            for (const BudgetEntry& entry : kRenderBudget) {
                fx::IFeatureDebugEndpoint* endpoint = findEndpoint(entry.feature);
                if (endpoint == nullptr) {
                    continue;
                }
                std::vector<std::string_view> keys;
                std::vector<fx::FeatureDebugArg> args;
                for (std::size_t i = 0; i < entry.count; ++i) {
                    keys.emplace_back(entry.keys[i].key);
                    args.push_back({ entry.keys[i].key, entry.keys[i].value });
                }
                SavedFeatureSettings saved{ std::string(entry.feature), SnapshotFeatureSettings(*endpoint, keys) };
                const fx::FeatureDebugResult budget = endpoint->Set(args);
                if (!budget.ok) {
                    error = std::string(entry.feature) + " refused the render budget: " + budget.message;
                    RestoreTerrainFeatures(compositor, gpuDrivenScene, findEndpoint, next);
                    return false;
                }
                next.savedBudget.push_back(std::move(saved));
            }
        }

        if (compositor != nullptr && profile != terrain::TerrainProfile::Full) {
            for (const std::string& name : PlanTerrainFeatureSwitch(profile, compositor->DebugSnapshot())) {
                if (!TrySetFeatureEnabled(*compositor, name, false)) {
                    error = "could not turn off the feature \"" + name + "\"";
                    RestoreTerrainFeatures(compositor, gpuDrivenScene, findEndpoint, next);
                    return false;
                }
                next.disabledFeatures.push_back(name);
            }
        }
        state = std::move(next);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        RestoreTerrainFeatures(compositor, gpuDrivenScene, findEndpoint, next);
        return false;
    }
}

} // namespace gte
