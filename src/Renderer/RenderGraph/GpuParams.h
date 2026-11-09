#pragma once

#include "GpuBuffer.h"
#include "../GpuTiming.h"
#include "../OffscreenFrameTicket.h"
#include "../OffscreenRingGuard.h"
#include "../../Core/GraphicsHandles.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>

namespace gte {
class Renderer;
} // namespace gte

namespace gte::rg {

class RenderGraphBuilder;

enum class GpuParamsUploadDecision {
    Create,            // First upload: allocate the buffer.
    Overwrite,         // Same size as the existing buffer.
    RefuseEmpty,
    RefuseSizeMismatch,
    RefusePipelined,   // Frames in flight would read bytes the CPU is overwriting.
};

// Pure policy. existingSize is 0 until the first successful upload.
GpuParamsUploadDecision DecideGpuParamsUpload(
    std::size_t existingSize, std::size_t newSize, bool pipelinedRegime) noexcept;

// Which ring copies of a params buffer hold this frame's bytes. Pure, so it runs without a device.
template <std::uint32_t Copies>
class BasicGpuParamsSlotLedger {
    static_assert(Copies >= 1, "a ring has at least one copy");

public:
    // True when the copy of the ticket's slot is not read by a frame still in flight.
    bool Writable(const OffscreenFrameTicket& ticket) const noexcept { return m_use[RingCopyOf(ticket, Copies)].Writable(ticket); }

    // Records an upload; false when the copy may still be read by a frame in flight.
    bool NoteUpload(const OffscreenFrameTicket& ticket, std::uint64_t frameIndex) noexcept
    {
        const std::uint32_t copy = RingCopyOf(ticket, Copies);
        if (!m_use[copy].Claim(ticket)) {
            return false;
        }
        m_uploadedFrame[copy] = frameIndex;
        return true;
    }

    bool UploadedThisFrame(const OffscreenFrameTicket& ticket, std::uint64_t frameIndex) const noexcept
    {
        const std::optional<std::uint64_t>& uploaded = m_uploadedFrame[RingCopyOf(ticket, Copies)];
        return uploaded.has_value() && *uploaded == frameIndex;
    }

private:
    std::array<RingCopyUse, Copies> m_use{};
    std::array<std::optional<std::uint64_t>, Copies> m_uploadedFrame{};
};

using GpuParamsSlotLedger = BasicGpuParamsSlotLedger<kGpuFramesInFlight>;

// CPU-written storage buffer, created lazily; its size is fixed by the first upload.
// One copy per frame slot: a frame writes and binds the copy of its own slot only.
class GpuParamsBuffer {
public:
    GpuParamsBuffer() noexcept;
    ~GpuParamsBuffer();
    GpuParamsBuffer(GpuParamsBuffer&&) noexcept;
    GpuParamsBuffer& operator=(GpuParamsBuffer&&) noexcept;
    GpuParamsBuffer(const GpuParamsBuffer&) = delete;
    GpuParamsBuffer& operator=(const GpuParamsBuffer&) = delete;

    // debugName must have static storage duration (the buffer keeps the pointer).
    // Returns false (and logs once) when refused.
    bool Upload(RenderGraphBuilder& builder, const void* data, std::size_t size, const char* debugName);

    // The copy this frame uploaded; Invalid when this frame has not uploaded it yet.
    BufferObjectHandle Handle(const RenderGraphBuilder& builder) const noexcept;

private:
    void LogRefusalOnce(const std::string& message);
    void RetireCopies() noexcept;

    std::array<std::unique_ptr<GpuBuffer>, kGpuFramesInFlight> m_buffers;
    GpuParamsSlotLedger m_ledger;
    std::size_t m_size = 0;
    bool m_refusalLogged = false;
    Renderer* m_renderer = nullptr; // Set by the first upload; retired copies go there.
};

template <typename T>
class GpuParams {
    static_assert(std::is_trivially_copyable_v<T>, "GpuParams<T> is memcpy'd to the GPU");

public:
    bool Upload(RenderGraphBuilder& builder, const T& value, const char* debugName)
    {
        return m_buffer.Upload(builder, &value, sizeof(T), debugName);
    }

    BufferObjectHandle Handle(const RenderGraphBuilder& builder) const noexcept { return m_buffer.Handle(builder); }

private:
    GpuParamsBuffer m_buffer;
};

} // namespace gte::rg
