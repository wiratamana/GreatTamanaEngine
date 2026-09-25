#pragma once

#include <vector>

// editor-core-separation-6 campaign, PHASE2
// (PHASE2_PLUGIN_CAPABILITY_ORCHESTRATOR_REGISTRY_AND_RENDER_FEATURE_MIGRATION.md)
// - RENDER_FEATURE_COMPOSITING_FINDINGS_AND_PROPOSAL_2026-09-25.md Section
// 3.5: the ONE generic, reusable interface for "when plugins finish
// loading, let every interested HOST subsystem discover and react to them" -
// replacing a bespoke, hand-written for-loop per capability kind
// (IRenderFeatureModule_v1's loop lived inline in Core::LoadPlugins()/
// Core::RegisterOffscreenRenderPipelineProviders(); IEditorPanelModule_v1's
// loop lived inline in EditorHost.cpp - see PHASE3 for that migration).
// This header lives in gte_core (never gte_plugin_abi - it is HOST-internal
// wiring, never crosses the plugin ABI boundary, so it is free to use
// std::vector/real gte_core types like rg::RenderPassFrameContext).

namespace gte {

class IPluginModule;

namespace rg {
struct RenderPassFrameContext;
struct RenderPassDesc;
} // namespace rg

// One instance per DISTINCT plugin capability kind Core cares about (render
// features today; editor panels, PHASE3; any future kind later). Core owns
// a plain std::vector<std::unique_ptr<IPluginCapabilityOrchestrator>>,
// populated ONCE, at construction time, by
// Core::RegisterBuiltinCapabilityOrchestrators() - adding a brand-new
// capability kind in the future means writing ONE new class implementing
// this interface and adding ONE line to that registration function, never
// touching Core::LoadPlugins()'s own body, never touching
// Core::RegisterOffscreenRenderPipelineProviders()'s own body, ever again.
class IPluginCapabilityOrchestrator {
public:
    virtual ~IPluginCapabilityOrchestrator() = default;

    // Called once, right after PluginHost::LoadPlugins() returns
    // (Core::LoadPlugins(), below). Implementations discover which loaded
    // modules answer their own capability's QueryCapability() name,
    // validate/order them (e.g. RenderFeatureCompositor's own
    // priority-collision detection, PHASE4), and log anything worth
    // knowing. Never draws/renders here - discovery and validation only.
    virtual void OnPluginsLoaded(const std::vector<IPluginModule*>& modules) = 0;

    // Optional hook - only orchestrators that actually affect the render
    // graph override this (RenderFeatureCompositor does, PHASE4/PHASE5; a
    // future EditorPanelCapabilityOrchestrator, PHASE3, does not need to,
    // since panels are drawn through Dear ImGui, not the render graph).
    // Default no-op. Signature matches "PluginRenderFeatures" provider's
    // own existing lambda parameters EXACTLY (Step 2 above) - no provider-
    // registration change needed anywhere.
    virtual void ContributeRenderGraphPasses(const rg::RenderPassFrameContext& /*frame*/,
        std::vector<rg::RenderPassDesc>& /*out*/)
    {
    }
};

} // namespace gte
