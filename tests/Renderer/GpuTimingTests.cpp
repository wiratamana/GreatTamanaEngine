// Unit tests for Phase 4A's pure GPU-timestamp capability interpretation +
// tick-delta/slot-indexing math (src/Renderer/GpuTiming.h) - see
// PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md, "Phase 4A". No Vulkan/
// Renderer/live GPU device involved at all: no VkQueryPool is created
// anywhere in Phase 4A, so VulkanDevice::TimestampCapability()'s actual
// VALUE against a real device is Tier 2 (verified manually - see that
// document's own "Testing (Phase 4A)" section) while
// InterpretTimestampCapability() - the pure DECISION logic VulkanDevice's
// constructor calls with real, device-queried arguments - is exercised
// here directly with hand-fabricated inputs instead.

#include "Renderer/GpuTiming.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

// --- ConvertTimestampDeltaToMilliseconds() ---------------------------------

TEST(GpuTimingTest, ConvertTimestampDeltaToMillisecondsHandlesARoundPeriod)
{
    // 2.0 ns/tick, 1000-tick delta -> 1000 * 2.0 ns == 2000 ns == 0.002 ms.
    const double ms = ConvertTimestampDeltaToMilliseconds(100, 1100, 2.0f, 64);
    EXPECT_NEAR(ms, 0.002, 1e-9);
}

TEST(GpuTimingTest, ConvertTimestampDeltaToMillisecondsHandlesARealisticNonRoundPeriod)
{
    // 0.641291 ns/tick is a realistic, non-round value actually reported by
    // real hardware (see PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md's own
    // Phase 4A testing section) - 1,000,000-tick delta -> ~0.641291 ms.
    const double ms = ConvertTimestampDeltaToMilliseconds(0, 1'000'000, 0.641291f, 64);
    EXPECT_NEAR(ms, 0.641291, 1e-4);
}

TEST(GpuTimingTest, ConvertTimestampDeltaToMillisecondsZeroDeltaIsZero)
{
    EXPECT_DOUBLE_EQ(ConvertTimestampDeltaToMilliseconds(12345, 12345, 1.0f, 64), 0.0);
}

TEST(GpuTimingTest, ConvertTimestampDeltaToMillisecondsWrapsAroundWithinValidBits)
{
    // validBits == 32: a counter that goes from 0xFFFFFFF0 to 0x00000010
    // has genuinely advanced by 32 ticks (a forward wrap), NOT underflowed
    // to some enormous unsigned value - the single most important
    // regression case in this whole file (see this file's own header
    // comment and GpuTiming.h's own doc comment on masking).
    constexpr std::uint32_t validBits = 32;
    const std::uint64_t start = 0xFFFFFFF0ULL;
    const std::uint64_t end = 0x00000010ULL; // wrapped past 2^32
    const double ms = ConvertTimestampDeltaToMilliseconds(start, end, 1.0f, validBits);

    // 32 ticks * 1.0 ns/tick == 32 ns == 0.000032 ms.
    EXPECT_NEAR(ms, 0.000032, 1e-9);
}

TEST(GpuTimingTest, ConvertTimestampDeltaToMillisecondsNonPositivePeriodReturnsZero)
{
    EXPECT_DOUBLE_EQ(ConvertTimestampDeltaToMilliseconds(0, 1'000'000, 0.0f, 64), 0.0);
    EXPECT_DOUBLE_EQ(ConvertTimestampDeltaToMilliseconds(0, 1'000'000, -1.0f, 64), 0.0);
}

// --- PresentTimestampSlotBase() --------------------------------------------

TEST(GpuTimingTest, PresentTimestampSlotBaseReturnsDocumentedValues)
{
    EXPECT_EQ(PresentTimestampSlotBase(0), 0u);
    EXPECT_EQ(PresentTimestampSlotBase(1), 2u);
}

TEST(GpuTimingTest, PresentTimestampSlotBasePlusOneIsTheEndSlot)
{
    // The "+1 for the end timestamp" convention (see GpuTiming.h's own doc
    // comment) is asserted directly rather than assumed.
    EXPECT_EQ(PresentTimestampSlotBase(0) + 1, 1u);
    EXPECT_EQ(PresentTimestampSlotBase(1) + 1, 3u);
}

// --- Fixed enum/constant layout ---------------------------------------------

TEST(GpuTimingTest, GpuTimingSlotValuesAndCountAreExactlyAsDocumented)
{
    EXPECT_EQ(static_cast<std::uint32_t>(GpuTimingSlot::Offscreen0), 0u);
    EXPECT_EQ(static_cast<std::uint32_t>(GpuTimingSlot::Offscreen1), 1u);
    EXPECT_EQ(static_cast<std::uint32_t>(GpuTimingSlot::SwapchainPresent), 2u);
    EXPECT_EQ(kGpuTimingSlotCount, 3u);
}

TEST(GpuTimingTest, KGpuTimingFramesInFlightMatchesFramePresenterConvention)
{
    // Must always agree in VALUE with FramePresenter::kFramesInFlight (kept
    // as two separate named constants on purpose - see GpuTiming.h's own
    // doc comment and PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md's design
    // decision log).
    EXPECT_EQ(kGpuTimingFramesInFlight, 2u);
}

// --- InterpretTimestampCapability() -----------------------------------------

TEST(GpuTimingTest, InterpretTimestampCapabilitySupportedWhenAllInputsAreValid)
{
    const GpuTimestampCapability capability = InterpretTimestampCapability(true, 0.641291f, 64);

    EXPECT_TRUE(capability.supported);
    EXPECT_FLOAT_EQ(capability.timestampPeriodNs, 0.641291f);
    EXPECT_EQ(capability.validBits, 64u);
}

TEST(GpuTimingTest, InterpretTimestampCapabilityUnsupportedWhenComputeAndGraphicsFlagIsFalse)
{
    const GpuTimestampCapability capability = InterpretTimestampCapability(false, 1.0f, 64);
    EXPECT_FALSE(capability.supported);
}

TEST(GpuTimingTest, InterpretTimestampCapabilityUnsupportedWhenPeriodIsZero)
{
    // A timestampPeriod of exactly 0 is itself a valid "not supported"
    // signal per the Vulkan spec - never assumed positive.
    const GpuTimestampCapability capability = InterpretTimestampCapability(true, 0.0f, 64);
    EXPECT_FALSE(capability.supported);
}

TEST(GpuTimingTest, InterpretTimestampCapabilityUnsupportedWhenValidBitsIsZero)
{
    // timestampValidBits == 0 is itself a valid "this family can't do it"
    // signal per the Vulkan spec - never assumed non-zero.
    const GpuTimestampCapability capability = InterpretTimestampCapability(true, 1.0f, 0);
    EXPECT_FALSE(capability.supported);
}

TEST(GpuTimingTest, InterpretTimestampCapabilityStoresRawValuesEvenWhenUnsupported)
{
    // The raw period/validBits are still recorded verbatim even when the
    // overall result is unsupported - useful for diagnostics, and simpler
    // than special-casing them to 0 on the unsupported path.
    const GpuTimestampCapability capability = InterpretTimestampCapability(false, 3.5f, 12);

    EXPECT_FALSE(capability.supported);
    EXPECT_FLOAT_EQ(capability.timestampPeriodNs, 3.5f);
    EXPECT_EQ(capability.validBits, 12u);
}

} // namespace
} // namespace gte
