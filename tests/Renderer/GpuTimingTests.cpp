// Pure GPU timestamp math and capability interpretation. No device involved.

#include "Renderer/GpuTiming.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(GpuTimingTest, ConvertTimestampDeltaToMillisecondsHandlesARoundPeriod)
{
    // 2.0 ns/tick, 1000-tick delta -> 2000 ns == 0.002 ms.
    const double ms = ConvertTimestampDeltaToMilliseconds(100, 1100, 2.0f, 64);
    EXPECT_NEAR(ms, 0.002, 1e-9);
}

TEST(GpuTimingTest, ConvertTimestampDeltaToMillisecondsHandlesARealisticNonRoundPeriod)
{
    // 0.641291 ns/tick is a period real hardware reports.
    const double ms = ConvertTimestampDeltaToMilliseconds(0, 1'000'000, 0.641291f, 64);
    EXPECT_NEAR(ms, 0.641291, 1e-4);
}

TEST(GpuTimingTest, ConvertTimestampDeltaToMillisecondsZeroDeltaIsZero)
{
    EXPECT_DOUBLE_EQ(ConvertTimestampDeltaToMilliseconds(12345, 12345, 1.0f, 64), 0.0);
}

TEST(GpuTimingTest, ConvertTimestampDeltaToMillisecondsWrapsAroundWithinValidBits)
{
    // With 32 valid bits a counter going from 0xFFFFFFF0 to 0x10 advanced by 32 ticks.
    constexpr std::uint32_t validBits = 32;
    const double ms = ConvertTimestampDeltaToMilliseconds(0xFFFFFFF0ULL, 0x00000010ULL, 1.0f, validBits);
    EXPECT_NEAR(ms, 0.000032, 1e-9);
}

TEST(GpuTimingTest, ConvertTimestampDeltaToMillisecondsNonPositivePeriodReturnsZero)
{
    EXPECT_DOUBLE_EQ(ConvertTimestampDeltaToMilliseconds(0, 1'000'000, 0.0f, 64), 0.0);
    EXPECT_DOUBLE_EQ(ConvertTimestampDeltaToMilliseconds(0, 1'000'000, -1.0f, 64), 0.0);
}

TEST(GpuTimingTest, GpuFramesInFlightIsTwo)
{
    EXPECT_EQ(kGpuFramesInFlight, 2u);
}

TEST(GpuTimingTest, InterpretTimestampCapabilitySupportedWhenAllInputsAreValid)
{
    const GpuTimestampCapability capability = InterpretTimestampCapability(true, 0.641291f, 64);

    EXPECT_TRUE(capability.supported);
    EXPECT_FLOAT_EQ(capability.timestampPeriodNs, 0.641291f);
    EXPECT_EQ(capability.validBits, 64u);
}

TEST(GpuTimingTest, InterpretTimestampCapabilityUnsupportedWhenComputeAndGraphicsFlagIsFalse)
{
    EXPECT_FALSE(InterpretTimestampCapability(false, 1.0f, 64).supported);
}

TEST(GpuTimingTest, InterpretTimestampCapabilityUnsupportedWhenPeriodIsZero)
{
    // A zero period is the device's way of saying timestamps are not supported.
    EXPECT_FALSE(InterpretTimestampCapability(true, 0.0f, 64).supported);
}

TEST(GpuTimingTest, InterpretTimestampCapabilityUnsupportedWhenValidBitsIsZero)
{
    EXPECT_FALSE(InterpretTimestampCapability(true, 1.0f, 0).supported);
}

TEST(GpuTimingTest, InterpretTimestampCapabilityStoresRawValuesEvenWhenUnsupported)
{
    const GpuTimestampCapability capability = InterpretTimestampCapability(false, 3.5f, 12);

    EXPECT_FALSE(capability.supported);
    EXPECT_FLOAT_EQ(capability.timestampPeriodNs, 3.5f);
    EXPECT_EQ(capability.validBits, 12u);
}

} // namespace
} // namespace gte
