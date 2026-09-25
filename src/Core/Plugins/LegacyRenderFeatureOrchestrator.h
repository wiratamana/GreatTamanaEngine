#pragma once

#include "IPluginCapabilityOrchestrator.h"

namespace gte {

class Core; // forward declaration only - this header must not #include "../Core.h"
            // (that would be a circular include: Core.h itself will gain a member
            // of type std::unique_ptr<IPluginCapabilityOrchestrator>, PHASE2 Step 3.4,
            // and LegacyRenderFeatureOrchestrator.h is reachable from Core.cpp's own
            // #include list). The .cpp file #includes "../Core.h" for the real
            // Core& method calls (Core::FindPluginRenderFeatureTarget()/GetPluginHost()).

// editor-core-separation-6 campaign, PHASE2
// (PHASE2_PLUGIN_CAPABILITY_ORCHESTRATOR_REGISTRY_AND_RENDER_FEATURE_MIGRATION.md)
// - a VERBATIM relocation of Core::LoadPlugins()'s own multi-plugin warning
// (editor-core-separation-4, PHASE5) and Core::RegisterOffscreenRenderPipelineProviders()'s
// own "PluginRenderFeatures" IRenderFeatureModule_v1 loop
// (editor-core-separation-3, PHASE3) into the new IPluginCapabilityOrchestrator
// shape - ZERO observable behavior change (same warning text, same pass
// names). This is the PERMANENT home for the _v1 "last write wins, shared
// handle" legacy path (PHASE0_MASTER_STRATEGY.md Locked Design Decision #9)
// - _v1 plugins never migrate onto RenderFeatureCompositor (PHASE4/PHASE5).
class LegacyRenderFeatureOrchestrator final : public IPluginCapabilityOrchestrator {
public:
    // `core` is the SAME Core instance constructing this orchestrator inside
    // Core::RegisterBuiltinCapabilityOrchestrators() (Step 3.4 below,
    // `std::make_unique<LegacyRenderFeatureOrchestrator>(*this)`) - stored as a
    // reference, never a pointer, since it must outlive this orchestrator for
    // Core's entire remaining lifetime (this orchestrator lives inside
    // Core::m_capabilityOrchestrators, a member of that same Core instance).
    explicit LegacyRenderFeatureOrchestrator(Core& core) noexcept : m_core(core) { }

    void OnPluginsLoaded(const std::vector<IPluginModule*>& modules) override;
    void ContributeRenderGraphPasses(
        const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out) override;

private:
    Core& m_core;
};

} // namespace gte
