// Unit tests for EditorGpuMemoryNameOverlay (src/Editor/EditorGpuMemoryNameOverlay.h)
// - editor-core-separation-1 campaign, PHASE4
// (task_manager/editor-core-separation-1/PHASE4_GPU_MEMORY_TRACKER_BUCKET_A_EXTRACTION.md).
// Genuinely Tier 1: exercises the observer/name-lookup logic against a plain
// GpuMemoryTracker instance - no live VmaAllocator/VkDevice needed (see
// tests/Memory/GpuMemoryTrackerTests.cpp's own identical "Tier 1" framing).
//
// This overlay's own storage is intentionally a single global/static table
// (mirroring Logger/SdlMemoryTracker/ImGuiMemoryTracker's own precedent -
// see the class's own header comment), so every test here resets it via
// ResetForTesting() in SetUp()/TearDown() - otherwise two independent
// TEST() cases, each constructing their own short-lived GpuMemoryTracker,
// would innocently produce the exact same GpuResourceHandle{0,1} (a fresh
// tracker's first-ever handle) and could see a stale name bleed across
// tests.

#include "Editor/EditorGpuMemoryNameOverlay.h"
#include "Renderer/Memory/GpuMemoryTracker.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

class EditorGpuMemoryNameOverlayTest : public ::testing::Test {
protected:
    void SetUp() override { EditorGpuMemoryNameOverlay::ResetForTesting(); }
    void TearDown() override { EditorGpuMemoryNameOverlay::ResetForTesting(); }
};

TEST_F(EditorGpuMemoryNameOverlayTest, InstallThenSetDebugName_RoundTripsForAValidHandle)
{
    GpuMemoryTracker tracker;
    EditorGpuMemoryNameOverlay::Install(tracker);

    const GpuResourceHandle handle = tracker.Track(GpuResourceType::Buffer, GpuMemoryLocation::GpuOnly, 128);
    tracker.SetDebugName(handle, "PlayerVertexBuffer");

    EXPECT_EQ(EditorGpuMemoryNameOverlay::GetDebugName(handle), "PlayerVertexBuffer");
}

TEST_F(EditorGpuMemoryNameOverlayTest, UnsetHandleReturnsEmptyString)
{
    GpuMemoryTracker tracker;
    EditorGpuMemoryNameOverlay::Install(tracker);

    const GpuResourceHandle handle = tracker.Track(GpuResourceType::Buffer, GpuMemoryLocation::GpuOnly, 128);

    EXPECT_TRUE(EditorGpuMemoryNameOverlay::GetDebugName(handle).empty());
}

TEST_F(EditorGpuMemoryNameOverlayTest, InvalidHandleReturnsEmptyStringWithoutCrashing)
{
    EXPECT_TRUE(EditorGpuMemoryNameOverlay::GetDebugName(kInvalidGpuResourceHandle).empty());
}

TEST_F(EditorGpuMemoryNameOverlayTest, IsForgottenAfterUntrack)
{
    GpuMemoryTracker tracker;
    EditorGpuMemoryNameOverlay::Install(tracker);

    const GpuResourceHandle handle = tracker.Track(GpuResourceType::Buffer, GpuMemoryLocation::GpuOnly, 128);
    tracker.SetDebugName(handle, "Temp");

    tracker.Untrack(handle);

    EXPECT_TRUE(EditorGpuMemoryNameOverlay::GetDebugName(handle).empty());
}

TEST_F(EditorGpuMemoryNameOverlayTest, SetDebugNameOnATrackerThatWasNeverInstalledIsHarmlessNoop)
{
    GpuMemoryTracker tracker; // Install() never called on this instance.
    const GpuResourceHandle handle = tracker.Track(GpuResourceType::Buffer, GpuMemoryLocation::GpuOnly, 128);

    tracker.SetDebugName(handle, "ShouldGoNowhere"); // must not crash

    EXPECT_TRUE(EditorGpuMemoryNameOverlay::GetDebugName(handle).empty());
}

TEST_F(EditorGpuMemoryNameOverlayTest, DistinctHandlesGetDistinctNames)
{
    GpuMemoryTracker tracker;
    EditorGpuMemoryNameOverlay::Install(tracker);

    const GpuResourceHandle first = tracker.Track(GpuResourceType::Buffer, GpuMemoryLocation::GpuOnly, 64);
    const GpuResourceHandle second = tracker.Track(GpuResourceType::Texture, GpuMemoryLocation::Shared, 128);
    tracker.SetDebugName(first, "First");
    tracker.SetDebugName(second, "Second");

    EXPECT_EQ(EditorGpuMemoryNameOverlay::GetDebugName(first), "First");
    EXPECT_EQ(EditorGpuMemoryNameOverlay::GetDebugName(second), "Second");
}

} // namespace
} // namespace gte
