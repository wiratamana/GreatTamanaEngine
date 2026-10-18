#include "OffscreenTarget.h"

#include "FrameContext.h"
#include "PersistentTarget.h"
#include "../Core.h"
#include "../Logging.h"
#include "../../Renderer/RenderTexture.h"
#include "../../Renderer/RenderGraph/BuildRetryGate.h"
#include "../../Renderer/RenderGraph/ViewScopeUtil.h"

#include <array>
#include <exception>
#include <string>

namespace gte::fx {

namespace {

constexpr const char* kLogCategory = "FeatureKit";

} // namespace

struct OffscreenTarget::Impl {
    struct Slot {
        std::string name; // Imports keep this pointer for the frame, so it must not move.
        rg::RenderViewId view;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool depthOnly = false;
        bool created = false;
        rg::BuildRetryGate gate;
        std::uint32_t failedWidth = 0; // Size of the last failed create or resize.
        std::uint32_t failedHeight = 0;
    };

    explicit Impl(std::string_view base)
    {
        for (std::size_t i = 0; i < rg::kViewScopeCount; ++i) {
            slots[i].name = PersistentName(base, static_cast<rg::ViewScope>(i));
        }
    }

    Texture Acquire(Frame& frame, std::uint32_t width, std::uint32_t height, bool depthOnly, PixelFormat format);
    void Trim(Frame& frame);

    // Gated create or resize. A request for another size than the failed one retries at once.
    static bool ShouldAttempt(Slot& slot, const detail::FrameContext& context, std::uint32_t width, std::uint32_t height);
    static bool Create(Slot& slot, detail::FrameContext& context, std::uint32_t width, std::uint32_t height,
        bool depthOnly, PixelFormat format);
    static bool Resize(Slot& slot, detail::FrameContext& context, std::uint32_t width, std::uint32_t height);
    static void NoteSuccess(Slot& slot);
    static void NoteFailure(Slot& slot, const detail::FrameContext& context, const char* what, std::uint32_t width,
        std::uint32_t height, const char* reason);

    std::array<Slot, rg::kViewScopeCount> slots;
};

bool OffscreenTarget::Impl::ShouldAttempt(
    Slot& slot, const detail::FrameContext& context, std::uint32_t width, std::uint32_t height)
{
    if (slot.failedWidth != width || slot.failedHeight != height) {
        slot.gate = rg::BuildRetryGate{};
    }
    return slot.gate.ShouldAttempt(context.builder->FrameIndex());
}

void OffscreenTarget::Impl::NoteSuccess(Slot& slot)
{
    slot.failedWidth = 0;
    slot.failedHeight = 0;
    if (slot.gate.OnSuccess()) {
        GTE_LOG_INFO(kLogCategory, "Offscreen target '" + slot.name + "' recovered after an earlier failure.");
    }
}

void OffscreenTarget::Impl::NoteFailure(Slot& slot, const detail::FrameContext& context, const char* what,
    std::uint32_t width, std::uint32_t height, const char* reason)
{
    slot.failedWidth = width;
    slot.failedHeight = height;
    if (slot.gate.OnFailure(context.builder->FrameIndex()) == rg::BuildRetryGate::FailureKind::First) {
        GTE_LOG_ERROR(kLogCategory, "Offscreen target '" + slot.name + "' " + what + " at " + std::to_string(width) + "x"
            + std::to_string(height) + ": " + reason);
    }
}

bool OffscreenTarget::Impl::Create(Slot& slot, detail::FrameContext& context, std::uint32_t width,
    std::uint32_t height, bool depthOnly, PixelFormat format)
{
    try {
        slot.view = context.core->CreateRenderView(
            slot.name.c_str(), width, height, depthOnly, /*allowDepthSampledAccess=*/true, format);
    } catch (const std::exception& e) {
        NoteFailure(slot, context, "could not be created", width, height, e.what());
        return false;
    }
    if (context.core->FindRenderViewTarget(slot.view) == nullptr) {
        NoteFailure(slot, context, "could not be created", width, height, "the engine refused the view");
        return false;
    }
    slot.width = width;
    slot.height = height;
    slot.depthOnly = depthOnly;
    slot.created = true;
    NoteSuccess(slot);
    return true;
}

bool OffscreenTarget::Impl::Resize(Slot& slot, detail::FrameContext& context, std::uint32_t width, std::uint32_t height)
{
    if (!context.core->ResizeRenderView(slot.view, width, height)) {
        // The engine dropped the target; the next attempt creates it again.
        slot.created = false;
        NoteFailure(slot, context, "could not be resized", width, height, "the engine could not reallocate it");
        return false;
    }
    slot.width = width;
    slot.height = height;
    NoteSuccess(slot);
    return true;
}

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
        if (!ShouldAttempt(slot, *context, width, height) || !Create(slot, *context, width, height, depthOnly, format)) {
            return Texture{};
        }
    } else if (slot.depthOnly != depthOnly) {
        frame.ReportOnce("OffscreenTarget '" + slot.name + "' was made with another kind (depth-only or color); the new request is ignored");
    } else if (slot.width != width || slot.height != height) {
        if (context->builder->IsPersistentRegimePipelined()) {
            frame.ReportOnce("OffscreenTarget '" + slot.name + "' cannot be resized while the view runs pipelined; keeping "
                + std::to_string(slot.width) + "x" + std::to_string(slot.height));
        } else if (!ShouldAttempt(slot, *context, width, height) || !Resize(slot, *context, width, height)) {
            return Texture{};
        }
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

void OffscreenTarget::Impl::Trim(Frame& frame)
{
    detail::FrameContext* context = frame.Context();
    if (context == nullptr || context->builder == nullptr || context->core == nullptr) {
        frame.ReportOnce("OffscreenTarget::Trim needs a frame that belongs to a render callback");
        return;
    }
    Slot& slot = slots[rg::ViewScopeIndex(context->scope)];
    if (!slot.created || (slot.width == kTrimmedExtent && slot.height == kTrimmedExtent)
        || context->builder->IsPersistentRegimePipelined()) {
        return;
    }
    if (ShouldAttempt(slot, *context, kTrimmedExtent, kTrimmedExtent)) {
        Resize(slot, *context, kTrimmedExtent, kTrimmedExtent);
    }
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

void OffscreenTarget::Trim(Frame& frame)
{
    m_impl->Trim(frame);
}

} // namespace gte::fx
