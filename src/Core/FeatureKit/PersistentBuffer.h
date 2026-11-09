#pragma once

#include "Frame.h"
#include "GpuBufferRef.h"

#include <cstdint>
#include <memory>
#include <string_view>

namespace gte::fx {

// A GPU buffer that outlives the frame, one per view, created and grown by the kit; graph name "<base>.<view label>".
// Contents are undefined after a create or a grow. A replaced or released buffer is retired, never freed under a frame.
// With copies > 1 a HostWrite buffer is a ring: a frame reads and writes only the copy of its frame slot.
class PersistentBuffer {
public:
    PersistentBuffer(std::string_view baseName, BufferUse use, std::uint32_t copies = 1);
    ~PersistentBuffer();

    PersistentBuffer(const PersistentBuffer&) = delete;
    PersistentBuffer& operator=(const PersistentBuffer&) = delete;
    PersistentBuffer(PersistentBuffer&&) = delete;
    PersistentBuffer& operator=(PersistentBuffer&&) = delete;

    // Copies a ring of `requested` slots really has: 1 unless the buffer is HostWrite, at most kGpuFramesInFlight.
    static std::uint32_t RingCopiesFor(BufferUse use, std::uint32_t requested) noexcept;

    std::uint32_t Copies() const noexcept;

    // The buffer for this frame's view and frame slot, at least `byteSize` bytes, imported once per frame and kept
    // alive. Invalid when refused (logged once). Ask Frame::PassEnabled() before calling it.
    GpuBufferRef Acquire(Frame& frame, std::uint64_t byteSize);

    // Copies into a HostWrite buffer already acquired in this callback. False when refused.
    bool Upload(Frame& frame, const void* data, std::uint64_t size, std::uint64_t offset = 0);

    // Retires every copy of this frame's view; a frame in flight keeps reading it until it is done. False (kept)
    // while frames overlap or after it was acquired this frame. True when retired or when there was nothing to free.
    bool Release(Frame& frame);

    // Allocated bytes of this frame's view and slot copy; 0 when none.
    std::uint64_t Capacity(Frame& frame) const;

    // Counts every create, grow and release of this frame's slot copy. A changed value means its contents are undefined.
    std::uint64_t Generation(Frame& frame) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace gte::fx
