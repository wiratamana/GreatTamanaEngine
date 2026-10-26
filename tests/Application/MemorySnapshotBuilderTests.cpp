#include "Application/MemorySnapshotBuilder.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

// Every field gets a distinct value so a swapped pair is caught.
TEST(MemorySnapshotBuilderTest, MapsEveryFieldToItsMatchingSnapshotField)
{
    GpuMemoryTracker::Totals totals;
    totals.totalBytes = 1000;
    totals.bufferBytes = 200;
    totals.textureBytes = 800;
    totals.gpuOnlyBytes = 600;
    totals.cpuOnlyBytes = 150;
    totals.sharedBytes = 250;
    totals.bufferCount = 3;
    totals.textureCount = 5;

    const Profiling::MemorySnapshot snapshot = BuildMemorySnapshot(totals);

    EXPECT_EQ(snapshot.status, Profiling::SampleStatus::Present);
    EXPECT_EQ(snapshot.totalBytes, 1000u);
    EXPECT_EQ(snapshot.bufferBytes, 200u);
    EXPECT_EQ(snapshot.textureBytes, 800u);
    EXPECT_EQ(snapshot.gpuOnlyBytes, 600u);
    EXPECT_EQ(snapshot.cpuOnlyBytes, 150u);
    EXPECT_EQ(snapshot.sharedBytes, 250u);
    EXPECT_EQ(snapshot.bufferCount, 3u);
    EXPECT_EQ(snapshot.textureCount, 5u);
}

// All-zero totals are a real measurement, never Absent.
TEST(MemorySnapshotBuilderTest, AllZeroTotalsStillReportsPresent)
{
    const GpuMemoryTracker::Totals totals;
    const Profiling::MemorySnapshot snapshot = BuildMemorySnapshot(totals);

    EXPECT_EQ(snapshot.status, Profiling::SampleStatus::Present);
    EXPECT_EQ(snapshot.totalBytes, 0u);
    EXPECT_EQ(snapshot.bufferCount, 0u);
    EXPECT_EQ(snapshot.textureCount, 0u);
}

} // namespace
} // namespace gte
