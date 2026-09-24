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
} // namespace rg

class PluginRenderPassBuilderAdapter final : public IPluginRenderPassBuilder {
public:
    PluginRenderPassBuilderAdapter(rg::RenderGraphBuilder& builder, rg::TextureHandle viewTarget) noexcept
        : m_builder(builder)
        , m_viewTarget(viewTarget)
    {
    }

    void AddFullscreenClearPass(const char* debugName, float r, float g, float b, float a) override;

private:
    rg::RenderGraphBuilder& m_builder;
    rg::TextureHandle m_viewTarget;
};

} // namespace gte
