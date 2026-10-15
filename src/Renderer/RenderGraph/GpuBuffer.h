#pragma once

#include "../../Core/GraphicsHandles.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace gte {
class Buffer;
class Renderer;
} // namespace gte

namespace gte::rg {

struct GpuBufferAccess;

// Opaque handle to the device a buffer is created on. Handed out by Renderer::Device(); never looked up globally.
class DeviceRef {
public:
    DeviceRef() noexcept = default;
    bool IsValid() const noexcept { return m_renderer != nullptr; }

private:
    friend class ::gte::Renderer;
    friend struct GpuBufferAccess;

    explicit DeviceRef(Renderer* renderer) noexcept
        : m_renderer(renderer)
    {
    }

    Renderer* m_renderer = nullptr;
};

// What a buffer is used for. Device-local buffers are always a valid transfer destination.
enum class GpuBufferUse : std::uint32_t {
    None = 0,
    Storage = 1u << 0,
    Uniform = 1u << 1,
    Indirect = 1u << 2,
    Vertex = 1u << 3,
    TransferSrc = 1u << 4,
    TransferDst = 1u << 5,
    HostWrite = 1u << 6, // The CPU writes it (Upload); host-visible memory.
    HostRead = 1u << 7,  // The CPU reads it back (Download); host-visible memory.
};

constexpr GpuBufferUse operator|(GpuBufferUse a, GpuBufferUse b) noexcept
{
    return static_cast<GpuBufferUse>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

constexpr bool HasUse(GpuBufferUse mask, GpuBufferUse flag) noexcept
{
    return (static_cast<std::uint32_t>(mask) & static_cast<std::uint32_t>(flag)) != 0u;
}

// True when [offset, offset + size) lies inside `capacity`; safe against overflow.
constexpr bool GpuBufferRangeFits(std::size_t capacity, std::size_t offset, std::size_t size) noexcept
{
    return offset <= capacity && size <= capacity - offset;
}

// RAII owner of a device buffer. Moves, never copies.
class GpuBuffer {
public:
    // An empty buffer: IsValid() is false.
    GpuBuffer() noexcept;

    // `name` must have static storage duration. Throws std::runtime_error when the device refuses the allocation.
    GpuBuffer(DeviceRef device, std::size_t size, const char* name, GpuBufferUse use);

    // A device-local buffer filled with `data` (staged upload). `use` must not contain HostWrite or HostRead.
    static GpuBuffer CreateWithData(DeviceRef device, const void* data, std::size_t size, const char* name, GpuBufferUse use);

    ~GpuBuffer();
    GpuBuffer(GpuBuffer&&) noexcept;
    GpuBuffer& operator=(GpuBuffer&&) noexcept;
    GpuBuffer(const GpuBuffer&) = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;

    bool IsValid() const noexcept;
    std::size_t Size() const noexcept;
    const char* Name() const noexcept;

    // What Bind("name", buffer) and the graph import take. Invalid for a moved-from buffer.
    BufferObjectHandle Handle() const noexcept;

    // Copies into host-visible memory. False and one log when the range does not fit or the buffer is not HostWrite.
    bool Upload(const void* data, std::size_t size, std::size_t offset = 0);

    // Copies out of host-visible memory, invalidating the CPU cache first. False and one log on a bad range or non-HostRead.
    bool Download(void* data, std::size_t size, std::size_t offset = 0) const;

    struct Impl; // Opaque.

private:
    friend struct GpuBufferAccess;

    explicit GpuBuffer(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
};

} // namespace gte::rg
