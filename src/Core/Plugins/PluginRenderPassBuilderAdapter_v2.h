#pragma once

// editor-core-separation-6 campaign, PHASE4
// (PHASE4_RENDER_FEATURE_COMPOSITOR_CORE_AND_ORDERING.md, Step 3.2) - the
// gte_core-side implementation of IPluginRenderPassBuilder_v2 - constructed
// FRESH per plugin, per currently-visible view, per frame, by
// RenderFeatureCompositor::ContributeRenderGraphPasses() - never held past
// the end of that method's own per-entry loop iteration. Every one of the 3
// drawing operations forwards into ONE shared helper,
// RenderFeatureCompositor::DispatchOps(), differing only in which `opCode`
// (0/1/2) and which push-constant fields it fills - mirrors
// PluginRenderPassBuilderAdapter (v1)'s own single-method shape, just with 3
// methods instead of 1.

#include "../../../plugins/gte_plugin_abi/IPluginRenderPassBuilder_v2.h"
#include "../../Renderer/RenderGraph/RenderGraphTypes.h"

namespace gte {
namespace rg {
class RenderGraphBuilder;
} // namespace rg

class RenderFeatureCompositor;

class PluginRenderPassBuilderAdapter_v2 final : public IPluginRenderPassBuilder_v2 {
public:
    PluginRenderPassBuilderAdapter_v2(rg::RenderGraphBuilder& builder, rg::TextureHandle privateTarget,
        RenderFeatureCompositor& compositor, const char* privateTargetStateKey) noexcept
        : m_builder(builder)
        , m_privateTarget(privateTarget)
        , m_compositor(compositor)
        , m_privateTargetStateKey(privateTargetStateKey)
    {
    }

    void AddSolidFillPass(const char* debugName, float r, float g, float b, float a) override;
    void AddRadialVignettePass(const char* debugName, float centerX, float centerY, float innerRadius,
        float outerRadius, float r, float g, float b, float a) override;
    void AddColorGradePass(const char* debugName, float brightness, float contrast, float saturation, float tintR,
        float tintG, float tintB, float tintStrength) override;

private:
    rg::RenderGraphBuilder& m_builder;
    rg::TextureHandle m_privateTarget;
    RenderFeatureCompositor& m_compositor;
    // The SAME interned name RenderFeatureNamePool handed out for this
    // (plugin, view) pair's private-target slot - see
    // RenderFeatureCompositor::EnsurePrivateTargetState()/DispatchOps().
    const char* m_privateTargetStateKey;
};

} // namespace gte
