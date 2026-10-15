#pragma once

#include "GpuBuffer.h"
#include "../../Core/GraphicsHandles.h"

#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>

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

// CPU-written storage buffer, created lazily; its size is fixed by the first upload.
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

    // Invalid until the first successful Upload().
    BufferObjectHandle Handle() const noexcept;

private:
    void LogRefusalOnce(const std::string& message);

    std::unique_ptr<GpuBuffer> m_buffer;
    std::size_t m_size = 0;
    bool m_refusalLogged = false;
};

template <typename T>
class GpuParams {
    static_assert(std::is_trivially_copyable_v<T>, "GpuParams<T> is memcpy'd to the GPU");

public:
    bool Upload(RenderGraphBuilder& builder, const T& value, const char* debugName)
    {
        return m_buffer.Upload(builder, &value, sizeof(T), debugName);
    }

    BufferObjectHandle Handle() const noexcept { return m_buffer.Handle(); }

private:
    GpuParamsBuffer m_buffer;
};

} // namespace gte::rg
