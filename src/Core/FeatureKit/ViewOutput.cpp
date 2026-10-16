#include "ViewOutput.h"

#include "FrameContext.h"
#include "../Core.h"
#include "../Logging.h"
#include "../../Renderer/Renderer.h"
#include "../../Renderer/RenderTexture.h"
#include "../../Renderer/RenderGraph/BuildRetryGate.h"
#include "../../Renderer/RenderGraph/ViewScopeUtil.h"

#include <algorithm>
#include <array>
#include <exception>
#include <optional>
#include <string>

namespace gte::fx {

namespace {

constexpr const char* kLogCategory = "FeatureKit";

std::string ViewOutputName(std::string_view suffix, rg::ViewScope scope)
{
    std::string name;
    switch (scope) {
    case rg::ViewScope::GameView:
        name = "GameView";
        break;
    case rg::ViewScope::SceneView:
        name = "SceneView";
        break;
    case rg::ViewScope::Shared:
        break;
    }
    name += suffix;
    return name;
}

} // namespace

struct ViewOutput::Impl {
    struct Slot {
        std::string name; // The import keeps this pointer for the frame, so it must not move.
        std::optional<RenderTexture> texture;
        rg::TextureHandle imported; // This frame's import of `texture`.
        rg::BuildRetryGate gate;
    };

    explicit Impl(std::string_view suffix)
    {
        for (std::size_t i = 0; i < rg::kViewScopeCount; ++i) {
            slots[i].name = ViewOutputName(suffix, static_cast<rg::ViewScope>(i));
        }
    }

    bool Create(Slot& slot, detail::FrameContext& context, int width, int height, PixelFormat format)
    {
        if (!slot.gate.ShouldAttempt(context.builder->FrameIndex())) {
            return false;
        }
        try {
            slot.texture.emplace(context.core->GetRenderer().CreateRenderTexture(
                width, height, format, slot.name.c_str(), /*depthDebugName=*/nullptr, /*allowStorageImageAccess=*/true));
        } catch (const std::exception& e) {
            NoteFailure(slot, context, "could not be created: ", e.what());
            return false;
        }
        if (slot.gate.OnSuccess()) {
            GTE_LOG_INFO(kLogCategory, "View output '" + slot.name + "' created after an earlier failure.");
        }
        return true;
    }

    // A failed resize leaves the texture without an image, so it is dropped and rebuilt through Create() on the cooldown.
    bool Resize(Slot& slot, detail::FrameContext& context, int width, int height)
    {
        try {
            context.core->GetRenderer().ResizeRenderTexture(*slot.texture, width, height);
        } catch (const std::exception& e) {
            slot.texture.reset();
            NoteFailure(slot, context, "could not be resized: ", e.what());
            return false;
        }
        return true;
    }

    static void NoteFailure(Slot& slot, detail::FrameContext& context, const char* what, const char* reason)
    {
        if (slot.gate.OnFailure(context.builder->FrameIndex()) == rg::BuildRetryGate::FailureKind::First) {
            GTE_LOG_ERROR(kLogCategory, "View output '" + slot.name + "' " + what + reason);
        }
    }

    std::array<Slot, rg::kViewScopeCount> slots;
};

ViewOutput::ViewOutput(std::string_view nameSuffix)
    : m_impl(std::make_unique<Impl>(nameSuffix))
{
}

ViewOutput::~ViewOutput() = default;

Texture ViewOutput::Acquire(Frame& frame, Extent2D extent, PixelFormat format)
{
    detail::FrameContext* context = frame.Context();
    if (context == nullptr || context->builder == nullptr || context->core == nullptr) {
        frame.ReportOnce("ViewOutput::Acquire needs a frame that belongs to a render callback");
        return Texture{};
    }
    if (context->scope == rg::ViewScope::Shared) {
        frame.ReportOnce("ViewOutput::Acquire is for a view callback; a Shared callback has no view");
        return Texture{};
    }
    if (extent.width == 0 || extent.height == 0) {
        frame.ReportOnce("ViewOutput::Acquire was given a zero size");
        return Texture{};
    }

    Impl::Slot& slot = m_impl->slots[rg::ViewScopeIndex(context->scope)];
    const int width = static_cast<int>(extent.width);
    const int height = static_cast<int>(extent.height);
    if (!slot.texture.has_value()) {
        if (!m_impl->Create(slot, *context, width, height, format)) {
            return Texture{};
        }
    } else if (slot.texture->Extent().width != extent.width || slot.texture->Extent().height != extent.height) {
        if (!m_impl->Resize(slot, *context, width, height)) {
            return Texture{};
        }
    }

    RenderTexture& texture = *slot.texture;
    slot.imported = context->builder->ImportTexture(slot.name.c_str(), texture.Target(), ImageLayout::Undefined,
        texture.SamplerObjectHandle(), texture.DepthSamplerObjectHandle(), &texture);
    context->builder->KeepTextureOutput(slot.imported);
    return frame.Stamp(slot.imported);
}

void ViewOutput::Expose(Frame& frame, Texture produced)
{
    detail::FrameContext* context = frame.Context();
    if (context == nullptr || context->core == nullptr) {
        frame.ReportOnce("ExposeAsViewOutput needs a frame that belongs to a render callback");
        return;
    }
    Impl::Slot& slot = m_impl->slots[rg::ViewScopeIndex(context->scope)];
    if (!slot.texture.has_value() || !frame.Owns(produced) || !(produced.handle == slot.imported)) {
        frame.ReportOnce("ExposeAsViewOutput: the texture is not the one this output's Acquire() returned in this callback");
        return;
    }
    context->core->ExposeViewOutput(context->scope, *slot.texture, slot.name);
}

void Frame::ExposeAsViewOutput(ViewOutput& output, Texture produced)
{
    output.Expose(*this, produced);
}

} // namespace gte::fx
