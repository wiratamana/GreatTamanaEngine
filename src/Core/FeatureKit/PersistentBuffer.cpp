#include "PersistentBuffer.h"

#include "FrameContext.h"
#include "PersistentBufferPolicy.h"
#include "PersistentTarget.h"
#include "../Logging.h"
#include "../../Renderer/GpuTiming.h"
#include "../../Renderer/OffscreenRingGuard.h"
#include "../../Renderer/Renderer.h"
#include "../../Renderer/RenderGraph/BuildRetryGate.h"
#include "../../Renderer/RenderGraph/GpuBuffer.h"
#include "../../Renderer/RenderGraph/ViewScopeUtil.h"

#include <algorithm>
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
    // One ring copy of a view's buffer. Capacity and generation are per copy: a copy grows on its own turn.
    struct Copy {
        rg::GpuBuffer buffer;
        std::uint64_t capacity = 0;
        std::uint64_t generation = 0; // Bumped by every create, grow and release.
        RingCopyUse use;              // The newest offscreen frame that read this copy.
    };

    struct Slot {
        std::string name; // Set on first use. The graph import and the buffer keep this pointer: it must not move.
        std::array<std::string, kGpuFramesInFlight> copyNames; // Copy 0 uses `name`; same rule.
        std::array<Copy, kGpuFramesInFlight> copies;
        rg::BuildRetryGate gate;
        std::uint64_t importedSerial = 0; // 0 = not imported yet (a real serial is never 0).
        const rg::RenderGraphBuilder* importedBuilder = nullptr; // The builder that owns `imported`.
        std::uint32_t importedCopy = 0;
        rg::BufferHandle imported;
    };

    Impl(std::string_view baseName, BufferUse bufferUse, std::uint32_t copyCount)
        : base(baseName)
        , use(bufferUse)
        , copies(copyCount)
    {
    }

    const char* CopyName(Slot& slot, std::uint32_t copy)
    {
        if (copy == 0) {
            return slot.name.c_str();
        }
        std::string& text = slot.copyNames[copy];
        if (text.empty()) {
            text = slot.name + ".C" + std::to_string(copy);
        }
        return text.c_str();
    }

    // Hands a copy's buffer to the renderer, which frees it once no frame in flight can read it.
    void RetireCopy(Copy& copy)
    {
        if (renderer != nullptr && copy.capacity != 0) {
            renderer->Retire(std::move(copy.buffer));
        }
    }

    // The new buffer is created before the old one is retired, so their handles can never be equal.
    bool Allocate(Slot& slot, std::uint32_t copyIndex, const Frame& frame, rg::RenderGraphBuilder& builder,
        std::uint64_t capacity)
    {
        if (!slot.gate.ShouldAttempt(frame.FrameIndex())) {
            return false;
        }
        Copy& copy = slot.copies[copyIndex];
        const char* name = CopyName(slot, copyIndex);
        try {
            rg::GpuBuffer next(builder.OwningRenderer().Device(), static_cast<std::size_t>(capacity), name, use);
            renderer = &builder.OwningRenderer();
            RetireCopy(copy);
            copy.buffer = std::move(next);
            copy.capacity = capacity;
            ++copy.generation;
            copy.use.Reset(); // The new buffer was read by no frame.
        } catch (const std::exception& e) {
            if (slot.gate.OnFailure(frame.FrameIndex()) == rg::BuildRetryGate::FailureKind::First) {
                frame.ReportOnce("PersistentBuffer '" + std::string(name) + "' could not allocate: " + e.what());
            }
            return false;
        }
        if (slot.gate.OnSuccess()) {
            GTE_LOG_INFO(kLogCategory, "PersistentBuffer '" + std::string(name) + "' allocated after an earlier failure.");
        }
        return true;
    }

    std::string base;
    Renderer* renderer = nullptr; // Set by the first allocation; retired buffers go there.
    BufferUse use;
    std::uint32_t copies;
    std::array<Slot, rg::kViewScopeCount> slots;
};

std::uint32_t PersistentBuffer::RingCopiesFor(BufferUse use, std::uint32_t requested) noexcept
{
    if (!rg::HasUse(use, BufferUse::HostWrite)) {
        return 1;
    }
    return std::clamp(requested, 1u, kGpuFramesInFlight);
}

PersistentBuffer::PersistentBuffer(std::string_view baseName, BufferUse use, std::uint32_t copies)
    : m_impl(std::make_unique<Impl>(baseName, use, RingCopiesFor(use, copies)))
{
}

PersistentBuffer::~PersistentBuffer()
{
    // A frame in flight may still read a copy: retire them rather than draining the GPU.
    for (Impl::Slot& slot : m_impl->slots) {
        for (Impl::Copy& copy : slot.copies) {
            m_impl->RetireCopy(copy);
        }
    }
}

