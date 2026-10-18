#pragma once

#include "../GraphicsHandles.h"
#include "../../Renderer/RenderGraph/RenderGraphHandles.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>

namespace gte::fx {

class Frame;

namespace detail {

// Non-template part of GpuReadback: a ring of three device/host buffer pairs per view scope.
class ReadbackCore {
public:
    static constexpr std::uint64_t kNoFrame = ~std::uint64_t{ 0 };

    // What the verbs need for the current frame's cell.
    struct Binding {
        rg::BufferHandle device;
        rg::BufferHandle host;
        BufferObjectHandle deviceObject = BufferObjectHandle::Invalid;
        std::uint64_t size = 0;
    };

    ReadbackCore(std::string_view baseName, std::size_t byteSize);
    ~ReadbackCore();
    ReadbackCore(ReadbackCore&&) noexcept;
    ReadbackCore& operator=(ReadbackCore&&) noexcept;
    ReadbackCore(const ReadbackCore&) = delete;
    ReadbackCore& operator=(const ReadbackCore&) = delete;

    // Creates the buffers on first use and imports this frame's cell once. Empty while creation is backing off.
    std::optional<Binding> Acquire(const Frame& frame);

    // Marks this frame's host buffer as written by a declared copy.
    void NoteCopied(const Frame& frame);

    // Copies the bytes recorded exactly two frames ago into `out`. False when there are none.
    bool Latest(const Frame& frame, void* out, std::uint64_t& recordedFrame) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

struct ReadbackAccess;

} // namespace detail

// A small GPU-written buffer the CPU reads two frames later. Owned by the feature; destroy it only while the GPU is idle.
//   f.ClearReadback("Stats.Clear", r, 0xFFFFFFFFu); f.Compute(...).BindOutput("result", r).Dispatch(extent);
//   f.CopyReadbackToHost("Stats.Readback", r);  ... later: r.Latest(f)
template <typename T>
class GpuReadback {
    static_assert(std::is_trivially_copyable_v<T> && sizeof(T) % 4 == 0 && sizeof(T) <= 256,
        "GpuReadback<T> is copied as raw 32-bit words");

public:
    struct Sample {
        T value;
        std::uint64_t frame;
    };

    explicit GpuReadback(std::string_view baseName)
        : m_core(baseName, sizeof(T))
    {
    }

    // The value recorded two frames ago for this frame's view, or nothing (first frames, skipped frames,
    // a toggled-off pass, a refused allocation).
    std::optional<Sample> Latest(const Frame& frame) const
    {
        Sample sample{};
        if (!m_core.Latest(frame, &sample.value, sample.frame)) {
            return std::nullopt;
        }
        return sample;
    }

private:
    friend struct detail::ReadbackAccess;

    detail::ReadbackCore m_core;
};

namespace detail {

// The verbs reach the core through this; feature code cannot.
struct ReadbackAccess {
    template <typename T>
    static ReadbackCore& Of(GpuReadback<T>& readback) noexcept
    {
        return readback.m_core;
    }
};

} // namespace detail

} // namespace gte::fx
