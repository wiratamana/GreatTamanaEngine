// src/Core/Plugins/PluginRenderFeatureDiagnostics.h
#pragma once

#include <vector>

namespace gte {

class IPluginModule;

// editor-core-separation-4 campaign, PHASE5
// (PHASE5_MULTI_RENDER_FEATURE_PLUGIN_WARNING_AND_REGRESSION_LOCK.md) - a
// small, pure, Tier-1-testable helper: counts how many of the given loaded
// plugin modules implement IRenderFeatureModule_v1. Takes a plain vector of
// already-resolved IPluginModule* (never touches PluginHost/the filesystem/
// any GPU state itself), so it is directly unit-testable with fake
// IPluginModule doubles - mirrors this codebase's own established "extract
// the pure logic, test it directly" precedent (AGENTS.md, "Testability &
// Regression Safety").
int CountModulesImplementingRenderFeature(const std::vector<IPluginModule*>& modules);

} // namespace gte
