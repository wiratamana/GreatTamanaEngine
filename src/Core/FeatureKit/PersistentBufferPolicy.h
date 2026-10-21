#pragma once

#include <cstdint>

namespace gte::fx {

inline constexpr std::uint64_t kMinPersistentBufferBytes = 256;
// The smallest maxStorageBufferRange a device may report: a whole-buffer descriptor is always legal.
inline constexpr std::uint64_t kMaxPersistentBufferBytes = std::uint64_t{ 128 } << 20;

enum class PersistentBufferDecision : std::uint8_t {
    Create,
    Reuse,
    Grow,
    RefuseEmpty,
    RefuseTooLarge,
    RefuseGrowWhilePipelined,
};

// existingCapacity is 0 until the first allocation.
constexpr PersistentBufferDecision DecidePersistentBufferAcquire(
    std::uint64_t existingCapacity, std::uint64_t requestedBytes, bool pipelined) noexcept
{
    if (requestedBytes == 0) {
        return PersistentBufferDecision::RefuseEmpty;
    }
    if (requestedBytes > kMaxPersistentBufferBytes) {
        return PersistentBufferDecision::RefuseTooLarge;
    }
    if (existingCapacity >= requestedBytes) {
        return PersistentBufferDecision::Reuse;
    }
    if (existingCapacity == 0) {
        return PersistentBufferDecision::Create;
    }
    return pipelined ? PersistentBufferDecision::RefuseGrowWhilePipelined : PersistentBufferDecision::Grow;
}

// The allocation size for a request: the next power of two, at least the minimum, at most the maximum.
constexpr std::uint64_t PersistentBufferCapacityFor(std::uint64_t requestedBytes) noexcept
{
    std::uint64_t capacity = kMinPersistentBufferBytes;
    while (capacity < requestedBytes && capacity < kMaxPersistentBufferBytes) {
        capacity <<= 1;
    }
    return capacity;
}

} // namespace gte::fx
