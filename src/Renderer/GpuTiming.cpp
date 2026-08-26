#include "GpuTiming.h"

namespace gte {

double ConvertTimestampDeltaToMilliseconds(
    std::uint64_t startTicks, std::uint64_t endTicks, float timestampPeriodNs, std::uint32_t validBits) noexcept
{
    if (timestampPeriodNs <= 0.0f) {
        return 0.0;
    }

    // A validBits of 64 (or, defensively, anything >= 64) means "the full
    // counter width" - shifting a std::uint64_t by 64 is undefined
    // behavior in C++, so that case is handled as an all-ones mask
    // directly rather than computing (1 << 64).
    const std::uint64_t mask =
        (validBits >= 64) ? ~std::uint64_t{ 0 } : ((std::uint64_t{ 1 } << validBits) - 1);

    const std::uint64_t maskedStart = startTicks & mask;
    const std::uint64_t maskedEnd = endTicks & mask;
    // Modular subtraction within `validBits` - correct even across exactly
    // one counter wraparound (see this function's own header comment).
    const std::uint64_t deltaTicks = (maskedEnd - maskedStart) & mask;

    return static_cast<double>(deltaTicks) * static_cast<double>(timestampPeriodNs) / 1'000'000.0;
}

GpuTimestampCapability InterpretTimestampCapability(
    bool timestampComputeAndGraphics, float timestampPeriod, std::uint32_t validBits) noexcept
{
    GpuTimestampCapability capability;
    capability.timestampPeriodNs = timestampPeriod;
    capability.validBits = validBits;
    capability.supported = timestampComputeAndGraphics && timestampPeriod > 0.0f && validBits > 0;
    return capability;
}

} // namespace gte
