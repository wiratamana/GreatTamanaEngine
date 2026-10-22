// Tier-1 tests for the indirect draw capability decisions in src/Renderer/Culling/ClusterCullMath.h.

#include "Renderer/Culling/ClusterCullMath.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

constexpr IndirectDrawCapsView kAllCaps{ true, true, true, 65536 };

TEST(ClusterCullMath, ClusterPathNeedsEveryCapability)
{
    EXPECT_TRUE(ClusterPathSupported(kAllCaps));

    IndirectDrawCapsView caps = kAllCaps;
    caps.drawIndirectCount = false;
    EXPECT_FALSE(ClusterPathSupported(caps));

    caps = kAllCaps;
    caps.multiDrawIndirect = false;
    EXPECT_FALSE(ClusterPathSupported(caps));

    caps = kAllCaps;
    caps.drawIndirectFirstInstance = false;
    EXPECT_FALSE(ClusterPathSupported(caps));

    caps = kAllCaps;
    caps.maxDrawIndirectCount = 0;
    EXPECT_FALSE(ClusterPathSupported(caps));
}

TEST(ClusterCullMath, OldBatchPathNeedsThreeFeaturesButNoLimit)
{
    EXPECT_TRUE(OldBatchPathSupported(kAllCaps));

    IndirectDrawCapsView caps = kAllCaps;
    caps.drawIndirectCount = false;
    EXPECT_FALSE(OldBatchPathSupported(caps));

    caps = kAllCaps;
    caps.multiDrawIndirect = false;
    EXPECT_FALSE(OldBatchPathSupported(caps));

    caps = kAllCaps;
    caps.drawIndirectFirstInstance = false;
    EXPECT_FALSE(OldBatchPathSupported(caps));

    caps = kAllCaps;
    caps.maxDrawIndirectCount = 0;
    EXPECT_TRUE(OldBatchPathSupported(caps));
}

TEST(ClusterCullMath, DefaultCapsSupportNothing)
{
    constexpr IndirectDrawCapsView none{};
    EXPECT_FALSE(ClusterPathSupported(none));
    EXPECT_FALSE(OldBatchPathSupported(none));
}

} // namespace
} // namespace gte
