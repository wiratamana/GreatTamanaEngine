#include "TransientTarget.h"

#include "FrameContext.h"
#include "../../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../../Renderer/RenderGraph/ViewScopeUtil.h"

#include <algorithm>
#include <array>
#include <string>

namespace gte::fx {

struct TransientTarget::Impl {
    explicit Impl(std::string_view base)
    {
        for (std::size_t i = 0; i < rg::kViewScopeCount; ++i) {
            names[i] = PersistentName(base, static_cast<rg::ViewScope>(i));
        }
    }

    std::array<std::string, rg::kViewScopeCount> names; // The graph keeps the c_str() for the frame.
};

TransientTarget::TransientTarget(std::string_view baseName)
    : m_impl(std::make_unique<Impl>(baseName))
{
}

TransientTarget::~TransientTarget() = default;

Texture TransientTarget::Acquire(Frame& frame, Extent2D extent, PixelFormat format, bool storage)
{
    detail::FrameContext* ctx = frame.Context();
    if (ctx == nullptr || ctx->builder == nullptr) {
        frame.ReportOnce("TransientTarget::Acquire needs a frame that belongs to a render callback");
        return Texture{};
    }
    if (format == PixelFormat::Undefined) {
        frame.ReportOnce("TransientTarget::Acquire needs an explicit pixel format");
        return Texture{};
    }

    rg::TextureDesc desc;
    desc.width = std::max(1u, extent.width);
    desc.height = std::max(1u, extent.height);
    desc.format = format;
    desc.usage = storage ? rg::TextureUsage::Storage : rg::TextureUsage::Sampled;

    const std::size_t index = rg::ViewScopeIndex(ctx->scope);
    const rg::TextureHandle handle = ctx->builder->CreateTexture(m_impl->names[index].c_str(), desc);
    if (!handle.IsValid()) {
        return Texture{};
    }
    ctx->builder->KeepTextureOutput(handle);
    return frame.Stamp(handle);
}

} // namespace gte::fx
