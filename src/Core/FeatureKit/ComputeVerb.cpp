#include "ComputeChain.h"

#include "ComputeShader.h"
#include "VerbSupport.h"
#include "../../Renderer/RenderGraph/ComputePass.h"

#include <optional>
#include <utility>

namespace gte::fx {

// `pass` is empty when the chain is inert: pass toggled off, or the frame has no engine context.
struct ComputeChain::Impl {
    Impl(Frame& owner, rg::StaticName passName) noexcept
        : frame(&owner)
        , name(passName)
    {
    }

    Frame* frame;
    rg::StaticName name;
    std::optional<rg::ComputePass> pass;
};

ComputeChain Frame::Compute(ComputeShader& shader, rg::StaticName passName)
{
    auto impl = std::make_unique<ComputeChain::Impl>(*this, passName);
    if (m_context == nullptr || m_context->builder == nullptr) {
        ReportOnce("Compute() needs a frame that belongs to a render callback");
    } else if (detail::PassEnabled(*m_context, passName)) {
        impl->pass.emplace(*m_context->builder, detail::ShaderOf(shader), passName, m_context->scope, m_context->event);
    }
    return ComputeChain(std::move(impl));
}

ComputeChain::ComputeChain(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl))
{
}

ComputeChain::~ComputeChain() = default;
ComputeChain::ComputeChain(ComputeChain&&) noexcept = default;
ComputeChain& ComputeChain::operator=(ComputeChain&&) noexcept = default;

ComputeChain& ComputeChain::Bind(std::string_view name, Texture texture)
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

ComputeChain& ComputeChain::Bind(std::string_view name, DepthRef depth)
{
    if (m_impl && m_impl->pass) {
        if (detail::CheckOwned(*m_impl->frame, depth.texture, m_impl->name, name)) {
            m_impl->pass->Bind(name, rg::DepthRef{ depth.texture.handle });
        } else {
            m_impl->pass->Abandon();
        }
    }
    return *this;
}

ComputeChain& ComputeChain::BindBuffer(std::string_view name, GpuBufferRef buffer)
{
    if (m_impl && m_impl->pass) {
        if (!detail::CheckOwned(*m_impl->frame, buffer, m_impl->name, name)) {
            m_impl->pass->Abandon();
        } else if (!rg::HasUse(buffer.use, BufferUse::Storage)) {
            m_impl->frame->ReportOnce(std::string("Pass '") + m_impl->name.value + "': '" + std::string(name)
                + "' was not created with BufferUse::Storage");
            m_impl->pass->Abandon();
        } else {
            m_impl->pass->Bind(name, buffer.handle);
        }
    }
    return *this;
}

ComputeChain& ComputeChain::BindReadback(std::string_view name, detail::ReadbackCore& readback)
{
    if (m_impl && m_impl->pass) {
        if (const auto binding = readback.Acquire(*m_impl->frame)) {
            m_impl->pass->Bind(name, binding->device);
        } else {
            m_impl->pass->Abandon();
        }
    }
    return *this;
}

ComputeChain& ComputeChain::PushBytes(const void* data, std::size_t size)
{
    if (m_impl && m_impl->pass) {
        m_impl->pass->PushBytes(data, size);
    }
    return *this;
}

ComputeChain& ComputeChain::ParamsBytes(
    std::string_view bindingName, rg::GpuParamsBuffer& params, const void* data, std::size_t size)
{
    if (m_impl && m_impl->pass) {
        m_impl->pass->ParamsBytes(bindingName, params, data, size);
    }
    return *this;
}

bool ComputeChain::Dispatch(Extent2D size, std::uint32_t depth)
{
    if (!m_impl || !m_impl->pass) {
        return false;
    }
    const bool declared = m_impl->pass->Dispatch(size, depth);
    m_impl->pass.reset();
    return declared;
}

} // namespace gte::fx
