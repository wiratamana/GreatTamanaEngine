#include "FullscreenPass.h"

#include "RenderGraph.h"
#include "RenderGraphBuilder.h"
#include "Shader.h"
#include "ShaderBindings.h"
#include "../Pipeline.h"

#include <cstdint>
#include <utility>

namespace gte::rg {

FullscreenPass::FullscreenPass(
    RenderGraphBuilder& builder, Shader& shader, StaticName name, ViewScope scope, RenderPassEvent event)
    : PassChain<FullscreenPass>(builder, shader, ShaderKind::Graphics, name, scope, event)
{
}

FullscreenPass::~FullscreenPass()
{
    m_state.ReportUnfinished("Draw");
}

FullscreenPass& FullscreenPass::Acknowledge(TextureHandle depth)
{
    if (!m_state.inert) {
        m_depthAcknowledgements.push_back(depth);
    }
    return *this;
}

FullscreenPass& FullscreenPass::Target(TextureHandle target, std::optional<std::array<float, 4>> clear)
{
    m_target = target;
    m_clear = clear;
    return *this;
}

FullscreenPass& FullscreenPass::Blend(BlendMode blend)
{
    m_blend = blend;
    return *this;
}

bool FullscreenPass::Draw()
{
    if (!m_state.Begin("Draw")) {
        return false;
    }
    std::optional<BindingPlan> planned = m_state.BuildPlan();
    if (!planned.has_value()) {
        return false;
    }

    RenderGraphBuilder& builder = m_state.builder;
    const std::optional<TextureDesc> targetDesc = m_target.IsValid() ? builder.DescOf(m_target) : std::nullopt;
    if (const auto error = ValidateFullscreenTarget(targetDesc)) {
        m_state.Report(*error);
        return false;
    }

    const Pipeline* pipeline = m_state.shader.GraphicsPipeline(builder, FullscreenKey{ targetDesc->format, m_blend });
    if (pipeline == nullptr) {
        return false; // The shader logged the build failure and retries on a cooldown.
    }
    const DescriptorSetLayoutHandle layout = m_state.shader.Layout();
    const ShaderStageMask pushStages = m_state.shader.PushStages();

    builder.AddRenderPass(
        m_state.name.value, PassKind::Graphics, m_state.scope, RenderPassCategory::General,
        [&](RenderGraphBuilder::PassBuilder& pass) {
            DeclareFullscreen(pass, *planned, layout, m_target, m_clear, m_depthAcknowledgements);
        },
        [pipeline, pushStages, push = std::move(m_state.push)](PassContext& ctx) {
            const DescriptorSetHandle set = ctx.BoundDescriptorSet();
            if (set == DescriptorSetHandle::Invalid) {
                return; // The graph refused the bindings; drawing with a stale set is unsafe.
            }
            auto cmd = ctx.Cmd();
            cmd.BindPipeline(*pipeline);
            cmd.BindDescriptorSet(0, set);
            if (!push.empty()) {
                cmd.PushConstants(push.data(), static_cast<std::uint32_t>(push.size()), pushStages);
            }
            cmd.Draw(3);
        },
        RenderPassDrawKind::DrawQuad, m_state.event);
    return true;
}

} // namespace gte::rg
