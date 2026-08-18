// Unit tests for GpuResourceHandle (src/Renderer/Memory/GpuResourceHandle.h)
// - pure POD/value-semantics checks. No Vulkan device, GPU resource, or
// GpuMemoryTracker involved at all (see GpuMemoryTrackerTests.cpp for the
// tracker itself, which is what actually generates these handles).

#include "Renderer/Memory/GpuResourceHandle.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(GpuResourceHandleTest, DefaultConstructedHandleIsInvalid)
{
    const GpuResourceHandle handle;

    EXPECT_FALSE(handle.IsValid());
    EXPECT_EQ(handle.index, 0u);
    EXPECT_EQ(handle.generation, 0u);
}

TEST(GpuResourceHandleTest, KInvalidHandleIsInvalidAndEqualsDefault)
{
    EXPECT_FALSE(kInvalidGpuResourceHandle.IsValid());
    EXPECT_EQ(kInvalidGpuResourceHandle, GpuResourceHandle{});
}

TEST(GpuResourceHandleTest, NonZeroGenerationIsValid)
{
    // generation == 0 is the ONLY thing IsValid() checks - see the class
    // comment in GpuResourceHandle.h ("0 == never assigned / invalid").
    const GpuResourceHandle handle{ 3, 1 };

    EXPECT_TRUE(handle.IsValid());
}

TEST(GpuResourceHandleTest, EqualityComparesBothIndexAndGeneration)
{
    const GpuResourceHandle a{ 5, 2 };
    const GpuResourceHandle b{ 5, 2 };
    const GpuResourceHandle differentIndex{ 6, 2 };
    const GpuResourceHandle differentGeneration{ 5, 3 };

    EXPECT_EQ(a, b);
    EXPECT_NE(a, differentIndex);
    EXPECT_NE(a, differentGeneration);
}

TEST(GpuResourceHandleTest, InequalityIsExactNegationOfEquality)
{
    const GpuResourceHandle a{ 1, 1 };
    const GpuResourceHandle b{ 1, 1 };

    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a == b);
}

} // namespace
} // namespace gte
