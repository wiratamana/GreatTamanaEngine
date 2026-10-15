#include "ComputePass.h"

#include "PassKindDefaults.h"
#include "RenderGraph.h"
#include "RenderGraphBuilder.h"
#include "Shader.h"
#include "ShaderBindings.h"
#include "../Compute/ComputePipeline.h"

#include <utility>

namespace gte::rg {

ComputePass::ComputePass(
    RenderGraphBuilder& builder, Shader& shader, StaticName name, ViewScope scope, RenderPassEvent event)
    : PassChain<ComputePass>(builder, shader, ShaderKind::Compute, name, scope, event)
{
}

ComputePass::~ComputePass()
{
    m_state.ReportUnfinished("Dispatch");
}

ComputePass& ComputePass::ParamsBytes(GpuParamsBuffer& params, const void* data, std::size_t size)
{
    if (m_state.inert) {
        return *this;
    }
    if (!params.Upload(m_state.builder, data, size, m_state.name.value)) {
        m_state.failed = true;
        return *this;
    }
    return Bind("params", params.Handle());
}

bool ComputePass::Dispatch(Extent2D requested, std::uint32_t depth)
{
    if (!m_state.Begin("Dispatch")) {
        return false;
    }
    std::optional<BindingPlan> planned = m_state.BuildPlan();
    if (!planned.has_value()) {
        return false;
    }

    RenderGraphBuilder& builder = m_state.builder;

    // The first written storage image bounds the dispatch (a persistent target resizes one frame late).
    Extent2D size = requested;
    for (const PlannedBinding& binding : planned->bindings) {
        if (binding.type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE && binding.access != ReflectedAccess::ReadOnly) {
            if (const auto desc = builder.DescOf(binding.texture)) {
                size = ClampDispatchSize(requested, Extent2D{ desc->width, desc->height });
            }
            break;
        }
    }
    if (size.width == 0 || size.height == 0 || depth == 0) {
        m_state.Report("dispatch size has a zero dimension");
        return false;
    }

    const ComputePipeline* pipeline = m_state.shader.Compute();
    const DescriptorSetLayoutHandle layout = m_state.shader.Layout();

    builder.AddRenderPass(
        m_state.name.value, DefaultPassKind(DeclarativePassType::Compute), m_state.scope, RenderPassCategory::General,
        [&](RenderGraphBuilder::PassBuilder& pass) { DeclareBindings(pass, *planned, layout); },
        [pipeline, push = std::move(m_state.push), size, depth](PassContext& ctx) {
            const DescriptorSetHandle set = ctx.BoundDescriptorSet();
            if (set == DescriptorSetHandle::Invalid) {
                return; // The graph refused the bindings; dispatching with a stale set is unsafe.
            }
            auto cmd = ctx.Cmd();
            cmd.BindComputePipeline(*pipeline);
            cmd.BindDescriptorSet(set);
            if (!push.empty()) {
                cmd.SetPushConstants(push.data(), static_cast<std::uint32_t>(push.size()));
            }
            cmd.DispatchOverSize(size.width, size.height, depth);
        },
        DefaultDrawKind(DeclarativePassType::Compute), m_state.event);
    return true;
}

} // namespace gte::rg
