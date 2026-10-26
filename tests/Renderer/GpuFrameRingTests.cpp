// Pure slot and frame-index rules of the pipelined GPU timing read.

#include "Renderer/GpuFrameRing.h"
#include "Renderer/NameHash.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(GpuFrameRingTest, EmptySlotHasNothingToRead)
{
    GpuFrameRing ring;
    EXPECT_EQ(ring.Take(0, 10).status, GpuFrameRing::Status::None);
    EXPECT_EQ(ring.Take(kGpuFramesInFlight, 10).status, GpuFrameRing::Status::None);
}

TEST(GpuFrameRingTest, FrameInsideTheRingIsReadyOnce)
{
    GpuFrameRing ring;
    ring.Submitted(1, 5);
    const GpuFrameRing::Taken taken = ring.Take(1, 5 + kGpuFramesInFlight - 1);
    EXPECT_EQ(taken.status, GpuFrameRing::Status::Ready);
    EXPECT_EQ(taken.frameIndex, 5u);
    EXPECT_EQ(ring.Take(1, 6).status, GpuFrameRing::Status::None);
}

TEST(GpuFrameRingTest, FrameThatLeftTheRingIsDropped)
{
    GpuFrameRing ring;
    ring.Submitted(0, 5);
    const GpuFrameRing::Taken taken = ring.Take(0, 5 + kGpuFramesInFlight);
    EXPECT_EQ(taken.status, GpuFrameRing::Status::Dropped);
    EXPECT_EQ(taken.frameIndex, 5u);
}

TEST(GpuFrameRingTest, ACounterBehindTheFrameIsDropped)
{
    GpuFrameRing ring;
    ring.Submitted(0, 9);
    EXPECT_EQ(ring.Take(0, 3).status, GpuFrameRing::Status::Dropped);
}

TEST(GpuFrameRingTest, ASlotOutOfRangeIsIgnored)
{
    GpuFrameRing ring;
    ring.Submitted(kGpuFramesInFlight, 1);
    EXPECT_EQ(ring.Take(kGpuFramesInFlight, 1).status, GpuFrameRing::Status::None);
}

TEST(NameHashTest, SameTextGivesTheSameHashAndDifferentTextDiffers)
{
    EXPECT_EQ(NameHash32("RenderOpaque"), NameHash32("RenderOpaque"));
    EXPECT_NE(NameHash32("RenderOpaque"), NameHash32("RenderTransparent"));
    EXPECT_EQ(NameHash32(""), 2166136261u);
}

} // namespace
} // namespace gte
