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
void RecordFullscreen(PassContext& ctx, const Pipeline& pipeline, ShaderStageMask pushStages,
    const std::vector<std::byte>& push, const std::optional<IndirectArgsRef>& indirect)
{
    const DescriptorSetHandle set = ctx.BoundDescriptorSet();
    if (set == DescriptorSetHandle::Invalid) {
        return; // The graph refused the bindings; drawing with a stale set is unsafe.
    }
    VkBuffer commands = VK_NULL_HANDLE;
    if (indirect.has_value()) {
        commands = ctx.resolveBuffer(indirect->buffer);
        if (commands == VK_NULL_HANDLE) {
            ctx.ReportOnce("FullscreenPass", "indirect draw skipped: the args buffer did not resolve");
            return;
        }
    }
    auto cmd = ctx.Cmd();
    cmd.BindPipeline(pipeline);
    cmd.BindDescriptorSet(0, set);
    if (!push.empty()) {
        cmd.PushConstants(push.data(), static_cast<std::uint32_t>(push.size()), pushStages);
    }
    if (indirect.has_value()) {
        cmd.DrawIndirect(commands, indirect->byteOffset);
        if (ctx.recordIndirectDraw) {
            ctx.recordIndirectDraw();
        }
    } else {
        cmd.Draw(3);
    }
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

FullscreenPass& FullscreenPass::IndirectArgs(BufferHandle args, std::uint64_t byteOffset)
{
    m_indirect = IndirectArgsRef{ args, byteOffset };
    return *this;
}

FullscreenPass& FullscreenPass::ParamsBytes(
    std::string_view bindingName, GpuParamsBuffer& params, const void* data, std::size_t size)
{
    if (m_state.inert) {
        return *this;
    }
    if (!params.Upload(m_state.builder, data, size, m_state.name.value)) {
        m_state.failed = true;
        return *this;
    }
    return Bind(bindingName, params.Handle(m_state.builder));
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
    if (m_indirect.has_value()) {
        if (const auto error = ValidateIndirectArgs(builder.DescOfBuffer(m_indirect->buffer), m_indirect->byteOffset)) {
            m_state.Report(*error);
            return false;
        }
    }

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
    const std::optional<IndirectArgsRef> indirect = m_indirect;

    builder.AddRenderPass(
        m_state.name.value, DefaultPassKind(DeclarativePassType::Fullscreen), m_state.scope, RenderPassCategory::General,
        [&](RenderGraphBuilder::PassBuilder& pass) {
            DeclareFullscreen(pass, *planned, layout, m_targets, m_clear, m_depthAcknowledgements, m_depth, indirect);
        },
        [pipeline, pushStages, push, indirect](PassContext& ctx) {
            RecordFullscreen(ctx, *pipeline, pushStages, *push, indirect);
        },
        DefaultDrawKind(DeclarativePassType::Fullscreen), m_state.event);

    if (replay != nullptr) {
        const auto plan = std::make_shared<const BindingPlan>(std::move(*planned));
        replay->declare = [plan, layout, indirect](RenderGraphBuilder::PassBuilder& pass) {
            DeclareBindings(pass, *plan, layout, BindingDomain::Graphics);
            if (indirect.has_value()) {
                pass.ReadBuffer(indirect->buffer, ResourceAccess::IndirectCommandRead);
            }
        };
        replay->record = [pipeline, pushStages, push, indirect](PassContext& ctx) {
            RecordFullscreen(ctx, *pipeline, pushStages, *push, indirect);
        };
    }
    return true;
}

} // namespace gte::rg
