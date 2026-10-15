// Tier-1 tests for the descriptor pool growth policy.

#include "Renderer/Vulkan/DescriptorPoolGrowth.h"

#include <gtest/gtest.h>

#include <limits>

namespace gte {
namespace {

constexpr PoolCapacity kFirst{ 256, 256, 128, 128, 64 };

TEST(DescriptorPoolGrowthTest, NextPoolDoublesEveryField)
{
    const auto next = NextPoolCapacity(kFirst, 1);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(*next, (PoolCapacity{ 512, 512, 256, 256, 128 }));
}

TEST(DescriptorPoolGrowthTest, GrowthStopsAtThePoolCap)
{
    EXPECT_TRUE(NextPoolCapacity(kFirst, kMaxDescriptorPools - 1).has_value());
    EXPECT_FALSE(NextPoolCapacity(kFirst, kMaxDescriptorPools).has_value());
    EXPECT_FALSE(NextPoolCapacity(kFirst, kMaxDescriptorPools + 10).has_value());
}

TEST(DescriptorPoolGrowthTest, DoublingSaturatesInsteadOfWrapping)
{
    constexpr std::uint32_t kMax = std::numeric_limits<std::uint32_t>::max();
    const auto next = NextPoolCapacity(PoolCapacity{ kMax, kMax / 2 + 1, kMax, 1, 0 }, 1);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->sets, kMax);
    EXPECT_EQ(next->storageBuffers, kMax);
    EXPECT_EQ(next->storageImages, kMax);
    EXPECT_EQ(next->combinedSamplers, 2u);
    EXPECT_EQ(next->uniformBuffers, 0u);
}

TEST(DescriptorPoolGrowthTest, EveryAllowedPoolGrowsMonotonically)
{
    PoolCapacity cap = kFirst;
    for (std::size_t count = 1; count < kMaxDescriptorPools; ++count) {
        const auto next = NextPoolCapacity(cap, count);
        ASSERT_TRUE(next.has_value());
        EXPECT_GT(next->sets, cap.sets);
        cap = *next;
    }
}

TEST(DescriptorPoolGrowthTest, OneRequestAddsAtMostOnePool)
{
    EXPECT_TRUE(CanGrowPoolForRequest(0));
    EXPECT_FALSE(CanGrowPoolForRequest(1));
    EXPECT_FALSE(CanGrowPoolForRequest(7));
}

} // namespace
} // namespace gte
