#pragma once

#include <vector>

// Generic hook: once plugins finish loading, each registered orchestrator finds and wires up the capability it cares about.
// Host-internal; it never crosses the plugin ABI boundary.

namespace gte {

class IPluginModule;

namespace rg {
struct RenderPassFrameContext;
struct RenderPassDesc;
} // namespace rg

// One orchestrator per plugin capability kind. Core owns them and creates them in RegisterBuiltinCapabilityOrchestrators().
class IPluginCapabilityOrchestrator {
public:
    virtual ~IPluginCapabilityOrchestrator() = default;

    // Called once after plugins load. Finds the loaded modules that expose this capability; discovery only, no drawing.
    virtual void OnPluginsLoaded(const std::vector<IPluginModule*>& modules) = 0;

    // Optional: adds render graph passes. Default does nothing.
    virtual void ContributeRenderGraphPasses(const rg::RenderPassFrameContext& /*frame*/,
        std::vector<rg::RenderPassDesc>& /*out*/)
    {
    }
};

} // namespace gte
