// Unit tests for ImGuiMemoryTracker (src/Editor/ImGuiMemoryTracker.h) -
// installs a tracking allocator behind Dear ImGui's own
// MemAlloc()/MemFree() (see ImGui::SetAllocatorFunctions(), imgui.h) and
// exercises it directly - neither MemAlloc()/MemFree() nor Install() need a
// live ImGui context/window/Vulkan device, so this is genuinely Tier 1
// despite living under src/Editor/ (same reasoning as
// tests/Editor/EditorCameraTests.cpp). Only built when GTE_ENABLE_EDITOR is
// ON, since ImGuiMemoryTracker itself is only compiled into gte_core then.
//
// Install() is process-global and idempotent - every test captures
// LiveBytes()/LiveAllocationCount() BEFORE its own MemAlloc/MemFree calls
// and asserts on the DELTA, so test order can never make these flaky.

#include "Editor/ImGuiMemoryTracker.h"

#include <imgui.h>

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(ImGuiMemoryTrackerTest, MemAllocIncreasesLiveBytesAndCount_MemFreeUndoesIt)
{
    ImGuiMemoryTracker::Install();

    const std::uint64_t bytesBefore = ImGuiMemoryTracker::LiveBytes();
    const std::uint64_t countBefore = ImGuiMemoryTracker::LiveAllocationCount();

    void* block = ImGui::MemAlloc(128);
    ASSERT_NE(block, nullptr);

    EXPECT_EQ(ImGuiMemoryTracker::LiveBytes(), bytesBefore + 128);
    EXPECT_EQ(ImGuiMemoryTracker::LiveAllocationCount(), countBefore + 1);

    ImGui::MemFree(block);

    EXPECT_EQ(ImGuiMemoryTracker::LiveBytes(), bytesBefore);
    EXPECT_EQ(ImGuiMemoryTracker::LiveAllocationCount(), countBefore);
}

TEST(ImGuiMemoryTrackerTest, MultipleOutstandingAllocationsAggregateCorrectly)
{
    ImGuiMemoryTracker::Install();

    const std::uint64_t bytesBefore = ImGuiMemoryTracker::LiveBytes();
    const std::uint64_t countBefore = ImGuiMemoryTracker::LiveAllocationCount();

    void* a = ImGui::MemAlloc(10);
    void* b = ImGui::MemAlloc(20);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    EXPECT_EQ(ImGuiMemoryTracker::LiveBytes(), bytesBefore + 30);
    EXPECT_EQ(ImGuiMemoryTracker::LiveAllocationCount(), countBefore + 2);

    ImGui::MemFree(a);
    ImGui::MemFree(b);

    EXPECT_EQ(ImGuiMemoryTracker::LiveBytes(), bytesBefore);
    EXPECT_EQ(ImGuiMemoryTracker::LiveAllocationCount(), countBefore);
}

TEST(ImGuiMemoryTrackerTest, FreeOfNullIsHarmlessNoop)
{
    ImGuiMemoryTracker::Install();

    const std::uint64_t bytesBefore = ImGuiMemoryTracker::LiveBytes();
    const std::uint64_t countBefore = ImGuiMemoryTracker::LiveAllocationCount();

    ImGui::MemFree(nullptr);

    EXPECT_EQ(ImGuiMemoryTracker::LiveBytes(), bytesBefore);
    EXPECT_EQ(ImGuiMemoryTracker::LiveAllocationCount(), countBefore);
}

TEST(ImGuiMemoryTrackerTest, InstallIsIdempotent)
{
    ImGuiMemoryTracker::Install();
    ImGuiMemoryTracker::Install();

    const std::uint64_t bytesBefore = ImGuiMemoryTracker::LiveBytes();
    void* block = ImGui::MemAlloc(16);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(ImGuiMemoryTracker::LiveBytes(), bytesBefore + 16);
    ImGui::MemFree(block);
    EXPECT_EQ(ImGuiMemoryTracker::LiveBytes(), bytesBefore);
}

} // namespace
} // namespace gte
