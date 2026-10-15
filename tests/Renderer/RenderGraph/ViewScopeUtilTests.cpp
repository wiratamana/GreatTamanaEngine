// Tier-1 tests: ViewPair indexing by ViewScope.

#include "Renderer/RenderGraph/ViewScopeUtil.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

TEST(ViewScopeUtilTest, ScopeIndicesAreDistinctAndInRange)
{
    EXPECT_EQ(ViewScopeIndex(ViewScope::Shared), 0u);
    EXPECT_EQ(ViewScopeIndex(ViewScope::GameView), 1u);
    EXPECT_EQ(ViewScopeIndex(ViewScope::SceneView), 2u);
    EXPECT_EQ(kViewScopeCount, 3u);
}

TEST(ViewScopeUtilTest, ViewPairReturnsTheEntryOfEachScope)
{
    constexpr ViewPair<int> pair{ { 10, 20, 30 } };
    static_assert(pair.For(ViewScope::Shared) == 10);
    EXPECT_EQ(pair.For(ViewScope::Shared), 10);
    EXPECT_EQ(pair.For(ViewScope::GameView), 20);
    EXPECT_EQ(pair.For(ViewScope::SceneView), 30);
}

TEST(ViewScopeUtilTest, SharedHasItsOwnEntry)
{
    constexpr ViewPair<const char*> names{ { "shared", "game", "scene" } };
    EXPECT_STRNE(names.For(ViewScope::Shared), names.For(ViewScope::SceneView));
    EXPECT_STRNE(names.For(ViewScope::Shared), names.For(ViewScope::GameView));
}

} // namespace
} // namespace gte::rg
