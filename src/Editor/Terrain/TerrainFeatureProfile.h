#pragma once

// What the terrain scene changes in the render features while it is open, and the undo of it:
// heavy fixed-cost features off, and the GPU-driven culling set to the mode the terrain settings ask for.

#include "../../Core/FeatureKit/FeatureDebug.h"
#include "../../Core/Plugins/RenderFeatureDebugEntry.h"
#include "../../Terrain/TerrainControl.h"

#include <span>
#include <string>
#include <vector>

namespace gte {

class RenderFeatureCompositor;

// Culling mode name of the GPU-driven feature for these settings: NoCull, Frustum or HiZ.
const char* TerrainCullModeName(const terrain::TerrainSettings& settings) noexcept;

// Features of `profile` that are still enabled and must go off. Pure.
std::vector<std::string> PlanTerrainFeatureSwitch(
    terrain::TerrainProfile profile, std::span<const RenderFeatureDebugEntry> features);

// What a terrain scene saved before it changed anything.
struct TerrainFeatureState {
    bool active = false;
    std::vector<fx::FeatureDebugArg> savedSettings; // Culling feature settings before the scene opened.
    std::vector<std::string> disabledFeatures;      // Features the scene turned off.
};

// Saves the culling settings, switches the profile's features off and sets the culling feature up (enabled,
// the mode of `settings`, stats on, no debug tint). Everything is undone on failure. `compositor` may be null
// for the Full profile.
bool ApplyTerrainFeatures(RenderFeatureCompositor* compositor, fx::IFeatureDebugEndpoint* gpuDrivenScene,
    terrain::TerrainProfile profile, const terrain::TerrainSettings& settings, TerrainFeatureState& state,
    std::string& error);

// Sets only the culling mode of `settings`. False when the feature refused.
bool SetTerrainCullMode(fx::IFeatureDebugEndpoint* gpuDrivenScene, const terrain::TerrainSettings& settings) noexcept;

// Turns the features back on and restores the culling settings. Safe to call twice. False when incomplete.
bool RestoreTerrainFeatures(RenderFeatureCompositor* compositor, fx::IFeatureDebugEndpoint* gpuDrivenScene,
    TerrainFeatureState& state) noexcept;

} // namespace gte
