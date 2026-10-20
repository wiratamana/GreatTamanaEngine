#pragma once

#include "Frame.h"
#include "GpuBufferRef.h"

#include <cstdint>
#include <memory>
#include <string_view>

namespace gte::fx {

// A GPU buffer that outlives the frame, one per view, created and grown by the kit.
// Contents are undefined after a create or a grow. The name is the graph name ("<base>.GameView").
// Destroy it only while the GPU is idle.
class PersistentBuffer {
public:
    PersistentBuffer(std::string_view baseName, BufferUse use);
    ~PersistentBuffer();

    PersistentBuffer(const PersistentBuffer&) = delete;
    PersistentBuffer& operator=(const PersistentBuffer&) = delete;
    PersistentBuffer(PersistentBuffer&&) = delete;
    PersistentBuffer& operator=(PersistentBuffer&&) = delete;

    // The buffer for this frame's view, at least `byteSize` bytes, imported once per frame and kept
    // alive. Invalid when refused (logged once). Ask Frame::PassEnabled() before calling it.
    GpuBufferRef Acquire(Frame& frame, std::uint64_t byteSize);

    // Copies into a HostWrite buffer already acquired in this callback. False when refused.
    bool Upload(Frame& frame, const void* data, std::uint64_t size, std::uint64_t offset = 0);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace gte::fx
