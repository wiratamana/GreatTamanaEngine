#include "PersistentVolume.h"

#include "FrameContext.h"
#include "PersistentTarget.h"
#include "../Core.h"
#include "../Logging.h"
#include "../../Renderer/GraphicsHandleRegistry.h"
#include "../../Renderer/Renderer.h"
#include "../../Renderer/VolumeTexture.h"
#include "../../Renderer/RenderGraph/BuildRetryGate.h"
#include "../../Renderer/RenderGraph/ViewScopeUtil.h"

#include <array>
#include <exception>
#include <optional>
#include <string>

namespace gte::fx {

namespace {
constexpr const char* kLogCategory = "FeatureKit";
} // namespace

struct PersistentVolume::Impl {
    struct Slot {
        std::string name; // Set on first use. The import keeps this pointer for the frame, so it must not move.
        std::optional<VolumeTexture> volume;
        CachedSamplerHandle sampler;
        rg::BuildRetryGate gate;
    };

    Impl(std::string_view baseName, VolumeSize volumeSize, PixelFormat volumeFormat)
        : base(baseName)
        , size(volumeSize)
        , format(volumeFormat)
    {
    }

    ~Impl()
    {
        for (Slot& slot : slots) {
            if (slot.sampler.handle != SamplerHandle::Invalid) {
                UnregisterSampler(slot.sampler.handle);
            }
        }
    }

    bool Create(Slot& slot, detail::FrameContext& context)
    {
        if (!slot.gate.ShouldAttempt(context.builder->FrameIndex())) {
            return false;
        }
        try {
            slot.volume.emplace(context.core->GetRenderer().CreateVolumeTexture(static_cast<int>(size.width),
                static_cast<int>(size.height), static_cast<int>(size.depth), format, slot.name.c_str()));
        } catch (const std::exception& e) {
            if (slot.gate.OnFailure(context.builder->FrameIndex()) == rg::BuildRetryGate::FailureKind::First) {
                GTE_LOG_ERROR(kLogCategory, "Volume '" + slot.name + "' could not be created: " + e.what());
            }
            return false;
        }
        if (slot.gate.OnSuccess()) {
            GTE_LOG_INFO(kLogCategory, "Volume '" + slot.name + "' created after an earlier failure.");
        }
        return true;
    }

    std::string base;
    VolumeSize size;
    PixelFormat format;
    std::array<Slot, rg::kViewScopeCount> slots;
};

PersistentVolume::PersistentVolume(std::string_view baseName, VolumeSize size, PixelFormat format)
    : m_impl(std::make_unique<Impl>(baseName, size, format))
{
}

PersistentVolume::~PersistentVolume() = default;

Texture PersistentVolume::Acquire(Frame& frame)
{
    detail::FrameContext* context = frame.Context();
    if (context == nullptr || context->builder == nullptr || context->core == nullptr) {
        frame.ReportOnce("PersistentVolume::Acquire needs a frame that belongs to a render callback");
        return Texture{};
    }
    if (!IsValidVolumeSize(m_impl->size)) {
        frame.ReportOnce("PersistentVolume::Acquire: the volume size has a zero axis");
        return Texture{};
    }

    Impl::Slot& slot = m_impl->slots[rg::ViewScopeIndex(context->scope)];
    if (slot.name.empty()) {
        slot.name = PersistentName(m_impl->base, *context);
    }
    if (!slot.volume.has_value() && !m_impl->Create(slot, *context)) {
        return Texture{};
    }

    const SamplerHandle sampler = slot.sampler.Resolve(slot.volume->Sampler());
    const rg::TextureHandle handle = context->builder->ImportVolumeTexture(
        slot.name.c_str(), slot.volume->Target(), ImageLayout::Undefined, sampler);
    context->builder->KeepTextureOutput(handle);
    return frame.Stamp(handle);
}

} // namespace gte::fx
