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
    EXPECT_NE(&views.For(ViewScope::Shared), &views.For(ViewScope::Primary));
    EXPECT_NE(&views.For(ViewScope::Primary), &views.For(ViewScope::Secondary));
    EXPECT_NE(&views.For(ViewScope::Shared), &views.For(ViewScope::Secondary));
}

TEST(PerViewTest, RepeatedLookupReturnsTheSameSlotAndKeepsWrites)
{
    PerView<Slot> views;
    views.For(ViewScope::Primary).value = 7;
    EXPECT_EQ(&views.For(ViewScope::Primary), &views.For(ViewScope::Primary));
    EXPECT_EQ(views.For(ViewScope::Primary).value, 7);
    EXPECT_EQ(views.For(ViewScope::Secondary).value, 0);
}

TEST(PerViewTest, ConstLookupMatchesNonConst)
{
    PerView<Slot> views;
    const PerView<Slot>& constViews = views;
    EXPECT_EQ(&constViews.For(ViewScope::Secondary), &views.For(ViewScope::Secondary));
}

} // namespace
} // namespace gte::rg
