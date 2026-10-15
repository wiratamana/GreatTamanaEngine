// Tier-1 tests: PerView keeps one stable slot per ViewScope.

#include "Renderer/RenderGraph/PerView.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

struct Slot {
    int value = 0;
};

TEST(PerViewTest, EachScopeHasADistinctSlot)
{
    PerView<Slot> views;
    EXPECT_NE(&views.For(ViewScope::Shared), &views.For(ViewScope::GameView));
    EXPECT_NE(&views.For(ViewScope::GameView), &views.For(ViewScope::SceneView));
    EXPECT_NE(&views.For(ViewScope::Shared), &views.For(ViewScope::SceneView));
}

TEST(PerViewTest, RepeatedLookupReturnsTheSameSlotAndKeepsWrites)
{
    PerView<Slot> views;
    views.For(ViewScope::GameView).value = 7;
    EXPECT_EQ(&views.For(ViewScope::GameView), &views.For(ViewScope::GameView));
    EXPECT_EQ(views.For(ViewScope::GameView).value, 7);
    EXPECT_EQ(views.For(ViewScope::SceneView).value, 0);
}

TEST(PerViewTest, ConstLookupMatchesNonConst)
{
    PerView<Slot> views;
    const PerView<Slot>& constViews = views;
    EXPECT_EQ(&constViews.For(ViewScope::SceneView), &views.For(ViewScope::SceneView));
}

} // namespace
} // namespace gte::rg
