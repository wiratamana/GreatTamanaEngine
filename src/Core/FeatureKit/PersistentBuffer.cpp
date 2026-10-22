#include "PersistentBuffer.h"

#include "FrameContext.h"
#include "PersistentBufferPolicy.h"
#include "PersistentTarget.h"
#include "../Logging.h"
#include "../../Renderer/Renderer.h"
#include "../../Renderer/RenderGraph/BuildRetryGate.h"
#include "../../Renderer/RenderGraph/GpuBuffer.h"
#include "../../Renderer/RenderGraph/ViewScopeUtil.h"

#include <array>
#include <cstddef>
#include <exception>
#include <string>
#include <utility>

namespace gte::fx {

namespace {
constexpr const char* kLogCategory = "FeatureKit";
} // namespace

struct PersistentBuffer::Impl {
    struct Slot {
        std::string name; // The graph import and the buffer keep this pointer: it must not move.
        rg::GpuBuffer buffer;
        std::uint64_t capacity = 0;
        rg::BuildRetryGate gate;
        std::uint64_t importedSerial = 0; // 0 = not imported yet (a real serial is never 0).
        const rg::RenderGraphBuilder* importedBuilder = nullptr; // The builder that owns `imported`.
        rg::BufferHandle imported;
    };

    Impl(std::string_view base, BufferUse bufferUse)
        : use(bufferUse)
    {
        for (std::size_t i = 0; i < rg::kViewScopeCount; ++i) {
            slots[i].name = PersistentName(base, static_cast<rg::ViewScope>(i));
        }
    }

    // The new buffer is created before the old one is freed, so their handles can never be equal.
    bool Allocate(Slot& slot, const Frame& frame, rg::RenderGraphBuilder& builder, std::uint64_t capacity)
    {
        if (!slot.gate.ShouldAttempt(frame.FrameIndex())) {
            return false;
        }
        try {
            rg::GpuBuffer next(
                builder.OwningRenderer().Device(), static_cast<std::size_t>(capacity), slot.name.c_str(), use);
            slot.buffer = std::move(next);
            slot.capacity = capacity;
        } catch (const std::exception& e) {
            if (slot.gate.OnFailure(frame.FrameIndex()) == rg::BuildRetryGate::FailureKind::First) {
                frame.ReportOnce("PersistentBuffer '" + slot.name + "' could not allocate: " + e.what());
            }
            return false;
        }
        if (slot.gate.OnSuccess()) {
            GTE_LOG_INFO(kLogCategory, "PersistentBuffer '" + slot.name + "' allocated after an earlier failure.");
        }
        return true;
    }

    BufferUse use;
    std::array<Slot, rg::kViewScopeCount> slots;
};

PersistentBuffer::PersistentBuffer(std::string_view baseName, BufferUse use)
    : m_impl(std::make_unique<Impl>(baseName, use))
{
}

PersistentBuffer::~PersistentBuffer() = default;

GpuBufferRef PersistentBuffer::Acquire(Frame& frame, std::uint64_t byteSize)
{
    detail::FrameContext* context = frame.Context();
    if (context == nullptr || context->builder == nullptr || context->builder->RendererOrNull() == nullptr) {
        frame.ReportOnce("PersistentBuffer::Acquire needs a frame that belongs to a render callback");
        return GpuBufferRef{};
    }
    rg::RenderGraphBuilder& builder = *context->builder;
    Impl::Slot& slot = m_impl->slots[rg::ViewScopeIndex(context->scope)];

    // A second request in the same frame and graph returns the same import; it can never grow the buffer.
    if (slot.importedSerial == frame.Serial() && slot.importedBuilder == &builder) {
        if (byteSize == 0 || byteSize > slot.capacity) {
            frame.ReportOnce("PersistentBuffer '" + slot.name
                + "': a larger size was requested after the buffer was used this frame");
            return GpuBufferRef{};
        }
        return frame.StampBuffer(slot.imported, byteSize, m_impl->use);
    }

    switch (DecidePersistentBufferAcquire(slot.capacity, byteSize, builder.IsPersistentRegimePipelined())) {
    case PersistentBufferDecision::RefuseEmpty:
        frame.ReportOnce("PersistentBuffer '" + slot.name + "': the requested size is zero");
        return GpuBufferRef{};
    case PersistentBufferDecision::RefuseTooLarge:
        frame.ReportOnce("PersistentBuffer '" + slot.name + "': the requested size is over the limit");
        return GpuBufferRef{};
    case PersistentBufferDecision::RefuseGrowWhilePipelined:
        frame.ReportOnce("PersistentBuffer '" + slot.name + "': growing is refused while frames overlap");
        return GpuBufferRef{};
    case PersistentBufferDecision::Reuse:
        break;
    case PersistentBufferDecision::Create:
    case PersistentBufferDecision::Grow:
        if (!m_impl->Allocate(slot, frame, builder, PersistentBufferCapacityFor(byteSize))) {
            return GpuBufferRef{};
        }
        break;
    }

    slot.imported = builder.Import(slot.buffer, slot.name.c_str());
    builder.KeepBufferOutput(slot.imported);
    slot.importedSerial = frame.Serial();
    slot.importedBuilder = &builder;
    return frame.StampBuffer(slot.imported, byteSize, m_impl->use);
}

bool PersistentBuffer::Upload(Frame& frame, const void* data, std::uint64_t size, std::uint64_t offset)
{
    detail::FrameContext* context = frame.Context();
    if (context == nullptr || context->builder == nullptr) {
        frame.ReportOnce("PersistentBuffer::Upload needs a frame that belongs to a render callback");
        return false;
    }
    Impl::Slot& slot = m_impl->slots[rg::ViewScopeIndex(context->scope)];
    if (slot.importedSerial != frame.Serial() || slot.importedBuilder != context->builder) {
        frame.ReportOnce("PersistentBuffer '" + slot.name + "': Upload needs an Acquire in the same frame");
        return false;
    }
    if (context->builder->IsPersistentRegimePipelined()) {
        frame.ReportOnce("PersistentBuffer '" + slot.name + "': Upload is refused while frames overlap");
        return false;
    }
    try {
        return slot.buffer.Upload(data, static_cast<std::size_t>(size), static_cast<std::size_t>(offset));
    } catch (const std::exception& e) {
        frame.ReportOnce("PersistentBuffer '" + slot.name + "': Upload failed: " + e.what());
        return false;
    }
}

bool PersistentBuffer::Release(Frame& frame)
{
    detail::FrameContext* context = frame.Context();
    if (context == nullptr || context->builder == nullptr) {
        return false;
    }
    Impl::Slot& slot = m_impl->slots[rg::ViewScopeIndex(context->scope)];
    if (slot.capacity == 0) {
        return true;
    }
    if (context->builder->IsPersistentRegimePipelined()
        || (slot.importedSerial == frame.Serial() && slot.importedBuilder == context->builder)) {
        return false;
    }
    slot.buffer = rg::GpuBuffer{};
    slot.capacity = 0;
    slot.importedSerial = 0;
    slot.importedBuilder = nullptr;
    slot.imported = rg::BufferHandle{};
    return true;
}

std::uint64_t PersistentBuffer::Capacity(Frame& frame) const
{
    detail::FrameContext* context = frame.Context();
    if (context == nullptr) {
        return 0;
    }
    return m_impl->slots[rg::ViewScopeIndex(context->scope)].capacity;
}

} // namespace gte::fx
