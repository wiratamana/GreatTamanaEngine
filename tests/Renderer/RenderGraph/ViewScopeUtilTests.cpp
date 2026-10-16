// Tier-1 tests: ViewScope array indices.

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

} // namespace
} // namespace gte::rg
