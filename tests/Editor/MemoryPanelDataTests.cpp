// Unit tests for the Editor's "Memory" panel data-shaping logic
// (src/Editor/MemoryPanelData.h) - BuildMemoryRows()/FormatBytes()/
// ToString() are deliberately pure (no ImGui/Renderer/live-Vulkan-device
// knowledge at all), so they're Tier-1-testable exactly like EditorCamera
// (src/Editor/EditorCamera.h) despite living under src/Editor/ - see
// AGENTS.md, "Testability & Regression Safety". Only built when
// GTE_ENABLE_EDITOR is ON, since MemoryPanelData.h/.cpp are only compiled
// into gte_core then (see the root CMakeLists.txt's "Editor Module
// Structure") - the same "zero-touch when off" rule already applied to
// tests/Editor/EditorCameraTests.cpp.

#include "Editor/MemoryPanelData.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(MemoryPanelDataTest, BuildMemoryRows_EmptyInputProducesEmptyOutput)
{
    const std::vector<GpuMemoryTracker::Entry> entries;
    const std::vector<MemoryRow> rows =
        BuildMemoryRows(entries, [](GpuResourceHandle) { return std::string(); });

    EXPECT_TRUE(rows.empty());
}

TEST(MemoryPanelDataTest, BuildMemoryRows_CopiesTypeLocationAndSizeFromEachEntry)
{
    std::vector<GpuMemoryTracker::Entry> entries;
    entries.push_back(GpuMemoryTracker::Entry{
        GpuResourceHandle{ 0, 1 }, GpuResourceRecord{ GpuResourceType::Buffer, GpuMemoryLocation::GpuOnly, 1024 } });

    const std::vector<MemoryRow> rows =
        BuildMemoryRows(entries, [](GpuResourceHandle) { return std::string("VertexBuffer"); });

    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].handle, entries[0].handle);
    EXPECT_EQ(rows[0].name, "VertexBuffer");
    EXPECT_EQ(rows[0].type, GpuResourceType::Buffer);
    EXPECT_EQ(rows[0].location, GpuMemoryLocation::GpuOnly);
    EXPECT_EQ(rows[0].sizeBytes, 1024u);
}

TEST(MemoryPanelDataTest, BuildMemoryRows_SortsBySizeDescending)
{
    std::vector<GpuMemoryTracker::Entry> entries;
    entries.push_back(GpuMemoryTracker::Entry{
        GpuResourceHandle{ 0, 1 }, GpuResourceRecord{ GpuResourceType::Buffer, GpuMemoryLocation::GpuOnly, 100 } });
    entries.push_back(GpuMemoryTracker::Entry{
        GpuResourceHandle{ 1, 1 }, GpuResourceRecord{ GpuResourceType::Texture, GpuMemoryLocation::Shared, 5000 } });
    entries.push_back(GpuMemoryTracker::Entry{
        GpuResourceHandle{ 2, 1 }, GpuResourceRecord{ GpuResourceType::Buffer, GpuMemoryLocation::CpuOnly, 900 } });

    const std::vector<MemoryRow> rows =
        BuildMemoryRows(entries, [](GpuResourceHandle) { return std::string(); });

    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0].sizeBytes, 5000u);
    EXPECT_EQ(rows[1].sizeBytes, 900u);
    EXPECT_EQ(rows[2].sizeBytes, 100u);
}

TEST(MemoryPanelDataTest, BuildMemoryRows_TiesBrokenByHandleIndexAscending)
{
    std::vector<GpuMemoryTracker::Entry> entries;
    entries.push_back(GpuMemoryTracker::Entry{
        GpuResourceHandle{ 5, 1 }, GpuResourceRecord{ GpuResourceType::Buffer, GpuMemoryLocation::GpuOnly, 256 } });
    entries.push_back(GpuMemoryTracker::Entry{
        GpuResourceHandle{ 2, 1 }, GpuResourceRecord{ GpuResourceType::Buffer, GpuMemoryLocation::GpuOnly, 256 } });

    const std::vector<MemoryRow> rows =
        BuildMemoryRows(entries, [](GpuResourceHandle) { return std::string(); });

    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].handle.index, 2u);
    EXPECT_EQ(rows[1].handle.index, 5u);
}

TEST(MemoryPanelDataTest, BuildMemoryRows_UsesWhateverNameLookupReturnsPerHandle)
{
    std::vector<GpuMemoryTracker::Entry> entries;
    entries.push_back(GpuMemoryTracker::Entry{
        GpuResourceHandle{ 0, 1 }, GpuResourceRecord{ GpuResourceType::Buffer, GpuMemoryLocation::GpuOnly, 10 } });
    entries.push_back(GpuMemoryTracker::Entry{
        GpuResourceHandle{ 1, 1 }, GpuResourceRecord{ GpuResourceType::Buffer, GpuMemoryLocation::GpuOnly, 20 } });

    const std::vector<MemoryRow> rows = BuildMemoryRows(entries, [](GpuResourceHandle handle) {
        return handle.index == 0 ? std::string("First") : std::string("Second");
    });

    ASSERT_EQ(rows.size(), 2u);
    // Sorted by size descending, so index 1 ("Second", 20 bytes) comes first.
    EXPECT_EQ(rows[0].name, "Second");
    EXPECT_EQ(rows[1].name, "First");
}

