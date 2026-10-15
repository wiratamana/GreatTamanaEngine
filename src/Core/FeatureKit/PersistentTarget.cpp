#include "PersistentTarget.h"

#include "FrameContext.h"
#include "../../Renderer/RenderGraph/PersistentTarget.h"
#include "../../Renderer/RenderGraph/ViewScopeUtil.h"

#include <array>

namespace gte::fx {

std::string PersistentName(std::string_view base, rg::ViewScope scope)
{
    std::string name(base);
    switch (scope) {
    case rg::ViewScope::GameView:
        name += ".GameView";
        break;
    case rg::ViewScope::SceneView:
        name += ".SceneView";
        break;
    case rg::ViewScope::Shared:
        break;
    }
    return name;
}

struct PersistentTarget::Impl {
    explicit Impl(std::string_view base)
    {
        for (std::size_t i = 0; i < rg::kViewScopeCount; ++i) {
            names[i] = PersistentName(base, static_cast<rg::ViewScope>(i));
        }
    }

    std::array<std::string, rg::kViewScopeCount> names; // Stable: the cache copies, but tokens key on them.
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
    const rg::TextureHandle handle =
        m_impl->targets[index].Acquire(*ctx->builder, m_impl->names[index].c_str(), extent, format);
    return handle.IsValid() ? frame.Stamp(handle) : Texture{};
}

} // namespace gte::fx