std::uint32_t PersistentBuffer::Copies() const noexcept
{
    return m_impl->copies;
}

GpuBufferRef PersistentBuffer::Acquire(Frame& frame, std::uint64_t byteSize)
{
    detail::FrameContext* context = frame.Context();
    if (context == nullptr || context->builder == nullptr || context->builder->RendererOrNull() == nullptr) {
        frame.ReportOnce("PersistentBuffer::Acquire needs a frame that belongs to a render callback");
        return GpuBufferRef{};
    }
    rg::RenderGraphBuilder& builder = *context->builder;
    Impl::Slot& slot = m_impl->slots[rg::ViewScopeIndex(context->scope)];
    if (slot.name.empty()) {
        slot.name = PersistentName(m_impl->base, *context);
    }

    // A second request in the same frame and graph returns the same import; it can never grow the buffer.
    if (slot.importedSerial == frame.Serial() && slot.importedBuilder == &builder) {
        if (byteSize == 0 || byteSize > slot.copies[slot.importedCopy].capacity) {
            frame.ReportOnce("PersistentBuffer '" + slot.name
                + "': a larger size was requested after the buffer was used this frame");
            return GpuBufferRef{};
        }
        return frame.StampBuffer(slot.imported, byteSize, m_impl->use);
    }

    const OffscreenFrameTicket ticket = builder.FrameTicket();
    const std::uint32_t copyIndex = RingCopyOf(ticket, m_impl->copies);
    Impl::Copy& copy = slot.copies[copyIndex];

    switch (DecidePersistentBufferAcquire(copy.capacity, byteSize, builder.IsPersistentRegimePipelined())) {
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
        // The replaced copy is retired: a frame in flight may still read it.
        if (!m_impl->Allocate(slot, copyIndex, frame, builder, PersistentBufferCapacityFor(byteSize))) {
            return GpuBufferRef{};
        }
        break;
    }
    // A ring copy is written by one frame at a time; a copy only the GPU reads may serve frames in flight.
    if (m_impl->copies > 1) {
        if (!copy.use.Claim(ticket)) {
            frame.ReportOnce("PersistentBuffer '" + slot.name + "': the ring copy of this frame slot is still in use");
            return GpuBufferRef{};
        }
    } else {
        copy.use.Note(ticket);
    }

    slot.imported = builder.Import(copy.buffer, m_impl->CopyName(slot, copyIndex));
    builder.KeepBufferOutput(slot.imported);
    slot.importedSerial = frame.Serial();
    slot.importedBuilder = &builder;
    slot.importedCopy = copyIndex;
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
    Impl::Copy& copy = slot.copies[slot.importedCopy];
    if (!copy.use.Claim(context->builder->FrameTicket())) {
        frame.ReportOnce("PersistentBuffer '" + slot.name + "': Upload refused, a frame in flight still reads this copy");
        return false;
    }
    try {
        return copy.buffer.Upload(data, static_cast<std::size_t>(size), static_cast<std::size_t>(offset));
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
    const bool anyAllocated = std::any_of(slot.copies.begin(), slot.copies.end(),
        [](const Impl::Copy& copy) { return copy.capacity != 0; });
    if (!anyAllocated) {
        return true;
    }
    if (context->builder->IsPersistentRegimePipelined()
        || (slot.importedSerial == frame.Serial() && slot.importedBuilder == context->builder)) {
        return false;
    }
    // A frame in flight may still read a copy, so each one is retired instead of freed.
    for (Impl::Copy& copy : slot.copies) {
        if (copy.capacity == 0) {
            continue;
        }
        m_impl->RetireCopy(copy);
        copy.capacity = 0;
        ++copy.generation;
        copy.use.Reset();
    }
    slot.importedSerial = 0;
    slot.importedBuilder = nullptr;
    slot.importedCopy = 0;
    slot.imported = rg::BufferHandle{};
    return true;
}

std::uint64_t PersistentBuffer::Capacity(Frame& frame) const
{
    detail::FrameContext* context = frame.Context();
    if (context == nullptr) {
        return 0;
    }
    const Impl::Slot& slot = m_impl->slots[rg::ViewScopeIndex(context->scope)];
    return slot.copies[RingCopyOf(frame.OffscreenTicket(), m_impl->copies)].capacity;
}

std::uint64_t PersistentBuffer::Generation(Frame& frame) const
{
    detail::FrameContext* context = frame.Context();
    if (context == nullptr) {
        return 0;
    }
    const Impl::Slot& slot = m_impl->slots[rg::ViewScopeIndex(context->scope)];
    return slot.copies[RingCopyOf(frame.OffscreenTicket(), m_impl->copies)].generation;
}

} // namespace gte::fx
