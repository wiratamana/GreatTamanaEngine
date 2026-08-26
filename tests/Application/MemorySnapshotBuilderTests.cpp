#include "Application/MemorySnapshotBuilder.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

// Every field is given a DISTINCT value so a transposed-field bug (e.g.
// bufferBytes/textureBytes swapped) is guaranteed to be caught - this is
// the ONE test in this phase that actually calls BuildMemorySnapshot()
// itself, rather than hand-constructing a MemorySnapshot directly (see
// tests/Profiling/FrameProfilerTests.cpp's own new tests, which
// deliberately do NOT exercise this function - see
// PHASE5_GPU_MEMORY_HISTORY_STRATEGY_v2.md, "Changes from v1").
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

    EXPECT_EQ(snapshot.status, Profiling::GpuSampleStatus::Present);
    EXPECT_EQ(snapshot.totalBytes, 1000u);
    EXPECT_EQ(snapshot.bufferBytes, 200u);
    EXPECT_EQ(snapshot.textureBytes, 800u);
    EXPECT_EQ(snapshot.gpuOnlyBytes, 600u);
    EXPECT_EQ(snapshot.cpuOnlyBytes, 150u);
    EXPECT_EQ(snapshot.sharedBytes, 250u);
    EXPECT_EQ(snapshot.bufferCount, 3u);
    EXPECT_EQ(snapshot.textureCount, 5u);
}

// A Totals value that is genuinely all-zero (e.g. a Renderer with no live
// Buffer/RenderTexture yet) must still map to status == Present, never
// Absent - the function's whole point is that it ALWAYS reports a real
// measurement; "no data" is a FrameProfiler-level concept (never calling
// SetMemorySnapshot() at all), never something this function decides.
TEST(MemorySnapshotBuilderTest, AllZeroTotalsStillReportsPresent)
{
    const GpuMemoryTracker::Totals totals; // Default-constructed, all zero.
    const Profiling::MemorySnapshot snapshot = BuildMemorySnapshot(totals);

    EXPECT_EQ(snapshot.status, Profiling::GpuSampleStatus::Present);
    EXPECT_EQ(snapshot.totalBytes, 0u);
    EXPECT_EQ(snapshot.bufferCount, 0u);
    EXPECT_EQ(snapshot.textureCount, 0u);
}

} // namespace
} // namespace gte
