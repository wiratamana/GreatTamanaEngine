#include "OffscreenTarget.h"

#include "FrameContext.h"
#include "PersistentTarget.h"
#include "../Core.h"
#include "../../Renderer/RenderTexture.h"
#include "../../Renderer/RenderGraph/ViewScopeUtil.h"

#include <array>
#include <string>

namespace gte::fx {

namespace {

// Pure rule: whether a request still matches the view that was made first.
bool SameShape(std::uint32_t width, std::uint32_t height, bool depthOnly, std::uint32_t firstWidth,
    std::uint32_t firstHeight, bool firstDepthOnly) noexcept
{
    return width == firstWidth && height == firstHeight && depthOnly == firstDepthOnly;
}

} // namespace

struct OffscreenTarget::Impl {
    struct Slot {
        std::string name; // Imports keep this pointer for the frame, so it must not move.
        rg::RenderViewId view;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool depthOnly = false;
        bool created = false;
    };

    explicit Impl(std::string_view base)
    {
        for (std::size_t i = 0; i < rg::kViewScopeCount; ++i) {
            slots[i].name = PersistentName(base, static_cast<rg::ViewScope>(i));
        }
    }

    Texture Acquire(Frame& frame, std::uint32_t width, std::uint32_t height, bool depthOnly, PixelFormat format);

    std::array<Slot, rg::kViewScopeCount> slots;
};

Texture OffscreenTarget::Impl::Acquire(
    Frame& frame, std::uint32_t width, std::uint32_t height, bool depthOnly, PixelFormat format)
{
    detail::FrameContext* context = frame.Context();
    if (context == nullptr || context->builder == nullptr || context->core == nullptr) {
        frame.ReportOnce("OffscreenTarget::Acquire needs a frame that belongs to a render callback");
        return Texture{};
    }
    if (width == 0 || height == 0) {
        frame.ReportOnce("OffscreenTarget::Acquire was given a zero size");
        return Texture{};
    }

    Slot& slot = slots[rg::ViewScopeIndex(context->scope)];
    if (!slot.created) {
        slot.view = context->core->CreateRenderView(
            slot.name.c_str(), width, height, depthOnly, /*allowDepthSampledAccess=*/true, format);
        if (context->core->FindRenderViewTarget(slot.view) == nullptr) {
            return Texture{}; // The engine logged the refusal; the next frame tries again.
        }
        slot.width = width;
        slot.height = height;
        slot.depthOnly = depthOnly;
        slot.created = true;
    } else if (!SameShape(width, height, depthOnly, slot.width, slot.height, slot.depthOnly)) {
        frame.ReportOnce("OffscreenTarget '" + slot.name + "' was made as " + std::to_string(slot.width) + "x"
            + std::to_string(slot.height) + " and keeps that size; the new request is ignored");
    }

    RenderTexture* target = context->core->FindRenderViewTarget(slot.view);
    if (target == nullptr) {
        return Texture{};
    }
    const rg::TextureHandle handle = context->builder->ImportTexture(slot.name.c_str(), target->Target(),
        ImageLayout::Undefined, slot.depthOnly ? SamplerHandle::Invalid : target->SamplerObjectHandle(),
        target->DepthSamplerObjectHandle());
    context->builder->KeepTextureOutput(handle);
    return frame.Stamp(handle);
}

OffscreenTarget::OffscreenTarget(std::string_view baseName)
    : m_impl(std::make_unique<Impl>(baseName))
{
}

OffscreenTarget::~OffscreenTarget() = default;

Texture OffscreenTarget::Acquire(Frame& frame, DepthOnly request)
{
    return m_impl->Acquire(frame, request.resolution, request.resolution, true, PixelFormat::Undefined);
}

Texture OffscreenTarget::Acquire(Frame& frame, ColorDepth request)
{
    return m_impl->Acquire(frame, request.width, request.height, false, request.format);
}

} // namespace gte::fx
