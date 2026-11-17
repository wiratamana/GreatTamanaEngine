// Tier 1: GPU node pool layout, absolute index pattern and slot bookkeeping (no device needed).

#include "Renderer/OffscreenRingGuard.h"
#include "Features/Terrain/Gpu/TerrainPoolLayout.h"
#include "Features/Terrain/Gpu/TerrainSlotAllocator.h"
#include "Features/Terrain/TerrainTypes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>

namespace gte::terrain::gpu {

namespace {

SlotFrame Sync() { return SlotFrame{}; }
SlotFrame Async(std::uint64_t serial, std::uint64_t completed) { return SlotFrame{ serial, completed }; }

} // namespace

TEST(GpuPoolLayoutTests, CountsMatchTheNodeGrid)
{
    const std::optional<PoolLayout> layout = MakePoolLayout(64, 768);
    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(layout->vertsPerSlot, 4485u);
    EXPECT_EQ(layout->indicesPerSlot, 26112u);
    EXPECT_EQ(layout->TotalVertices(), 768ull * 4485ull);
    EXPECT_EQ(layout->VertexBytes(), 768ull * 4485ull * 32ull);
    EXPECT_EQ(layout->IndexBytes(), 768ull * 26112ull * 4ull);
    EXPECT_EQ(layout->SlotVertexOffsetBytes(3), 3ull * 4485ull * 32ull);
    EXPECT_EQ(layout->SlotIndexOffsetBytes(3), 3ull * 26112ull * 4ull);
}

TEST(GpuPoolLayoutTests, RefusesOutOfRangeAndOverflowingPools)
{
    std::string why;
    EXPECT_FALSE(MakePoolLayout(0, 10, &why).has_value());
    EXPECT_FALSE(MakePoolLayout(64, 0, &why).has_value());
    EXPECT_FALSE(MakePoolLayout(64, kPoolMaxSlots + 1, &why).has_value());
    // 512 quads with 8192 slots needs an index buffer far above 4 GiB.
    EXPECT_FALSE(MakePoolLayout(512, kPoolMaxSlots, &why).has_value());
    EXPECT_FALSE(why.empty());
    EXPECT_TRUE(MakePoolLayout(32, 1536).has_value());
}

TEST(GpuPoolLayoutTests, SlotIndicesAreAbsoluteAndStayInsideTheirSlot)
{
    constexpr int kQuads = 16;
    const std::vector<std::uint32_t> local = BuildSharedIndices(kQuads);
    const std::optional<PoolLayout> layout = MakePoolLayout(kQuads, 5);
    ASSERT_TRUE(layout.has_value());
    ASSERT_EQ(local.size(), layout->indicesPerSlot);

    const std::vector<std::uint32_t> all = BuildPoolIndices(*layout, local);
    ASSERT_EQ(all.size(), layout->TotalIndices());
    for (std::uint32_t slot = 0; slot < layout->slotCount; ++slot) {
        const std::uint32_t lo = slot * layout->vertsPerSlot;
        const std::uint32_t hi = lo + layout->vertsPerSlot;
        for (std::uint32_t i = 0; i < layout->indicesPerSlot; ++i) {
            const std::uint32_t value = all[static_cast<std::size_t>(slot) * layout->indicesPerSlot + i];
            ASSERT_GE(value, lo);
            ASSERT_LT(value, hi);
            ASSERT_EQ(value - lo, local[i]);
        }
    }
}

TEST(GpuPoolLayoutTests, WriteSlotIndicesClampsToTheShorterSide)
{
    const std::vector<std::uint32_t> local{ 1, 2, 3, 4 };
    std::vector<std::uint32_t> out(3, 99);
    WriteSlotIndices(local, 100, out);
    EXPECT_EQ(out, (std::vector<std::uint32_t>{ 101, 102, 103 }));
}

TEST(GpuPoolLayoutTests, LargestIndexStaysBelowTotalVertices)
{
    const std::optional<PoolLayout> layout = MakePoolLayout(64, 8);
    ASSERT_TRUE(layout.has_value());
    const std::vector<std::uint32_t> local = BuildSharedIndices(64);
    const std::vector<std::uint32_t> all = BuildPoolIndices(*layout, local);
    EXPECT_LT(*std::max_element(all.begin(), all.end()), layout->TotalVertices());
}

TEST(GpuSlotAllocatorTests, HandsOutTheLowestFreeSlotAndRunsOut)
{
    SlotAllocator a(3);
    EXPECT_EQ(a.Acquire(Sync()), 0u);
    EXPECT_EQ(a.Acquire(Sync()), 1u);
    EXPECT_EQ(a.Acquire(Sync()), 2u);
    EXPECT_FALSE(a.Acquire(Sync()).has_value());
    EXPECT_EQ(a.UsedCount(), 3u);
    EXPECT_EQ(a.FreeCount(), 0u);
}

TEST(GpuSlotAllocatorTests, SynchronousReleaseFreesAtOnceAndKeepsLowestFirst)
{
    SlotAllocator a(4);
    for (int i = 0; i < 4; ++i) {
        ASSERT_TRUE(a.Acquire(Sync()).has_value());
    }
    EXPECT_TRUE(a.Release(2, Sync()));
    EXPECT_TRUE(a.Release(0, Sync()));
    EXPECT_EQ(a.Acquire(Sync()), 0u);
    EXPECT_EQ(a.Acquire(Sync()), 2u);
}

TEST(GpuSlotAllocatorTests, ReleaseOfAFreeOrRetiringSlotIsRefused)
{
    SlotAllocator a(2);
    EXPECT_FALSE(a.Release(0, Sync()));
    ASSERT_TRUE(a.Acquire(Async(5, 4)).has_value());
    EXPECT_TRUE(a.Release(0, Async(5, 4)));
    EXPECT_FALSE(a.Release(0, Async(5, 4)));
    EXPECT_FALSE(a.Release(99, Sync()));
}

TEST(GpuSlotAllocatorTests, AsyncSlotIsReusedOnlyAfterTheRetiringFrameFinished)
{
    SlotAllocator a(1);
    ASSERT_EQ(a.Acquire(Async(4, 3)), 0u);
    ASSERT_TRUE(a.Release(0, Async(5, 4)));
    EXPECT_EQ(a.State(0), SlotState::Retiring);

    EXPECT_FALSE(a.Acquire(Async(5, 4)).has_value()) << "frame 5 is the first one without the slot; 4 may still read it";
    EXPECT_FALSE(a.Acquire(Async(6, 4)).has_value());
    EXPECT_EQ(a.Acquire(Async(6, 5)), 0u);
}

TEST(GpuSlotAllocatorTests, ReuseRuleIsNeverLooserThanARingCopy)
{
    // The ring rule frees a copy last used by the same frame; a retired slot must wait for it to finish.
    for (std::uint64_t serial = 1; serial < 6; ++serial) {
        for (std::uint64_t completed = 0; completed <= serial; ++completed) {
            for (std::uint64_t retired = 1; retired <= serial; ++retired) {
                const SlotFrame frame{ serial, completed };
                if (SlotReusable(frame, retired)) {
                    EXPECT_TRUE(RingCopyWritable(OffscreenFrameTicket{ 0, serial, completed }, retired));
                }
            }
        }
    }
    EXPECT_TRUE(SlotReusable(Sync(), 123));
}

TEST(GpuSlotAllocatorTests, SlotsAreWrittenBeforeTheyAreMarkedAndKeepAGeneration)
{
    SlotAllocator a(2);
    const auto slot = a.Acquire(Sync());
    ASSERT_TRUE(slot.has_value());
    EXPECT_EQ(a.State(*slot), SlotState::Reserved);
    const std::uint32_t generation = a.Generation(*slot);
    EXPECT_TRUE(a.MarkWritten(*slot));
    EXPECT_EQ(a.State(*slot), SlotState::Written);
    EXPECT_FALSE(a.MarkWritten(*slot)) << "only Reserved can become Written";
    EXPECT_FALSE(a.MarkWritten(99));

    ASSERT_TRUE(a.Release(*slot, Sync()));
    const auto again = a.Acquire(Sync());
    ASSERT_EQ(again, slot);
    EXPECT_NE(a.Generation(*again), generation);
}

TEST(GpuSlotAllocatorTests, AbandonReturnsOnlyAReservedSlot)
{
    SlotAllocator a(1);
    const auto slot = a.Acquire(Async(3, 2));
    ASSERT_TRUE(slot.has_value());
    EXPECT_TRUE(a.Abandon(*slot));
    EXPECT_EQ(a.State(*slot), SlotState::Free);
    EXPECT_EQ(a.Acquire(Async(3, 2)), slot);
    ASSERT_TRUE(a.MarkWritten(*slot));
    EXPECT_FALSE(a.Abandon(*slot));
}

TEST(GpuSlotAllocatorTests, AccountingAddsUpThroughAMixedSequence)
{
    constexpr std::uint32_t kCapacity = 16;
    SlotAllocator a(kCapacity);
    std::set<std::uint32_t> held;
    std::uint64_t serial = 1;
    for (int step = 0; step < 400; ++step) {
        const SlotFrame frame = Async(serial, serial > 1 ? serial - 1 : 0);
        if (step % 3 != 2) {
            if (const auto slot = a.Acquire(frame)) {
                EXPECT_TRUE(held.insert(*slot).second) << "a slot was handed out twice";
                a.MarkWritten(*slot);
            }
        } else if (!held.empty()) {
            const std::uint32_t slot = *held.begin();
            held.erase(held.begin());
            EXPECT_TRUE(a.Release(slot, frame));
        }
        ++serial;
        EXPECT_EQ(a.UsedCount(), held.size());
        EXPECT_EQ(a.UsedCount() + a.RetiringCount() + a.FreeCount(), kCapacity);
    }
}

} // namespace gte::terrain::gpu
