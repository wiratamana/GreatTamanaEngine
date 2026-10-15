#include "PassChain.h"

#include "RenderGraphBuilder.h"
#include "ShaderBindings.h"

#include <cassert>
#include <utility>

namespace gte::rg {

namespace {
constexpr const char* kLogCategory = "PassChain";
} // namespace

PassChainState::PassChainState(
    RenderGraphBuilder& builder, Shader& shader, ShaderKind kind, StaticName name, ViewScope scope, RenderPassEvent event)
    : builder(builder)
    , shader(shader)
    , name(name)
    , scope(scope)
    , event(event)
{
    if (shader.Kind() != kind) {
        inert = true;
        Report(kind == ShaderKind::Compute ? "the shader is a graphics shader, not a compute shader"
                                           : "the shader is a compute shader, not a graphics shader");
        return;
    }
    inert = !shader.EnsureBuilt(builder);
}

void PassChainState::Report(const std::string& text)
{
    builder.ReportOnce(kLogCategory, std::string("Pass '") + name.value + "': " + text);
}

bool PassChainState::Begin(const char* terminal)
{
    if (finished) {
        Report(std::string(terminal) + "() called twice on one chain");
        return false;
    }
    finished = true;
    return !inert && !failed;
}

void PassChainState::ReportUnfinished(const char* terminal)
{
    if (inert || finished) {
        return;
    }
    assert(false && "pass chain destroyed without its terminal call");
    Report(std::string("chain ended without ") + terminal + "(): nothing was declared");
}

std::optional<BindingPlan> PassChainState::BuildPlan()
{
    std::vector<BindingArgument> bound;
    bound.reserve(arguments.size());
    for (const Argument& a : arguments) {
        BindingArgument out;
        out.name = a.name;
        out.kind = a.isBuffer ? BindingResourceKind::Buffer
                              : (a.isDepth ? BindingResourceKind::DepthTexture : BindingResourceKind::Texture);
        out.texture = a.texture;
        out.buffer = a.buffer;
        bound.push_back(out);
    }

    const DescribeTextureFn describe = [this](TextureHandle handle) { return builder.DescOf(handle); };
    BindingPlanResult planned = PlanBindings(shader.ReflectedBindings(), bound, describe);
    if (!planned.Ok()) {
        Report(planned.error);
        return std::nullopt;
    }
    if (const auto pushError = ValidatePushConstantSize(shader.PushConstantSize(), static_cast<std::uint32_t>(push.size()))) {
        Report(*pushError);
        return std::nullopt;
    }
    return std::move(planned.plan);
}

} // namespace gte::rg
