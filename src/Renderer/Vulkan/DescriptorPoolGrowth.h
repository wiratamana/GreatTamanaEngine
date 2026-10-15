#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace gte {

// Per-pool descriptor budget for the shared descriptor pool set.
struct PoolCapacity {
    std::uint32_t sets = 0;
    std::uint32_t storageBuffers = 0;
    std::uint32_t storageImages = 0;
    std::uint32_t combinedSamplers = 0;
    std::uint32_t uniformBuffers = 0;

    friend constexpr bool operator==(const PoolCapacity&, const PoolCapacity&) = default;
};

inline constexpr std::size_t kMaxDescriptorPools = 8;

namespace detail {
constexpr std::uint32_t DoubleSaturating(std::uint32_t v) noexcept
{
    return v > std::numeric_limits<std::uint32_t>::max() / 2 ? std::numeric_limits<std::uint32_t>::max() : v * 2;
}
} // namespace detail

// Capacity of the next pool: double the last one. Empty once the pool count cap is reached.
constexpr std::optional<PoolCapacity> NextPoolCapacity(const PoolCapacity& last, std::size_t poolCountSoFar) noexcept
{
    if (poolCountSoFar >= kMaxDescriptorPools) {
        return std::nullopt;
    }
    return PoolCapacity{ detail::DoubleSaturating(last.sets), detail::DoubleSaturating(last.storageBuffers),
        detail::DoubleSaturating(last.storageImages), detail::DoubleSaturating(last.combinedSamplers),
        detail::DoubleSaturating(last.uniformBuffers) };
}

// Per-type live accounting for the pool set (counts only, no Vulkan). `sets` counts descriptor sets.
class DescriptorBudget {
public:
    static constexpr float kWarnFraction = 0.75f;  // TakeWarning() fires above this.
    static constexpr float kRearmFraction = 0.60f; // ...and fires again only after dropping below this.

    void AddCapacity(const PoolCapacity& pool) noexcept { m_capacity = Add(m_capacity, pool); }

    void OnAllocated(const PoolCapacity& cost) noexcept
    {
        m_used = Add(m_used, cost);
        m_highWater = Max(m_highWater, m_used);
    }

    void OnFreed(const PoolCapacity& cost) noexcept { m_used = SubtractSaturating(m_used, cost); }

    const PoolCapacity& Capacity() const noexcept { return m_capacity; }
    const PoolCapacity& Used() const noexcept { return m_used; }
    const PoolCapacity& HighWater() const noexcept { return m_highWater; }

    // Highest used/capacity ratio over all five counters; a counter without capacity is ignored.
    float PeakFraction() const noexcept
    {
        return std::max({ Fraction(m_used.sets, m_capacity.sets), Fraction(m_used.storageBuffers, m_capacity.storageBuffers),
            Fraction(m_used.storageImages, m_capacity.storageImages),
            Fraction(m_used.combinedSamplers, m_capacity.combinedSamplers),
            Fraction(m_used.uniformBuffers, m_capacity.uniformBuffers) });
    }

    // True once per climb above kWarnFraction; re-arms after the peak falls below kRearmFraction.
    bool TakeWarning() noexcept
    {
        const float peak = PeakFraction();
        if (peak < kRearmFraction) {
            m_warned = false;
        } else if (peak > kWarnFraction && !m_warned) {
            m_warned = true;
            return true;
        }
        return false;
    }

    static constexpr float Fraction(std::uint32_t used, std::uint32_t capacity) noexcept
    {
        return capacity == 0 ? 0.0f : static_cast<float>(used) / static_cast<float>(capacity);
    }

private:
    static constexpr std::uint32_t AddSaturating(std::uint32_t a, std::uint32_t b) noexcept
    {
        return a > std::numeric_limits<std::uint32_t>::max() - b ? std::numeric_limits<std::uint32_t>::max() : a + b;
    }
    static constexpr std::uint32_t SubSaturating(std::uint32_t a, std::uint32_t b) noexcept { return a > b ? a - b : 0u; }

    static constexpr PoolCapacity Add(const PoolCapacity& a, const PoolCapacity& b) noexcept
    {
        return { AddSaturating(a.sets, b.sets), AddSaturating(a.storageBuffers, b.storageBuffers),
            AddSaturating(a.storageImages, b.storageImages), AddSaturating(a.combinedSamplers, b.combinedSamplers),
            AddSaturating(a.uniformBuffers, b.uniformBuffers) };
    }
    static constexpr PoolCapacity SubtractSaturating(const PoolCapacity& a, const PoolCapacity& b) noexcept
    {
        return { SubSaturating(a.sets, b.sets), SubSaturating(a.storageBuffers, b.storageBuffers),
            SubSaturating(a.storageImages, b.storageImages), SubSaturating(a.combinedSamplers, b.combinedSamplers),
            SubSaturating(a.uniformBuffers, b.uniformBuffers) };
    }
    static constexpr PoolCapacity Max(const PoolCapacity& a, const PoolCapacity& b) noexcept
    {
        return { std::max(a.sets, b.sets), std::max(a.storageBuffers, b.storageBuffers),
            std::max(a.storageImages, b.storageImages), std::max(a.combinedSamplers, b.combinedSamplers),
            std::max(a.uniformBuffers, b.uniformBuffers) };
    }

    PoolCapacity m_capacity;
    PoolCapacity m_used;
    PoolCapacity m_highWater;
    bool m_warned = false;
};

// Snapshot for the GPU Memory panel and logs.
struct DescriptorPoolStats {
    std::size_t poolCount = 0;
    PoolCapacity capacity;
    PoolCapacity used;
    PoolCapacity highWater;
};

} // namespace gte
