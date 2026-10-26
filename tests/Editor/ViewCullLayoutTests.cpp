// Placement of the cull view box.

#include "Editor/ViewCullLayout.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

constexpr Vec2 kImageMin{ 100.0f, 50.0f };
constexpr float kSwitcherBottom = 78.0f;

TEST(ViewCullLayoutTest, BoxSitsBelowTheSwitcherAtNarrowWidth)
{
    const auto box = PlaceViewCullBox(kImageMin, Vec2(350.0f, 400.0f), kSwitcherBottom);
    ASSERT_TRUE(box.has_value());
    EXPECT_GE(box->min.y, kSwitcherBottom + kViewCullMargin);

    // The switcher rect starts at imageMin + 8 and ends at its bottom edge.
    const float switcherRight = kImageMin.x + 8.0f + 200.0f;
    const bool overlapsX = box->min.x < switcherRight && box->min.x + box->width > kImageMin.x + 8.0f;
    const bool overlapsY = box->min.y < kSwitcherBottom;
    EXPECT_FALSE(overlapsX && overlapsY);
}

TEST(ViewCullLayoutTest, BoxIsRightAlignedWithMargin)
{
    const Vec2 size(350.0f, 400.0f);
    const auto box = PlaceViewCullBox(kImageMin, size, kSwitcherBottom);
    ASSERT_TRUE(box.has_value());
    EXPECT_FLOAT_EQ(box->min.x + box->width, kImageMin.x + size.x - kViewCullMargin);
    EXPECT_FLOAT_EQ(box->width, kViewCullMaxWidth);
}

TEST(ViewCullLayoutTest, WideImagesCapTheWidth)
{
    const auto box = PlaceViewCullBox(kImageMin, Vec2(1200.0f, 600.0f), kSwitcherBottom);
    ASSERT_TRUE(box.has_value());
    EXPECT_FLOAT_EQ(box->width, kViewCullMaxWidth);
}

TEST(ViewCullLayoutTest, TooNarrowGivesNothing)
{
    EXPECT_FALSE(PlaceViewCullBox(kImageMin, Vec2(150.0f, 400.0f), kSwitcherBottom).has_value());
}

TEST(ViewCullLayoutTest, TooShortGivesNothing)
{
    EXPECT_FALSE(PlaceViewCullBox(kImageMin, Vec2(350.0f, 90.0f), kSwitcherBottom).has_value());
}

TEST(ViewCullLayoutTest, MaxHeightEndsAtTheImageBottomMargin)
{
    const Vec2 size(350.0f, 400.0f);
    const auto box = PlaceViewCullBox(kImageMin, size, kSwitcherBottom);
    ASSERT_TRUE(box.has_value());
    EXPECT_FLOAT_EQ(box->min.y + box->maxHeight, kImageMin.y + size.y - kViewCullMargin);
}

TEST(ViewCullLayoutTest, SwitcherAboveTheImageIsIgnored)
{
    const auto box = PlaceViewCullBox(kImageMin, Vec2(350.0f, 400.0f), 0.0f);
    ASSERT_TRUE(box.has_value());
    EXPECT_FLOAT_EQ(box->min.y, kImageMin.y + kViewCullMargin);
}

} // namespace
} // namespace gte
