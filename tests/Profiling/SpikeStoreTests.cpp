// Tier 1: spike rule, classification, eviction, pin limit and late GPU reports. No clock, no GPU.
#include "Profiling/NameTable.h"
#include "Profiling/SpikeDetail.h"
#include "Profiling/SpikeStore.h"

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <vector>

namespace gte::Profiling {
namespace {

constexpr std::uint64_t Ms(std::uint64_t value)
{
    return value * 1'000'000ull;
}

// A frozen frame is large: build records on the heap.
std::unique_ptr<FrameRecord> MakeFrame(std::uint64_t index, std::uint64_t frameMs)
{
    auto record = std::make_unique<FrameRecord>();
    record->summary.frameIndex = index;
    record->summary.startTicks = 0;
    record->summary.endTicks = Ms(frameMs);
    record->summary.cpuMilliseconds = static_cast<double>(frameMs);
    return record;
}

void AddScope(FrameRecord& record, const char* name, std::uint64_t startMs, std::uint64_t endMs, std::uint16_t depth,
    ScopeKind kind = ScopeKind::Work)
{
    record.scopes.Append(ScopeEvent{ Ms(startMs), Ms(endMs), InternName(name), depth, kind, ViewSlot::Shared });
}

std::unique_ptr<SpikeStore> MakeStore()
{
    return std::make_unique<SpikeStore>();
}

bool Freeze(SpikeStore& store, std::uint64_t index)
{
    const auto record = MakeFrame(index, 50);
    AddScope(*record, "SpikeStoreTestBody", 0, 50, 0);
    return store.Freeze(*record, 16.0, ClassifySpike(*record, 16.0));
}

TEST(SpikeRuleTest, AFrameIsASpikeAboveTwiceTheMedian)
{
    EXPECT_FALSE(IsSpikeFrame(30.0, 16.0, 0.0));
    EXPECT_FALSE(IsSpikeFrame(32.0, 16.0, 0.0)); // exactly 2x is not above
    EXPECT_TRUE(IsSpikeFrame(32.5, 16.0, 0.0));
    EXPECT_FALSE(IsSpikeFrame(100.0, 0.0, 0.0)); // no baseline, no spike
}

TEST(SpikeRuleTest, AUserThresholdReplacesTheMedianRule)
{
    EXPECT_TRUE(IsSpikeFrame(21.0, 16.0, 20.0));
    EXPECT_FALSE(IsSpikeFrame(19.0, 16.0, 20.0));
    EXPECT_TRUE(IsSpikeFrame(21.0, 0.0, 20.0));
}

TEST(SpikeRuleTest, TheMedianOfRecentFrames)
{
    const std::array<double, 5> values{ 16, 17, 100, 16, 18 };
    EXPECT_DOUBLE_EQ(MedianOf(values), 17.0);
    EXPECT_DOUBLE_EQ(MedianOf({}), 0.0);
}

TEST(SpikeClassifyTest, ADominantScopeExplainsTheExcess)
{
    const auto record = MakeFrame(1, 50);
    AddScope(*record, "SpikeStoreTestWait", 5, 45, 1, ScopeKind::Blocked);
    AddScope(*record, "SpikeStoreTestFrame", 0, 50, 0);

    const SpikeClassification c = ClassifySpike(*record, 16.0);
    EXPECT_EQ(c.kind, SpikeClass::Explained);
    EXPECT_STREQ(ResolveName(c.dominant), "SpikeStoreTestWait");
    EXPECT_DOUBLE_EQ(c.dominantSelfMs, 40.0);
    EXPECT_DOUBLE_EQ(c.excessMs, 34.0);
    EXPECT_FALSE(c.markerPresent);
}

TEST(SpikeClassifyTest, AMarkerExplainsAFrameEvenWithoutADominantScope)
{
    const auto record = MakeFrame(1, 50);
    for (std::uint64_t i = 0; i < 10; ++i) {
        AddScope(*record, "SpikeStoreTestSmall", i * 4, i * 4 + 4, 0);
    }
    AddScope(*record, "SpikeStoreTestMarker", 20, 20, 0, ScopeKind::Marker);

    const SpikeClassification c = ClassifySpike(*record, 16.0);
    EXPECT_TRUE(c.markerPresent);
    EXPECT_EQ(c.kind, SpikeClass::Explained);
}

TEST(SpikeClassifyTest, ManySmallScopesAndNoMarkerStayUnexplained)
{
    const auto record = MakeFrame(1, 50);
    for (std::uint64_t i = 0; i < 10; ++i) {
        AddScope(*record, "SpikeStoreTestSmall", i * 4, i * 4 + 4, 0);
    }
    const SpikeClassification c = ClassifySpike(*record, 16.0);
    EXPECT_EQ(c.kind, SpikeClass::Unexplained);
    EXPECT_DOUBLE_EQ(c.dominantSelfMs, 4.0);
}

TEST(SpikeClassifyTest, AnEmptyFrameIsUnexplained)
{
    const auto record = MakeFrame(1, 50);
    EXPECT_EQ(ClassifySpike(*record, 16.0).kind, SpikeClass::Unexplained);
}

TEST(SpikeStoreTest, TheSeventeenthSpikeEvictsTheOldestUnpinnedOne)
{
    const auto store = MakeStore();
    for (std::uint64_t i = 1; i <= SpikeStore::kSlots; ++i) {
        ASSERT_TRUE(Freeze(*store, i));
    }
    EXPECT_EQ(store->Count(), SpikeStore::kSlots);

    ASSERT_TRUE(Freeze(*store, 100));
    EXPECT_EQ(store->Count(), SpikeStore::kSlots);
    EXPECT_FALSE(BuildSpikeDetail(*store, 1).has_value()); // the oldest is gone
    EXPECT_TRUE(BuildSpikeDetail(*store, 2).has_value());
    EXPECT_TRUE(BuildSpikeDetail(*store, 100).has_value());
    EXPECT_EQ(store->TotalFrozen(), SpikeStore::kSlots + 1);
}

TEST(SpikeStoreTest, APinnedSpikeSurvivesEviction)
{
    const auto store = MakeStore();
    for (std::uint64_t i = 1; i <= SpikeStore::kSlots; ++i) {
        ASSERT_TRUE(Freeze(*store, i));
    }
    ASSERT_EQ(store->Pin(1), PinResult::Ok);

    ASSERT_TRUE(Freeze(*store, 100));
    EXPECT_TRUE(BuildSpikeDetail(*store, 1).has_value());
    EXPECT_FALSE(BuildSpikeDetail(*store, 2).has_value()); // the oldest unpinned went instead
}

TEST(SpikeStoreTest, TheNinthPinIsRefusedAndAFreeSlotAlwaysExists)
{
    const auto store = MakeStore();
    for (std::uint64_t i = 1; i <= SpikeStore::kSlots; ++i) {
        ASSERT_TRUE(Freeze(*store, i));
    }
    for (std::uint64_t i = 1; i <= kMaxPinnedSpikes; ++i) {
        EXPECT_EQ(store->Pin(i), PinResult::Ok);
    }
    EXPECT_EQ(store->PinnedCount(), kMaxPinnedSpikes);
    EXPECT_EQ(store->Pin(kMaxPinnedSpikes + 1), PinResult::LimitReached);
    EXPECT_EQ(store->Pin(1), PinResult::Ok); // pinning twice changes nothing

    for (std::uint64_t i = 200; i < 230; ++i) {
        ASSERT_TRUE(Freeze(*store, i)) << i; // pinned frames never block a new spike
    }
    EXPECT_EQ(store->PinnedCount(), kMaxPinnedSpikes);
    EXPECT_TRUE(BuildSpikeDetail(*store, 1).has_value());
}

TEST(SpikeStoreTest, UnpinFreesAPinSlotAndUnknownFramesAreRefused)
{
    const auto store = MakeStore();
    ASSERT_TRUE(Freeze(*store, 5));
    EXPECT_EQ(store->Pin(99), PinResult::NotFound);
    ASSERT_EQ(store->Pin(5), PinResult::Ok);
    EXPECT_TRUE(store->Unpin(5));
    EXPECT_EQ(store->PinnedCount(), 0u);
    EXPECT_FALSE(store->Unpin(99));
}

TEST(SpikeStoreTest, ALateGpuReportAttachesOnlyWithinTwoFrames)
{
    const auto store = MakeStore();
    ASSERT_TRUE(Freeze(*store, 10));

    GpuFrameMetrics gpu;
    gpu.span = { 9.0f, SampleStatus::Present, 0 };
    EXPECT_TRUE(store->AttachGpu(10, gpu, 12));
    EXPECT_FALSE(store->AttachGpu(10, gpu, 13)); // too late
    EXPECT_FALSE(store->AttachGpu(77, gpu, 12)); // unknown frame

    const auto detail = BuildSpikeDetail(*store, 10);
    ASSERT_TRUE(detail.has_value());
    EXPECT_TRUE(detail->gpuAttached);
    EXPECT_EQ(detail->gpu.span.status, SampleStatus::Present);
    EXPECT_FLOAT_EQ(detail->gpu.span.milliseconds, 9.0f);
}

TEST(SpikeStoreTest, RowsComeNewestFirstWithTheirClassification)
{
    const auto store = MakeStore();
    for (std::uint64_t i = 1; i <= 3; ++i) {
        ASSERT_TRUE(Freeze(*store, i));
    }
    ASSERT_EQ(store->Pin(2), PinResult::Ok);

    std::array<SpikeStore::Row, SpikeStore::kSlots> rows{};
    ASSERT_EQ(store->Rows(rows), 3u);
    EXPECT_EQ(rows[0].frameIndex, 3u);
    EXPECT_EQ(rows[1].frameIndex, 2u);
    EXPECT_EQ(rows[2].frameIndex, 1u);
    EXPECT_TRUE(rows[1].pinned);
    EXPECT_FALSE(rows[0].pinned);
    EXPECT_EQ(rows[0].classification.kind, SpikeClass::Explained);
    EXPECT_DOUBLE_EQ(rows[0].frameMs, 50.0);
}

TEST(SpikeStoreTest, ClearForgetsEverything)
{
    const auto store = MakeStore();
    ASSERT_TRUE(Freeze(*store, 1));
    ASSERT_EQ(store->Pin(1), PinResult::Ok);
    store->Clear();
    EXPECT_EQ(store->Count(), 0u);
    EXPECT_EQ(store->PinnedCount(), 0u);
    EXPECT_EQ(store->TotalFrozen(), 0u);
}

TEST(SpikeDetailTest, ScopesComeInTimeOrderWithParentsFirst)
{
    const auto store = MakeStore();
    const auto record = MakeFrame(7, 50);
    // Post-order: children first.
    AddScope(*record, "SpikeStoreTestChild", 10, 20, 1);
    AddScope(*record, "SpikeStoreTestParent", 10, 25, 0);
    AddScope(*record, "SpikeStoreTestLate", 30, 40, 0);
    ASSERT_TRUE(store->Freeze(*record, 16.0, ClassifySpike(*record, 16.0)));

    const auto detail = BuildSpikeDetail(*store, 7);
    ASSERT_TRUE(detail.has_value());
    ASSERT_EQ(detail->scopes.size(), 3u);
    EXPECT_EQ(detail->scopes[0].name, "SpikeStoreTestParent");
    EXPECT_EQ(detail->scopes[1].name, "SpikeStoreTestChild");
    EXPECT_EQ(detail->scopes[2].name, "SpikeStoreTestLate");
    EXPECT_DOUBLE_EQ(detail->scopes[0].selfMs, 5.0);
    EXPECT_DOUBLE_EQ(detail->scopes[1].startMs, 10.0);
    EXPECT_DOUBLE_EQ(detail->medianMs, 16.0);
}

} // namespace
} // namespace gte::Profiling
