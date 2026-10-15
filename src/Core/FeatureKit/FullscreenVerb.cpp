#include "FullscreenChain.h"

#include "FullscreenShader.h"
#include "VerbSupport.h"
#include "../../Renderer/RenderGraph/FullscreenPass.h"

#include <optional>
#include <utility>

namespace gte::fx {

namespace {

BlendMode ToBlendMode(FullscreenBlend blend) noexcept
{
    switch (blend) {
    case FullscreenBlend::AlphaBlend:
        return BlendMode::AlphaBlend;
    case FullscreenBlend::Additive:
        return BlendMode::Additive;
    case FullscreenBlend::Opaque:
        break;
    }
    return BlendMode::Opaque;
}

} // namespace

// `pass` is empty when the chain is inert: pass toggled off, or the frame has no engine context.
struct FullscreenChain::Impl {
    Impl(Frame& owner, rg::StaticName passName, rg::TextureHandle depth) noexcept
        : frame(&owner)
        , name(passName)
        , defaultDepth(depth)
    {
    }

    Frame* frame;
    rg::StaticName name;
    rg::TextureHandle defaultDepth; // Acknowledged at Draw() unless the author covered it.
    bool depthCovered = false;
    std::optional<rg::FullscreenPass> pass;
};

FullscreenChain Frame::Fullscreen(FullscreenShader& shader, rg::StaticName passName)
{
    auto impl = std::make_unique<FullscreenChain::Impl>(*this, passName, m_acknowledgedDepth);
    if (m_context == nullptr || m_context->builder == nullptr) {
        ReportOnce("Fullscreen() needs a frame that belongs to a render callback");
    } else if (detail::PassEnabled(*m_context, passName)) {
        impl->pass.emplace(*m_context->builder, detail::ShaderOf(shader), passName, m_context->scope, m_context->event);
        impl->pass->Blend(ToBlendMode(detail::BlendOf(shader)));
    }
    return FullscreenChain(std::move(impl));
}

FullscreenChain::FullscreenChain(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl))
{
}

FullscreenChain::~FullscreenChain() = default;
FullscreenChain::FullscreenChain(FullscreenChain&&) noexcept = default;
FullscreenChain& FullscreenChain::operator=(FullscreenChain&&) noexcept = default;

FullscreenChain& FullscreenChain::Bind(std::string_view name, Texture texture)
{
    if (m_impl && m_impl->pass) {
        if (detail::CheckOwned(*m_impl->frame, texture, m_impl->name, name)) {
            m_impl->pass->Bind(name, texture.handle);
        } else {
            m_impl->pass->Abandon();
        }
    }
    return *this;
}

FullscreenChain& FullscreenChain::Bind(std::string_view name, DepthRef depth)
{
    if (m_impl && m_impl->pass) {
        if (detail::CheckOwned(*m_impl->frame, depth.texture, m_impl->name, name)) {
            m_impl->pass->Bind(name, rg::DepthRef{ depth.texture.handle });
            m_impl->depthCovered = m_impl->depthCovered || depth.texture.handle == m_impl->defaultDepth;
        } else {
            m_impl->pass->Abandon();
        }
    }
    return *this;
}

FullscreenChain& FullscreenChain::PushBytes(const void* data, std::size_t size)
{
    if (m_impl && m_impl->pass) {
        m_impl->pass->PushBytes(data, size);
    }
    return *this;
}

FullscreenChain& FullscreenChain::Acknowledge(Texture depth)
{
    if (m_impl && m_impl->pass) {
        if (detail::CheckOwned(*m_impl->frame, depth, m_impl->name, "the acknowledged depth")) {
            m_impl->pass->Acknowledge(depth.handle);
            m_impl->depthCovered = m_impl->depthCovered || depth.handle == m_impl->defaultDepth;
        } else {
            m_impl->pass->Abandon();
        }
    }
    return *this;
}

FullscreenChain& FullscreenChain::Target(Texture target, std::optional<std::array<float, 4>> clear)
{
    if (m_impl && m_impl->pass) {
        if (detail::CheckOwned(*m_impl->frame, target, m_impl->name, "the target")) {
            m_impl->pass->Target(target.handle, clear);
        } else {
            m_impl->pass->Abandon();
        }
    }
    return *this;
}

bool FullscreenChain::Draw()
{
    if (!m_impl || !m_impl->pass) {
        return false;
    }
    if (!m_impl->depthCovered && m_impl->defaultDepth.IsValid()) {
        m_impl->pass->Acknowledge(m_impl->defaultDepth);
    }
    const bool declared = m_impl->pass->Draw();
    m_impl->pass.reset();
    return declared;
}

} // namespace gte::fx