TEST(MemoryPanelDataTest, FormatBytes_SubKilobyteUsesPlainBytes)
{
    EXPECT_EQ(FormatBytes(0), "0 B");
    EXPECT_EQ(FormatBytes(512), "512 B");
    EXPECT_EQ(FormatBytes(1023), "1023 B");
}

TEST(MemoryPanelDataTest, FormatBytes_KilobyteRange)
{
    EXPECT_EQ(FormatBytes(1024), "1.00 KB");
    EXPECT_EQ(FormatBytes(1536), "1.50 KB");
}

TEST(MemoryPanelDataTest, FormatBytes_MegabyteAndGigabyteRange)
{
    EXPECT_EQ(FormatBytes(1024ull * 1024ull), "1.00 MB");
    EXPECT_EQ(FormatBytes(1024ull * 1024ull * 1024ull), "1.00 GB");
    EXPECT_EQ(FormatBytes(1024ull * 1024ull * 1024ull * 2ull), "2.00 GB");
}

TEST(MemoryPanelDataTest, ToString_ResourceTypeAndLocationAreNonNullAndDistinct)
{
    EXPECT_STREQ(ToString(GpuResourceType::Buffer), "Buffer");
    EXPECT_STREQ(ToString(GpuResourceType::Texture), "Texture");

    EXPECT_STRNE(ToString(GpuMemoryLocation::GpuOnly), ToString(GpuMemoryLocation::CpuOnly));
    EXPECT_STRNE(ToString(GpuMemoryLocation::CpuOnly), ToString(GpuMemoryLocation::Shared));
}

TEST(MemoryPanelDataTest, BuildHeapBudgetRows_EmptyInputProducesEmptyOutput)
{
    const std::vector<VmaBudget> budgets;
    const std::vector<HeapBudgetRow> rows = BuildHeapBudgetRows(budgets);

    EXPECT_TRUE(rows.empty());
}

TEST(MemoryPanelDataTest, BuildHeapBudgetRows_CopiesFieldsAndAssignsHeapIndexByPosition)
{
    std::vector<VmaBudget> budgets;
    budgets.push_back(VmaBudget{ VmaStatistics{ 1, 3, 1000, 400 }, 1200, 5000 });
    budgets.push_back(VmaBudget{ VmaStatistics{ 2, 7, 2000, 900 }, 2500, 6000 });

    const std::vector<HeapBudgetRow> rows = BuildHeapBudgetRows(budgets);

    ASSERT_EQ(rows.size(), 2u);

    EXPECT_EQ(rows[0].heapIndex, 0u);
    EXPECT_EQ(rows[0].vmaBlockBytes, 1000u);
    EXPECT_EQ(rows[0].vmaBlockCount, 1u);
    EXPECT_EQ(rows[0].vmaAllocationCount, 3u);
    EXPECT_EQ(rows[0].usageBytes, 1200u);
    EXPECT_EQ(rows[0].budgetBytes, 5000u);

    EXPECT_EQ(rows[1].heapIndex, 1u);
    EXPECT_EQ(rows[1].vmaBlockBytes, 2000u);
    EXPECT_EQ(rows[1].vmaBlockCount, 2u);
    EXPECT_EQ(rows[1].vmaAllocationCount, 7u);
    EXPECT_EQ(rows[1].usageBytes, 2500u);
    EXPECT_EQ(rows[1].budgetBytes, 6000u);
}

TEST(MemoryPanelDataTest, FormatBlockSummary_SingularWordingForOneBlockAndOneAllocation)
{
    EXPECT_EQ(FormatBlockSummary(1024, 1, 1), "1.00 KB across 1 block (1 sub-allocation)");
}

TEST(MemoryPanelDataTest, FormatBlockSummary_PluralWordingForMultipleBlocksAndAllocations)
{
    EXPECT_EQ(FormatBlockSummary(2048, 2, 5), "2.00 KB across 2 blocks (5 sub-allocations)");
}

TEST(MemoryPanelDataTest, FormatBlockSummary_ZeroBlocksAndAllocationsUsesPluralWording)
{
    EXPECT_EQ(FormatBlockSummary(0, 0, 0), "0 B across 0 blocks (0 sub-allocations)");
}

} // namespace
} // namespace gte
