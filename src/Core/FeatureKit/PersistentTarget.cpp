#include "PersistentTarget.h"

#include "FrameContext.h"
#include "../Core.h"
#include "../../Renderer/RenderGraph/PersistentTarget.h"
#include "../../Renderer/RenderGraph/ViewScopeUtil.h"

#include <array>

namespace gte::fx {

std::string PersistentName(std::string_view base, rg::ViewScope scope, std::string_view viewLabel)
{
    std::string name(base);
    if (scope != rg::ViewScope::Shared) {
        name += '.';
        name += viewLabel;
    }
    return name;
}

std::string PersistentName(std::string_view base, const detail::FrameContext& context)
{
    const char* label = "";
    if (context.scope != rg::ViewScope::Shared) {
        label = context.core != nullptr ? context.core->ViewResourceName(context.scope)
                                        : (context.scope == rg::ViewScope::Secondary ? kSecondaryViewName : kPrimaryViewName);
    }
    return PersistentName(base, context.scope, label);
}

struct PersistentTarget::Impl {
    explicit Impl(std::string_view baseName)
        : base(baseName)
    {
    }

    std::string base;
    std::array<std::string, rg::kViewScopeCount> names; // Set on first use. Stable: the cache copies, but tokens key on them.
    std::array<rg::PersistentTarget, rg::kViewScopeCount> targets;
};

PersistentTarget::PersistentTarget(std::string_view baseName)
    : m_impl(std::make_unique<Impl>(baseName))
{
}

PersistentTarget::~PersistentTarget() = default;

Texture PersistentTarget::Acquire(Frame& frame, Extent2D extent, PixelFormat format)
{
    detail::FrameContext* ctx = frame.Context();
    if (ctx == nullptr || ctx->builder == nullptr) {
        frame.ReportOnce("PersistentTarget::Acquire needs a frame that belongs to a render callback");
        return Texture{};
    }
    const std::size_t index = rg::ViewScopeIndex(ctx->scope);
    if (m_impl->names[index].empty()) {
        m_impl->names[index] = PersistentName(m_impl->base, *ctx);
    }
    const rg::TextureHandle handle =
        m_impl->targets[index].Acquire(*ctx->builder, m_impl->names[index].c_str(), extent, format);
    return handle.IsValid() ? frame.Stamp(handle) : Texture{};
}

} // namespace gte::fx
