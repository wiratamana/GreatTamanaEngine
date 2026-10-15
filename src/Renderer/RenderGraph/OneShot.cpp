#include "OneShot.h"

#include "OneShotPlan.h"
#include "PassKindDefaults.h"
#include "RecordedPass.h"
#include "RenderGraph.h"
#include "TransferPass.h"
#include "../Renderer.h"
#include "../VolumeTarget.h"
#include "../VulkanFormatInfo.h"

#include <deque>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gte::rg {

struct OneShot::Impl {
    // An imported texture whose tracked state is written back after a successful run.
    struct Tracked {
        TextureHandle handle;
        RenderTexture* renderTexture = nullptr;
        Texture2D* texture = nullptr;
        ResourceState initial;
        FinalState final = FinalState::Restore;
    };

    Impl(Renderer& renderer, RenderGraph& graph)
        : graph(graph)
    {
        builder.SetRenderer(&renderer);
        builder.SetOnceLog(graph.OnceLog());
        builder.SetPersistentResourceCache(nullptr, ExecuteTimingMode::SynchronousImmediateReadback, graph.NextOneShotFrame());
    }

    const char* NextName()
    {
        return names.emplace_back("OneShot.Texture" + std::to_string(names.size())).c_str();
    }

    RenderGraph& graph;
    RenderGraphBuilder builder;
    std::deque<std::string> names; // Import names must outlive the builder; a deque keeps them in place.
    std::vector<Tracked> tracked;
    bool ran = false;
};

OneShot::OneShot(Renderer& renderer)
    : m_impl(std::make_unique<Impl>(renderer, renderer.OneShotGraph()))
{
}

OneShot::~OneShot() = default;

RenderGraphBuilder& OneShot::Builder() noexcept
{
    return m_impl->builder;
}

TextureHandle OneShot::ImportReadOnly(const RenderTexture& texture)
{
    return ImportReadOnly(texture, texture.CurrentState());
}

TextureHandle OneShot::ImportReadOnly(const RenderTexture& texture, const ResourceState& current)
{
    const TextureHandle handle = m_impl->builder.ImportTexture(m_impl->NextName(), texture.Target(),
        FromVkImageLayout(current.layout), texture.SamplerObjectHandle(), texture.DepthSamplerObjectHandle());
    m_impl->tracked.push_back(Impl::Tracked{ handle, nullptr, nullptr, current, FinalState::Restore });
    return handle;
}

TextureHandle OneShot::Import(RenderTexture& texture, FinalState final)
{
    const TextureHandle handle = ImportReadOnly(texture);
    m_impl->tracked.back().renderTexture = &texture;
    m_impl->tracked.back().final = final;
    return handle;
}

TextureHandle OneShot::Import(Texture2D& texture, FinalState final)
{
    RenderTarget target;
    target.image = texture.Image();
    target.imageView = texture.View();
    target.extent = ToVkExtent2D(texture.Extent());
    target.format = texture.NativeFormat();

    const ResourceState state = texture.CurrentState();
    const TextureHandle handle =
        m_impl->builder.ImportTexture(m_impl->NextName(), target, FromVkImageLayout(state.layout));
    m_impl->tracked.push_back(Impl::Tracked{ handle, nullptr, &texture, state, final });
    return handle;
}

TextureHandle OneShot::Import(
    const VolumeTarget& volume, const ResourceState& current, SamplerHandle sampler, FinalState final)
{
    const TextureHandle handle =
        m_impl->builder.ImportVolumeTexture(m_impl->NextName(), volume, FromVkImageLayout(current.layout), sampler);
    m_impl->tracked.push_back(Impl::Tracked{ handle, nullptr, nullptr, current, final });
    return handle;
}

bool OneShot::CopyTexture(TextureHandle src, TextureHandle dst)
{
    constexpr const char* kPassName = "OneShot.Copy";
    std::optional<PassBody> body =
        MakeTransferBody(m_impl->builder, kPassName, TransferRecord::CopyTexture(src, dst));
    if (!body.has_value()) {
        return false;
    }
    m_impl->builder.AddRenderPass(kPassName, DefaultPassKind(DeclarativePassType::Transfer), ViewScope::Shared,
        RenderPassCategory::General, std::move(body->setup), std::move(body->execute),
        DefaultDrawKind(DeclarativePassType::Transfer));
    return true;
}

bool OneShot::Run()
{
    Impl& impl = *m_impl;
    if (impl.ran) {
        impl.graph.OnceLog()->Error("OneShot", "Run() called twice on one OneShot");
        return false;
    }
    impl.ran = true;

    std::vector<TextureHandle> outputs;
    std::vector<RenderGraph::OneShotFinal> finals;
    for (const Impl::Tracked& tracked : impl.tracked) {
        outputs.push_back(tracked.handle);
        finals.push_back(RenderGraph::OneShotFinal{ tracked.handle, PlanFinalState(tracked.initial, tracked.final) });
    }

    std::vector<std::optional<ResourceState>> states;
    if (!impl.graph.RunOneShot(impl.builder.Finish(), outputs, finals, states)) {
        return false;
    }
    WriteBackStates(outputs, states, [&impl](std::size_t i, const ResourceState& state) {
        const Impl::Tracked& tracked = impl.tracked[i];
        if (tracked.renderTexture != nullptr) {
            tracked.renderTexture->SetCurrentState(state);
        } else if (tracked.texture != nullptr) {
            tracked.texture->SetCurrentState(state);
        }
    });
    return true;
}

} // namespace gte::rg
