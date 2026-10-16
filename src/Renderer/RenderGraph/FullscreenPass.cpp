#include "FullscreenPass.h"

#include "FullscreenReplay.h"
#include "PassKindDefaults.h"
#include "RenderGraph.h"
#include "RenderGraphBuilder.h"
#include "Shader.h"
#include "ShaderBindings.h"
#include "../Pipeline.h"
#include "../Renderer.h"

#include <cstdint>
#include <memory>
#include <utility>

namespace gte::rg {

namespace {

// The execute step shared by the declared pass and its replay.
void RecordFullscreen(PassContext& ctx, const Pipeline& pipeline, ShaderStageMask pushStages, const std::vector<std::byte>& push)
{
    const DescriptorSetHandle set = ctx.BoundDescriptorSet();
    if (set == DescriptorSetHandle::Invalid) {
        return; // The graph refused the bindings; drawing with a stale set is unsafe.
    }
    auto cmd = ctx.Cmd();
    cmd.BindPipeline(pipeline);
    cmd.BindDescriptorSet(0, set);
    if (!push.empty()) {
        cmd.PushConstants(push.data(), static_cast<std::uint32_t>(push.size()), pushStages);
    }
    cmd.Draw(3);
}

} // namespace

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
    m_targets.assign(1, target);
    m_clear = clear;
    return *this;
}

FullscreenPass& FullscreenPass::Targets(
    std::initializer_list<TextureHandle> targets, std::optional<std::array<float, 4>> clear)
{
    m_targets.assign(targets.begin(), targets.end());
    m_clear = clear;
    return *this;
}

FullscreenPass& FullscreenPass::DepthTest(TextureHandle depth, CompareOp op, bool write)
{
    m_depth = depth;
    m_depthCompare = op;
    m_depthWrite = write;
    return *this;
}

FullscreenPass& FullscreenPass::Blend(BlendMode blend)
{
    m_blend = blend;
    return *this;
}

bool FullscreenPass::Draw(FullscreenReplay* replay)
{
    if (!m_state.Begin("Draw")) {
        return false;
    }
    std::optional<BindingPlan> planned = m_state.BuildPlan();
    if (!planned.has_value()) {
        return false;
    }

    RenderGraphBuilder& builder = m_state.builder;
    std::vector<std::optional<TextureDesc>> targetDescs;
    targetDescs.reserve(m_targets.size());
    for (const TextureHandle target : m_targets) {
        targetDescs.push_back(target.IsValid() ? builder.DescOf(target) : std::nullopt);
    }

    FullscreenDepthRequest depth;
    if (m_depth.has_value()) {
        depth.enabled = true;
        depth.desc = m_depth->IsValid() ? builder.DescOf(*m_depth) : std::nullopt;
        depth.compare = m_depthCompare;
        depth.write = m_depthWrite;
    }

    Renderer* renderer = builder.RendererOrNull();
    const PixelFormat rendererDepthFormat = renderer != nullptr ? renderer->DepthPixelFormat() : PixelFormat::Undefined;
    const FullscreenAttachmentPlan attachments = PlanFullscreenAttachments(targetDescs, depth, rendererDepthFormat, m_blend);
    if (!attachments.Ok()) {
        m_state.Report(attachments.error);
        return false;
    }

    const Pipeline* pipeline = m_state.shader.GraphicsPipeline(builder, attachments.key);
    if (pipeline == nullptr) {
        return false; // The shader logged the build failure and retries on a cooldown.
    }
    const DescriptorSetLayoutHandle layout = m_state.shader.Layout();
    const ShaderStageMask pushStages = m_state.shader.PushStages();
    const auto push = std::make_shared<const std::vector<std::byte>>(std::move(m_state.push));

    builder.AddRenderPass(
        m_state.name.value, DefaultPassKind(DeclarativePassType::Fullscreen), m_state.scope, RenderPassCategory::General,
        [&](RenderGraphBuilder::PassBuilder& pass) {
            DeclareFullscreen(pass, *planned, layout, m_targets, m_clear, m_depthAcknowledgements, m_depth);
        },
        [pipeline, pushStages, push](PassContext& ctx) { RecordFullscreen(ctx, *pipeline, pushStages, *push); },
        DefaultDrawKind(DeclarativePassType::Fullscreen), m_state.event);

    if (replay != nullptr) {
        const auto plan = std::make_shared<const BindingPlan>(std::move(*planned));
        replay->declare = [plan, layout](RenderGraphBuilder::PassBuilder& pass) { DeclareBindings(pass, *plan, layout); };
        replay->record = [pipeline, pushStages, push](PassContext& ctx) { RecordFullscreen(ctx, *pipeline, pushStages, *push); };
    }
    return true;
}

} // namespace gte::rg
