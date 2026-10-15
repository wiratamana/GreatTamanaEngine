// Tier-1 tests for the descriptor pool growth policy.

#include "Renderer/Vulkan/DescriptorCost.h"
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

// A set that binds two storage buffers and one uniform buffer.
constexpr PoolCapacity kCost{ 1, 2, 0, 0, 1 };

TEST(DescriptorBudgetTest, AllocationAndFreeTrackUsedAndHighWater)
{
    DescriptorBudget budget;
    budget.AddCapacity(kFirst);
    budget.OnAllocated(kCost);
    budget.OnAllocated(kCost);
    EXPECT_EQ(budget.Used(), (PoolCapacity{ 2, 4, 0, 0, 2 }));

    budget.OnFreed(kCost);
    EXPECT_EQ(budget.Used(), kCost);
    EXPECT_EQ(budget.HighWater(), (PoolCapacity{ 2, 4, 0, 0, 2 }));
    EXPECT_EQ(budget.Capacity(), kFirst);
}

TEST(DescriptorBudgetTest, FreeingMoreThanAllocatedClampsAtZero)
{
    DescriptorBudget budget;
    budget.AddCapacity(kFirst);
    budget.OnFreed(kCost);
    EXPECT_EQ(budget.Used(), PoolCapacity{});
}

TEST(DescriptorBudgetTest, AddingAPoolAddsItsCapacity)
{
    DescriptorBudget budget;
    budget.AddCapacity(kFirst);
    budget.AddCapacity(*NextPoolCapacity(kFirst, 1));
    EXPECT_EQ(budget.Capacity(), (PoolCapacity{ 768, 768, 384, 384, 192 }));
}

TEST(DescriptorBudgetTest, PeakFractionIsTheFullestCounterAndIgnoresCountersWithoutCapacity)
{
    DescriptorBudget budget;
    budget.AddCapacity(PoolCapacity{ 100, 10, 0, 0, 0 });
    budget.OnAllocated(PoolCapacity{ 10, 5, 3, 0, 0 }); // Image use has no capacity: ignored.
    EXPECT_FLOAT_EQ(budget.PeakFraction(), 0.5f);
}

TEST(DescriptorBudgetTest, WarningFiresOncePerClimbAndRearmsBelowTheLowMark)
{
    DescriptorBudget budget;
    budget.AddCapacity(PoolCapacity{ 100, 100, 100, 100, 100 });

    budget.OnAllocated(PoolCapacity{ 70, 0, 0, 0, 0 });
    EXPECT_FALSE(budget.TakeWarning()); // 70% is not above 75%.

    budget.OnAllocated(PoolCapacity{ 10, 0, 0, 0, 0 });
    EXPECT_TRUE(budget.TakeWarning());  // 80%
    EXPECT_FALSE(budget.TakeWarning()); // Already reported.

    budget.OnFreed(PoolCapacity{ 25, 0, 0, 0, 0 }); // 55%: below the re-arm mark.
    EXPECT_FALSE(budget.TakeWarning());
    budget.OnAllocated(PoolCapacity{ 30, 0, 0, 0, 0 }); // 85%
    EXPECT_TRUE(budget.TakeWarning());
}

TEST(DescriptorBudgetTest, FreeingBelowTheWarnMarkButAboveRearmDoesNotRearm)
{
    DescriptorBudget budget;
    budget.AddCapacity(PoolCapacity{ 100, 0, 0, 0, 0 });
    budget.OnAllocated(PoolCapacity{ 80, 0, 0, 0, 0 });
    ASSERT_TRUE(budget.TakeWarning());
    budget.OnFreed(PoolCapacity{ 15, 0, 0, 0, 0 }); // 65%
    budget.OnAllocated(PoolCapacity{ 15, 0, 0, 0, 0 }); // 80% again
    EXPECT_FALSE(budget.TakeWarning());
}

TEST(DescriptorBudgetTest, EmptyBudgetHasNoPeak)
{
    DescriptorBudget budget;
    EXPECT_FLOAT_EQ(budget.PeakFraction(), 0.0f);
    EXPECT_FALSE(budget.TakeWarning());
}

TEST(DescriptorCostTest, CountsOneSetAndEveryDescriptorByType)
{
    ReflectedDescriptorBinding buffer;
    buffer.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ReflectedDescriptorBinding uniform;
    uniform.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ReflectedDescriptorBinding samplers;
    samplers.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplers.count = 3;
    ReflectedDescriptorBinding image;
    image.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    const ReflectedDescriptorBinding bindings[] = { buffer, uniform, samplers, image };

    EXPECT_EQ(DescriptorCostOf(bindings), (PoolCapacity{ 1, 1, 1, 3, 1 }));
    EXPECT_EQ(DescriptorCostOf({}), (PoolCapacity{ 1, 0, 0, 0, 0 }));
}

TEST(DescriptorCostTest, DescriptorTypesTheSetDoesNotServeAreIgnored)
{
    PoolCapacity cost;
    AddDescriptorCost(cost, VK_DESCRIPTOR_TYPE_SAMPLER, 4);
    EXPECT_EQ(cost, PoolCapacity{});
}

} // namespace
} // namespace gte
