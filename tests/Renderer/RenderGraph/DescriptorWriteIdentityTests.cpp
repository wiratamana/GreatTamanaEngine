// Tier-1 tests: a buffer descriptor write carries the identity of its buffer.

#include "Renderer/DescriptorSet.h"
#include "Renderer/RenderGraph/RenderGraphComputeBindingCache.h"

#include <gtest/gtest.h>

#include <vector>

namespace gte::rg {
namespace {

TEST(DescriptorWriteIdentityTest, ADifferentIdentityOfTheSameBufferValueNeedsARewrite)
{
    const std::vector<DescriptorWrite> a{ DescriptorWrite::StorageBuffer(0, VK_NULL_HANDLE).WithIdentity(1) };
    const std::vector<DescriptorWrite> b{ DescriptorWrite::StorageBuffer(0, VK_NULL_HANDLE).WithIdentity(2) };
    EXPECT_TRUE(DescriptorWritesNeedRewrite(a, b));
    EXPECT_FALSE(DescriptorWritesNeedRewrite(a, a));
}

TEST(DescriptorWriteIdentityTest, IdentityIsPartOfTheWriteEquality)
{
    const DescriptorWrite a = DescriptorWrite::StorageBuffer(0, VK_NULL_HANDLE).WithIdentity(1);
    const DescriptorWrite b = DescriptorWrite::StorageBuffer(0, VK_NULL_HANDLE).WithIdentity(2);
    EXPECT_TRUE(a == a);
    EXPECT_FALSE(a == b);
}

TEST(DescriptorWriteIdentityTest, ImageWritesAlwaysRewriteAndCarryNoIdentity)
{
    const std::vector<DescriptorWrite> writes{
        DescriptorWrite::StorageImage(0, reinterpret_cast<VkImageView>(std::uintptr_t{ 16 }))
    };
    EXPECT_EQ(writes[0].identity, 0u);
    EXPECT_TRUE(DescriptorWritesNeedRewrite(writes, writes));
}

} // namespace
} // namespace gte::rg
