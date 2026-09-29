#pragma once

// editor-core-separation-3 campaign, PHASE3
// (PHASE3_RUNTIME_RENDER_FEATURE_CAPABILITY.md) - the gte_core-side
// implementation of IPluginRenderPassBuilder - constructed FRESH, once per
// currently-visible view, per frame, by the "PluginRenderFeatures" provider
// (Core.cpp) - never held past the end of that provider's own lambda
// invocation. Forwards AddFullscreenClearPass() into a real
// rg::RenderGraphBuilder::AddRenderPass() call against the real target this
// frame's view actually resolved to - see .cpp.

#include "../../../plugins/gte_plugin_abi/IPluginRenderPassBuilder.h"
#include "../../Renderer/RenderGraph/RenderGraphTypes.h"

namespace gte {
namespace rg {
class RenderGraphBuilder;
class RenderPassToggleRegistry;
} // namespace rg

class PluginRenderPassBuilderAdapter final : public IPluginRenderPassBuilder {
public:
    // editor-core-separation-21 campaign, PHASE4 (fixing a confirmed lie
    // from PHASE3's own audit, findings #18/#19) - `toggleRegistry` is
    // OPTIONAL (nullptr-safe, mirroring AtmosphereLutRenderer's own
    // established default-nullptr precedent) so AddFullscreenClearPass()
    // below can honestly consult RenderPassToggleRegistry::
    // NoteDeclaredAndCheckEnabled(debugName) before ever calling
    // m_builder.AddRenderPass() - the ONE real construction site
    // (LegacyRenderFeatureOrchestrator::ContributeRenderGraphPasses())
    // always passes a real, non-null pointer; nullptr is only kept as a
    // safety fallback (treated as "always enabled", zero behavior change)
    // for any future construction site that doesn't have one handy.
    PluginRenderPassBuilderAdapter(rg::RenderGraphBuilder& builder, rg::TextureHandle viewTarget,
        rg::RenderPassToggleRegistry* toggleRegistry = nullptr) noexcept
        : m_builder(builder)
        , m_viewTarget(viewTarget)
        , m_toggleRegistry(toggleRegistry)
    {
    }

    void AddFullscreenClearPass(const char* debugName, float r, float g, float b, float a) override;

private:
    rg::RenderGraphBuilder& m_builder;
    rg::TextureHandle m_viewTarget;
    rg::RenderPassToggleRegistry* m_toggleRegistry = nullptr;
};

} // namespace gte
