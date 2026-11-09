// Tier-1 tests: a readback cell is only read once the frame that wrote it is done on the GPU.

#include "Core/FeatureKit/GpuReadbackPolicy.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

namespace gte::fx::detail {
namespace {

constexpr std::uint64_t kLag = kReadbackSampleLagFrames;

TEST(CellReadableTest, TheCellOfTheFrameTwoBackIsReadableOnceThatFrameIsDone)
{
    EXPECT_TRUE(CellReadable(8, 10, kLag, /*cellSerial=*/7, /*completedSerial=*/7));
    EXPECT_TRUE(CellReadable(8, 10, kLag, 7, 9));
}

TEST(CellReadableTest, ACellWrittenByAFrameStillInFlightIsNotReadable)
{
    EXPECT_FALSE(CellReadable(8, 10, kLag, 7, 6));
}

TEST(CellReadableTest, ACellOfAnotherFrameIsNotReadable)
{
    EXPECT_FALSE(CellReadable(7, 10, kLag, 6, 99));
    EXPECT_FALSE(CellReadable(9, 10, kLag, 8, 99));
}

TEST(CellReadableTest, NothingIsReadableBeforeTheLagHasPassed)
{
    EXPECT_FALSE(CellReadable(0, 1, kLag, 0, 99));
}

TEST(CellReadableTest, ASkippedSubmitMakesTheSerialLagBehindTheFrameIndex)
{
    // Frame index 10 wrote its cell, but earlier frames skipped their submit, so its serial is only 4.
    EXPECT_TRUE(CellReadable(10, 12, kLag, 4, 4));
    EXPECT_FALSE(CellReadable(10, 12, kLag, 4, 3));
}

TEST(CellReadableTest, ASynchronousWriterIsAlwaysDone)
{
    EXPECT_TRUE(CellReadable(8, 10, kLag, 0, 0));
}

TEST(CompletedSerialLimitTest, ASynchronousFrameDrainedTheGpuSoEverythingIsDone)
{
    EXPECT_EQ(CompletedSerialLimit(OffscreenFrameTicket{}), std::numeric_limits<std::uint64_t>::max());
}

TEST(CompletedSerialLimitTest, AnOverlappedFrameKnowsOnlyWhatTheSlotWaitProved)
{
    EXPECT_EQ(CompletedSerialLimit(OffscreenFrameTicket{ 1, 5, 3 }), 3u);
}

TEST(CellReadableTest, TheLagCoversEveryFrameInFlight)
{
    // A reader of frame N sees the cell of frame N - lag; the slot wait proves frame N - frames-in-flight is done.
    EXPECT_GE(kLag, static_cast<std::uint64_t>(kGpuFramesInFlight));
}

} // namespace
} // namespace gte::fx::detail
