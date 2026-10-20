#include "Renderer/UploadChunker.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace gte {
namespace {

using Bytes = std::vector<std::uint64_t>;

std::size_t TotalItems(const std::vector<UploadChunk>& chunks)
{
    std::size_t total = 0;
    for (const UploadChunk& chunk : chunks) {
        total += chunk.count;
    }
    return total;
}

TEST(UploadChunkerTest, NoItemsGivesNoChunks)
{
    EXPECT_TRUE(PlanUploadChunks(Bytes{}, 100, 8).empty());
}

TEST(UploadChunkerTest, OneItemIsOneChunk)
{
    const auto chunks = PlanUploadChunks(Bytes{ 40 }, 100, 8);
    ASSERT_EQ(chunks.size(), 1u);
    EXPECT_EQ(chunks[0].first, 0u);
    EXPECT_EQ(chunks[0].count, 1u);
    EXPECT_EQ(chunks[0].bytes, 40u);
}

TEST(UploadChunkerTest, OversizedItemGetsItsOwnChunk)
{
    const auto chunks = PlanUploadChunks(Bytes{ 10, 500, 10 }, 100, 8);
    ASSERT_EQ(chunks.size(), 3u);
    EXPECT_EQ(chunks[1].first, 1u);
    EXPECT_EQ(chunks[1].count, 1u);
    EXPECT_EQ(chunks[1].bytes, 500u);
    EXPECT_EQ(chunks[2].first, 2u);
}

TEST(UploadChunkerTest, ExactFitStaysInOneChunk)
{
    const auto chunks = PlanUploadChunks(Bytes{ 60, 40 }, 100, 8);
    ASSERT_EQ(chunks.size(), 1u);
    EXPECT_EQ(chunks[0].count, 2u);
    EXPECT_EQ(chunks[0].bytes, 100u);
}

TEST(UploadChunkerTest, OneByteOverStartsANewChunk)
{
    const auto chunks = PlanUploadChunks(Bytes{ 60, 41 }, 100, 8);
    ASSERT_EQ(chunks.size(), 2u);
    EXPECT_EQ(chunks[0].count, 1u);
    EXPECT_EQ(chunks[1].first, 1u);
    EXPECT_EQ(chunks[1].bytes, 41u);
}

TEST(UploadChunkerTest, ItemLimitSplitsManySmallItems)
{
    const auto chunks = PlanUploadChunks(Bytes(10, 1), 1000, 4);
    ASSERT_EQ(chunks.size(), 3u);
    EXPECT_EQ(chunks[0].count, 4u);
    EXPECT_EQ(chunks[1].count, 4u);
    EXPECT_EQ(chunks[2].count, 2u);
    EXPECT_EQ(chunks[2].first, 8u);
    EXPECT_EQ(TotalItems(chunks), 10u);
}

TEST(UploadChunkerTest, ZeroSizeItemsTakeNoBytes)
{
    const auto chunks = PlanUploadChunks(Bytes{ 0, 0, 100, 0 }, 100, 8);
    ASSERT_EQ(chunks.size(), 1u);
    EXPECT_EQ(chunks[0].count, 4u);
    EXPECT_EQ(chunks[0].bytes, 100u);
}

TEST(UploadChunkerTest, ChunksCoverEveryItemInOrder)
{
    const Bytes items{ 30, 80, 5, 5, 200, 1, 99, 2 };
    const auto chunks = PlanUploadChunks(items, 100, 3);
    std::size_t next = 0;
    for (const UploadChunk& chunk : chunks) {
        EXPECT_EQ(chunk.first, next);
        next += chunk.count;
    }
    EXPECT_EQ(next, items.size());
}

} // namespace
} // namespace gte
