// Unit tests for GpuMemoryTracker (src/Renderer/Memory/GpuMemoryTracker.h) -
// exercises the pure bookkeeping logic (Track()/Untrack()/GetTotals()/
// GetAllResources(), generation-counted handle reuse, and - only in a
// GTE_ENABLE_EDITOR build - debug names) entirely in isolation. None of this
// touches a real VmaAllocator/VkDevice: Track() only ever takes plain enums
// and a byte count (ClassifyGpuMemoryLocation(), which DOES need a real VMA
// allocation, is exercised indirectly by Buffer/RenderTexture instead - see
// tests/CMakeLists.txt's "Tier 2" note), so no live Vulkan device or GPU is
// required to run any test in this file.

#include "Renderer/Memory/GpuMemoryTracker.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

namespace gte {
namespace {

TEST(GpuMemoryTrackerTest, FreshTrackerHasZeroTotalsAndNoResources)
{
    GpuMemoryTracker tracker;
    const GpuMemoryTracker::Totals totals = tracker.GetTotals();

    EXPECT_EQ(totals.totalBytes, 0u);
    EXPECT_EQ(totals.bufferBytes, 0u);
    EXPECT_EQ(totals.textureBytes, 0u);
    EXPECT_EQ(totals.gpuOnlyBytes, 0u);
    EXPECT_EQ(totals.cpuOnlyBytes, 0u);
    EXPECT_EQ(totals.sharedBytes, 0u);
    EXPECT_EQ(totals.bufferCount, 0u);
    EXPECT_EQ(totals.textureCount, 0u);
    EXPECT_TRUE(tracker.GetAllResources().empty());
}

TEST(GpuMemoryTrackerTest, TrackReturnsAValidHandle)
{
    GpuMemoryTracker tracker;
    const GpuResourceHandle handle = tracker.Track(GpuResourceType::Buffer, GpuMemoryLocation::GpuOnly, 1024);

    EXPECT_TRUE(handle.IsValid());
    EXPECT_EQ(handle.index, 0u);
    EXPECT_EQ(handle.generation, 1u);
}

TEST(GpuMemoryTrackerTest, TrackingABuffer_UpdatesTotalsAndPerTypeCounters)
{
    GpuMemoryTracker tracker;
    tracker.Track(GpuResourceType::Buffer, GpuMemoryLocation::GpuOnly, 1024);

    const GpuMemoryTracker::Totals totals = tracker.GetTotals();
    EXPECT_EQ(totals.totalBytes, 1024u);
    EXPECT_EQ(totals.bufferBytes, 1024u);
    EXPECT_EQ(totals.textureBytes, 0u);
    EXPECT_EQ(totals.gpuOnlyBytes, 1024u);
    EXPECT_EQ(totals.bufferCount, 1u);
    EXPECT_EQ(totals.textureCount, 0u);
}

TEST(GpuMemoryTrackerTest, TrackingATexture_UpdatesTotalsAndPerTypeCounters)
{
    GpuMemoryTracker tracker;
    tracker.Track(GpuResourceType::Texture, GpuMemoryLocation::Shared, 2048);

    const GpuMemoryTracker::Totals totals = tracker.GetTotals();
    EXPECT_EQ(totals.totalBytes, 2048u);
    EXPECT_EQ(totals.textureBytes, 2048u);
    EXPECT_EQ(totals.bufferBytes, 0u);
    EXPECT_EQ(totals.sharedBytes, 2048u);
    EXPECT_EQ(totals.textureCount, 1u);
    EXPECT_EQ(totals.bufferCount, 0u);
}

TEST(GpuMemoryTrackerTest, MultipleResources_AggregateAcrossTypesAndLocations)
{
    GpuMemoryTracker tracker;
    tracker.Track(GpuResourceType::Buffer, GpuMemoryLocation::GpuOnly, 500);
    tracker.Track(GpuResourceType::Texture, GpuMemoryLocation::Shared, 2000);
    tracker.Track(GpuResourceType::Buffer, GpuMemoryLocation::CpuOnly, 300);

    const GpuMemoryTracker::Totals totals = tracker.GetTotals();
    EXPECT_EQ(totals.totalBytes, 2800u);
    EXPECT_EQ(totals.bufferBytes, 800u);
    EXPECT_EQ(totals.textureBytes, 2000u);
    EXPECT_EQ(totals.gpuOnlyBytes, 500u);
    EXPECT_EQ(totals.cpuOnlyBytes, 300u);
    EXPECT_EQ(totals.sharedBytes, 2000u);
    EXPECT_EQ(totals.bufferCount, 2u);
    EXPECT_EQ(totals.textureCount, 1u);
    EXPECT_EQ(tracker.GetAllResources().size(), 3u);
}

TEST(GpuMemoryTrackerTest, Untrack_RemovesFromTotalsAndSnapshot)
{
    GpuMemoryTracker tracker;
    const GpuResourceHandle handle = tracker.Track(GpuResourceType::Buffer, GpuMemoryLocation::GpuOnly, 1024);

    tracker.Untrack(handle);

    const GpuMemoryTracker::Totals totals = tracker.GetTotals();
    EXPECT_EQ(totals.totalBytes, 0u);
    EXPECT_EQ(totals.bufferCount, 0u);
    EXPECT_TRUE(tracker.GetAllResources().empty());
}

TEST(GpuMemoryTrackerTest, Untrack_OutOfRangeHandleIsHarmlessNoop)
{
    GpuMemoryTracker tracker;
    tracker.Track(GpuResourceType::Buffer, GpuMemoryLocation::GpuOnly, 1024);

    // Never issued by this tracker (index way out of range) - must not
    // crash and must not disturb the one real entry above.
    tracker.Untrack(GpuResourceHandle{ 999, 1 });

    EXPECT_EQ(tracker.GetTotals().totalBytes, 1024u);
    EXPECT_EQ(tracker.GetAllResources().size(), 1u);
}

TEST(GpuMemoryTrackerTest, Untrack_StaleGenerationHandleIsHarmlessNoop_AndCannotDeleteAReusedSlot)
{
    GpuMemoryTracker tracker;
    const GpuResourceHandle first = tracker.Track(GpuResourceType::Buffer, GpuMemoryLocation::GpuOnly, 100);
    tracker.Untrack(first);

    // Slot 0 is now free-listed. Tracking again reuses it with a bumped
    // generation - simulates e.g. RenderTexture::Resize()'s
    // Destroy()+Create() re-tracking into the same slot (see AGENTS.md,
    // "GPU resource memory tracking").
    const GpuResourceHandle second = tracker.Track(GpuResourceType::Texture, GpuMemoryLocation::Shared, 4096);

    ASSERT_EQ(first.index, second.index);
    EXPECT_NE(first.generation, second.generation);

    // A caller that (incorrectly) still held onto `first` and calls
    // Untrack() with it again must NOT silently delete `second`'s record -
    // this is exactly the bug the generation counter exists to prevent.
    tracker.Untrack(first);

    const GpuMemoryTracker::Totals totals = tracker.GetTotals();
    EXPECT_EQ(totals.totalBytes, 4096u);
    EXPECT_EQ(totals.textureCount, 1u);
    ASSERT_EQ(tracker.GetAllResources().size(), 1u);
    EXPECT_EQ(tracker.GetAllResources().front().handle, second);
}

TEST(GpuMemoryTrackerTest, Untrack_IsIdempotent)
{
    GpuMemoryTracker tracker;
    const GpuResourceHandle handle = tracker.Track(GpuResourceType::Buffer, GpuMemoryLocation::GpuOnly, 64);

    tracker.Untrack(handle);
    // Calling it again with the same (now-stale) handle must be a no-op,
    // not double-subtract from the totals (e.g. underflowing bufferCount).
    tracker.Untrack(handle);

    EXPECT_EQ(tracker.GetTotals().totalBytes, 0u);
    EXPECT_EQ(tracker.GetTotals().bufferCount, 0u);
}

TEST(GpuMemoryTrackerTest, GetAllResources_ReflectsOnlyCurrentlyLiveEntries)
{
    GpuMemoryTracker tracker;
    const GpuResourceHandle a = tracker.Track(GpuResourceType::Buffer, GpuMemoryLocation::GpuOnly, 10);
    const GpuResourceHandle b = tracker.Track(GpuResourceType::Buffer, GpuMemoryLocation::GpuOnly, 20);
    const GpuResourceHandle c = tracker.Track(GpuResourceType::Texture, GpuMemoryLocation::Shared, 30);

    tracker.Untrack(b);

    const std::vector<GpuMemoryTracker::Entry> live = tracker.GetAllResources();
    ASSERT_EQ(live.size(), 2u);
    EXPECT_TRUE(std::none_of(live.begin(), live.end(), [&](const auto& e) { return e.handle == b; }));
    EXPECT_TRUE(std::any_of(live.begin(), live.end(), [&](const auto& e) { return e.handle == a; }));
    EXPECT_TRUE(std::any_of(live.begin(), live.end(), [&](const auto& e) { return e.handle == c; }));
}

TEST(GpuMemoryTrackerTest, Track_DefaultsFormatToUndefinedWhenNotSpecified)
{
    GpuMemoryTracker tracker;
    const GpuResourceHandle handle = tracker.Track(GpuResourceType::Buffer, GpuMemoryLocation::GpuOnly, 1024);

    const std::vector<GpuMemoryTracker::Entry> live = tracker.GetAllResources();
    ASSERT_EQ(live.size(), 1u);
    EXPECT_EQ(live.front().handle, handle);
    EXPECT_EQ(live.front().record.format, VK_FORMAT_UNDEFINED);
}

TEST(GpuMemoryTrackerTest, Track_RecordsTheSuppliedFormatForATexture)
{
    GpuMemoryTracker tracker;
    tracker.Track(GpuResourceType::Texture, GpuMemoryLocation::GpuOnly, 2048, VK_FORMAT_D32_SFLOAT);

    const std::vector<GpuMemoryTracker::Entry> live = tracker.GetAllResources();
    ASSERT_EQ(live.size(), 1u);
    EXPECT_EQ(live.front().record.format, VK_FORMAT_D32_SFLOAT);
}

#if GTE_ENABLE_EDITOR

TEST(GpuMemoryTrackerTest, DebugName_RoundTripsForAValidHandle)
{
    GpuMemoryTracker tracker;
    const GpuResourceHandle handle = tracker.Track(GpuResourceType::Buffer, GpuMemoryLocation::GpuOnly, 128);

    tracker.SetDebugName(handle, "PlayerVertexBuffer");

    EXPECT_EQ(tracker.GetDebugName(handle), "PlayerVertexBuffer");
}

TEST(GpuMemoryTrackerTest, DebugName_UnsetHandleReturnsEmptyString)
{
    GpuMemoryTracker tracker;
    const GpuResourceHandle handle = tracker.Track(GpuResourceType::Buffer, GpuMemoryLocation::GpuOnly, 128);

    EXPECT_TRUE(tracker.GetDebugName(handle).empty());
}

TEST(GpuMemoryTrackerTest, DebugName_InvalidHandleReturnsEmptyStringWithoutCrashing)
{
    GpuMemoryTracker tracker;

    EXPECT_TRUE(tracker.GetDebugName(kInvalidGpuResourceHandle).empty());
    tracker.SetDebugName(kInvalidGpuResourceHandle, "ShouldBeIgnored"); // must not crash/insert anything
}

TEST(GpuMemoryTrackerTest, DebugName_IsForgottenAfterUntrack)
{
    GpuMemoryTracker tracker;
    const GpuResourceHandle handle = tracker.Track(GpuResourceType::Buffer, GpuMemoryLocation::GpuOnly, 128);
    tracker.SetDebugName(handle, "Temp");

    tracker.Untrack(handle);

    EXPECT_TRUE(tracker.GetDebugName(handle).empty());
}

#endif // GTE_ENABLE_EDITOR

} // namespace
} // namespace gte
