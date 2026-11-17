#pragma once

// What the terrain scene changes in the render features while it is open, and the undo of it:
// heavy fixed-cost features off, and the GPU-driven culling set to the mode the terrain settings ask for.

#include "../../Core/FeatureKit/FeatureDebug.h"
#include "../PresetFeatureSettings.h"
#include "../../Core/Plugins/RenderFeatureDebugEntry.h"
#include "../../Features/Terrain/TerrainControl.h"

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gte {

class RenderFeatureCompositor;

// Finds the debug endpoint of a render feature by name; null when it does not exist.
using FeatureEndpointLookup = std::function<fx::IFeatureDebugEndpoint*(std::string_view)>;

// Culling mode name of the GPU-driven feature for these settings: NoCull, Frustum or HiZ.
const char* TerrainCullModeName(const terrain::TerrainSettings& settings) noexcept;

// Features of `profile` that are still enabled and must go off. Pure.
std::vector<std::string> PlanTerrainFeatureSwitch(
    terrain::TerrainProfile profile, std::span<const RenderFeatureDebugEntry> features);

// What a terrain scene saved before it changed anything.
struct TerrainFeatureState {
    bool active = false;
    std::vector<fx::FeatureDebugArg> savedSettings; // Culling feature settings before the scene opened.
    std::vector<SavedFeatureSettings> savedBudget;  // Shadow and atmosphere settings the render budget replaced.
    std::vector<std::string> disabledFeatures;      // Features the scene turned off.
};

// Saves the culling settings, switches the profile's features off and sets the culling feature up (enabled,
// the mode of `settings`, stats on, no debug tint). With `renderBudget` the shadow and atmosphere quality is
// lowered to what an integrated GPU can pay for a big landscape. Everything is undone on failure. `compositor`
// may be null for the Full profile; `findEndpoint` may be empty when `renderBudget` is false.
bool ApplyTerrainFeatures(RenderFeatureCompositor* compositor, fx::IFeatureDebugEndpoint* gpuDrivenScene,
    terrain::TerrainProfile profile, const terrain::TerrainSettings& settings, bool renderBudget,
    const FeatureEndpointLookup& findEndpoint, TerrainFeatureState& state, std::string& error);

// Sets only the culling mode of `settings`. False when the feature refused.
bool SetTerrainCullMode(fx::IFeatureDebugEndpoint* gpuDrivenScene, const terrain::TerrainSettings& settings) noexcept;

// Turns the features back on and restores the culling settings. Safe to call twice. False when incomplete.
bool RestoreTerrainFeatures(RenderFeatureCompositor* compositor, fx::IFeatureDebugEndpoint* gpuDrivenScene,
    const FeatureEndpointLookup& findEndpoint, TerrainFeatureState& state) noexcept;

} // namespace gte
