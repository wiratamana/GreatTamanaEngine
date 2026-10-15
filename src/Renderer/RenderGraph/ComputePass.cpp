#include "ComputePass.h"

#include "RenderGraph.h"
#include "RenderGraphBuilder.h"
#include "Shader.h"
#include "ShaderBindings.h"
#include "../Compute/ComputePipeline.h"
#include "../../Core/Logging.h"

#include <cassert>
#include <optional>
#include <string>
#include <utility>

namespace gte::rg {

namespace {
constexpr const char* kLogCategory = "ComputePass";
} // namespace

ComputePass::ComputePass(
    RenderGraphBuilder& builder, Shader& shader, StaticName name, ViewScope scope, RenderPassEvent event)
    : m_builder(builder)
    , m_shader(shader)
    , m_name(name)
    , m_scope(scope)
    , m_event(event)
    , m_inert(!shader.EnsureBuilt(builder))
{
}

ComputePass::~ComputePass()
{
    if (m_inert || m_dispatched) {
        return;
    }
    assert(false && "ComputePass destroyed without Dispatch()");
    Report("chain ended without Dispatch(): nothing was declared");
}

ComputePass& ComputePass::Bind(std::string_view name, TextureHandle texture)
{
    Add(Argument{ name, texture, BufferObjectHandle::Invalid, false, false });
    return *this;
}

ComputePass& ComputePass::Bind(std::string_view name, DepthRef depth)
{
    Add(Argument{ name, depth.texture, BufferObjectHandle::Invalid, false, true });
    return *this;
}

ComputePass& ComputePass::Bind(std::string_view name, BufferObjectHandle buffer)
{
    Add(Argument{ name, TextureHandle{}, buffer, true, false });
    return *this;
}

void ComputePass::Add(const Argument& argument)
{
    if (!m_inert) {
        m_arguments.push_back(argument);
    }
}

void ComputePass::Dispatch(Extent2D size, std::uint32_t depth)
{
    Finish(size, /*useImageExtent=*/false, depth);
}

void ComputePass::Dispatch()
{
    Finish(Extent2D{}, /*useImageExtent=*/true, 1);
}

void ComputePass::Report(const std::string& text)
{
    const std::string message = std::string("Pass '") + m_name.value + "': " + text;
    if (m_shader.NoteOnce(message)) {
        GTE_LOG_ERROR(kLogCategory, message);
    }
}

void ComputePass::Finish(Extent2D requested, bool useImageExtent, std::uint32_t depth)
{
    if (m_dispatched) {
        Report("Dispatch() called twice on one chain");
        return;
    }
    m_dispatched = true;
    if (m_inert || m_failed) {
        return;
    }

    std::vector<BindingArgument> arguments;
    arguments.reserve(m_arguments.size());
    for (const Argument& a : m_arguments) {
        BindingArgument out;
        out.name = a.name;
        out.kind = a.isBuffer ? BindingResourceKind::Buffer
                              : (a.isDepth ? BindingResourceKind::DepthTexture : BindingResourceKind::Texture);
        out.texture = a.texture;
        out.buffer = a.buffer;
        arguments.push_back(out);
    }

    const DescribeTextureFn describe = [this](TextureHandle handle) { return m_builder.DescOf(handle); };
    BindingPlanResult planned = PlanBindings(m_shader.ReflectedBindings(), arguments, describe);
    if (!planned.Ok()) {
        Report(planned.error);
        return;
    }
    if (const auto pushError = ValidatePushConstantSize(
            m_shader.HasPushConstants(), m_shader.PushConstantSize(), static_cast<std::uint32_t>(m_push.size()))) {
        Report(*pushError);
        return;
    }

    // The first written storage image bounds the dispatch (a persistent target resizes one frame late).
    std::optional<Extent2D> imageExtent;
    for (const PlannedBinding& binding : planned.plan.bindings) {
        if (binding.type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE && binding.access != ReflectedAccess::ReadOnly) {
            if (const auto desc = m_builder.DescOf(binding.texture)) {
                imageExtent = Extent2D{ desc->width, desc->height };
            }
            break;
        }
    }

    Extent2D size = requested;
    if (useImageExtent) {
        if (!imageExtent.has_value()) {
            Report("Dispatch() without a size needs a written storage image to take the size from");
            return;
        }
        size = *imageExtent;
    } else if (imageExtent.has_value()) {
        size = ClampDispatchSize(requested, *imageExtent);
    }
    if (size.width == 0 || size.height == 0 || depth == 0) {
        Report("dispatch size has a zero dimension");
        return;
    }

    const ComputePipeline* pipeline = m_shader.Pipeline();
    const DescriptorSetLayoutHandle layout = m_shader.Layout();
    std::optional<std::string> declareError;

    m_builder.AddRenderPass(
        m_name.value, PassKind::Compute, m_scope, RenderPassCategory::General,
        [&](RenderGraphBuilder::PassBuilder& pass) { declareError = DeclareBindings(pass, planned.plan, layout); },
        [pipeline, push = std::move(m_push), size, depth](PassContext& ctx) {
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
        RenderPassDrawKind::DrawMesh, m_event);

    if (declareError.has_value()) {
        Report(*declareError);
    }
}

} // namespace gte::rg
