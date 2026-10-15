#pragma once

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

} // namespace gte
