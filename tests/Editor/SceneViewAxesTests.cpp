// Unit tests for the Scene view axis table, gizmo layout and hit test.
#include "Editor/SceneViewAxes.h"
#include "Math/Quat.h"

#include <gtest/gtest.h>

#include <array>
#include <optional>

namespace gte {
namespace {

constexpr std::array<SceneViewAxis, kSceneViewAxisCount> kAllAxes = {
    SceneViewAxis::PositiveX, SceneViewAxis::NegativeX, SceneViewAxis::PositiveY,
    SceneViewAxis::NegativeY, SceneViewAxis::PositiveZ, SceneViewAxis::NegativeZ,
};

const SceneViewGizmoHead* FindHead(const SceneViewGizmoLayout& layout, SceneViewAxis axis)
{
    for (const SceneViewGizmoHead& head : layout.heads) {
        if (head.axis == axis) {
            return &head;
        }
    }
    return nullptr;
}

SceneViewGizmoLayout FrontLayout()
{
    return LayoutSceneViewGizmo(Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f), 30.0f);
}

SceneViewGizmoLayout TopLayout()
{
    return LayoutSceneViewGizmo(Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, -1.0f, 0.0f), 30.0f);
}

} // namespace

TEST(SceneViewAxesTest, LookDirectionMatchesTheAnglesOfEveryAxis)
{
    for (const SceneViewAxis axis : kAllAxes) {
        const SceneViewAngles angles = SceneViewAnglesFor(axis);
        const Vec3 forward =
            Quat::FromEulerDegrees(angles.pitchDegrees, angles.yawDegrees, 0.0f).RotateVector(Vec3::Forward());
        EXPECT_TRUE(ApproximatelyEqual(forward, SceneViewLookDirection(axis), 1e-4f)) << SceneViewAxisName(axis);
    }
}

TEST(SceneViewAxesTest, NamesAreTheUnityViewNames)
{
    EXPECT_STREQ(SceneViewAxisName(SceneViewAxis::PositiveX), "Right");
    EXPECT_STREQ(SceneViewAxisName(SceneViewAxis::NegativeX), "Left");
    EXPECT_STREQ(SceneViewAxisName(SceneViewAxis::PositiveY), "Top");
    EXPECT_STREQ(SceneViewAxisName(SceneViewAxis::NegativeY), "Bottom");
    EXPECT_STREQ(SceneViewAxisName(SceneViewAxis::PositiveZ), "Back");
    EXPECT_STREQ(SceneViewAxisName(SceneViewAxis::NegativeZ), "Front");
}

TEST(SceneViewAxesTest, ParsesOnlyLowercaseNames)
{
    const std::array<const char*, kSceneViewAxisCount> names = { "right", "left", "top", "bottom", "back", "front" };
    const std::array<SceneViewAxis, kSceneViewAxisCount> expected = {
        SceneViewAxis::PositiveX, SceneViewAxis::NegativeX, SceneViewAxis::PositiveY,
        SceneViewAxis::NegativeY, SceneViewAxis::PositiveZ, SceneViewAxis::NegativeZ,
    };
    for (std::size_t i = 0; i < names.size(); ++i) {
        SceneViewAxis out = SceneViewAxis::PositiveX;
        EXPECT_TRUE(TryParseSceneViewAxis(names[i], out)) << names[i];
        EXPECT_EQ(out, expected[i]) << names[i];
    }
    for (const char* bad : { "Top", "TOP", "", "+x", "up" }) {
        SceneViewAxis out = SceneViewAxis::NegativeY;
        EXPECT_FALSE(TryParseSceneViewAxis(bad, out)) << bad;
        EXPECT_EQ(out, SceneViewAxis::NegativeY) << bad;
    }
}

TEST(SceneViewAxesTest, AlignedAxisIsFoundForEveryAxis)
{
    for (const SceneViewAxis axis : kAllAxes) {
        const std::optional<SceneViewAxis> found = AlignedSceneViewAxis(SceneViewLookDirection(axis));
        ASSERT_TRUE(found.has_value()) << SceneViewAxisName(axis);
        EXPECT_EQ(*found, axis);
    }
}

TEST(SceneViewAxesTest, AlignedAxisIsEmptyWhenTilted)
{
    EXPECT_FALSE(AlignedSceneViewAxis(Vec3(0.6f, 0.0f, 0.8f)).has_value());
    EXPECT_FALSE(AlignedSceneViewAxis(Normalize(Vec3(0.0873f, 0.0f, 0.9962f))).has_value());
    const std::optional<SceneViewAxis> nearFront = AlignedSceneViewAxis(Normalize(Vec3(0.001745f, 0.0f, 1.0f)));
    ASSERT_TRUE(nearFront.has_value());
    EXPECT_EQ(*nearFront, SceneViewAxis::NegativeZ);
}

TEST(SceneViewAxesTest, LabelTextCombinesViewAndProjection)
{
    EXPECT_EQ(SceneViewLabel(std::nullopt, false), "Persp");
    EXPECT_EQ(SceneViewLabel(std::nullopt, true), "Iso");
    EXPECT_EQ(SceneViewLabel(SceneViewAxis::PositiveY, true), "Top Iso");
    EXPECT_EQ(SceneViewLabel(SceneViewAxis::NegativeZ, false), "Front Persp");
}

TEST(SceneViewAxesTest, FrontViewLayout)
{
    const SceneViewGizmoLayout layout = FrontLayout();
    EXPECT_EQ(layout.heads[0].axis, SceneViewAxis::NegativeZ);
    EXPECT_TRUE(ApproximatelyEqual(layout.heads[0].offset, Vec2(0.0f, 0.0f), 1e-4f));
    EXPECT_NEAR(layout.heads[0].depth, -1.0f, 1e-4f);
    EXPECT_EQ(layout.heads.back().axis, SceneViewAxis::PositiveZ);
    EXPECT_NEAR(layout.heads.back().depth, 1.0f, 1e-4f);

    const SceneViewGizmoHead* x = FindHead(layout, SceneViewAxis::PositiveX);
    ASSERT_NE(x, nullptr);
    EXPECT_TRUE(ApproximatelyEqual(x->offset, Vec2(30.0f, 0.0f), 1e-4f));
    EXPECT_NEAR(x->depth, 0.0f, 1e-4f);

    const SceneViewGizmoHead* y = FindHead(layout, SceneViewAxis::PositiveY);
    ASSERT_NE(y, nullptr);
    EXPECT_TRUE(ApproximatelyEqual(y->offset, Vec2(0.0f, -30.0f), 1e-4f));

    for (const SceneViewAxis axis : kAllAxes) {
        int count = 0;
        for (const SceneViewGizmoHead& head : layout.heads) {
            count += (head.axis == axis) ? 1 : 0;
        }
        EXPECT_EQ(count, 1) << SceneViewAxisName(axis);
    }
    for (std::size_t i = 1; i < layout.heads.size(); ++i) {
        EXPECT_GE(layout.heads[i].depth, layout.heads[i - 1].depth);
    }
}

TEST(SceneViewAxesTest, HitTestFindsTheHeadUnderThePoint)
{
    const SceneViewGizmoLayout layout = FrontLayout();
    EXPECT_EQ(HitTestSceneViewGizmo(layout, Vec2(30.0f, 0.0f), 10.0f), SceneViewAxis::PositiveX);
    EXPECT_EQ(HitTestSceneViewGizmo(layout, Vec2(0.0f, -30.0f), 10.0f), SceneViewAxis::PositiveY);
    EXPECT_EQ(HitTestSceneViewGizmo(layout, Vec2(-30.0f, 0.0f), 10.0f), SceneViewAxis::NegativeX);
    EXPECT_EQ(HitTestSceneViewGizmo(layout, Vec2(30.0f, 9.0f), 10.0f), SceneViewAxis::PositiveX);
    EXPECT_FALSE(HitTestSceneViewGizmo(layout, Vec2(30.0f, 11.0f), 10.0f).has_value());
    EXPECT_FALSE(HitTestSceneViewGizmo(layout, Vec2(100.0f, 100.0f), 10.0f).has_value());
}

TEST(SceneViewAxesTest, HitTestPrefersTheHeadNearestTheViewer)
{
    EXPECT_EQ(HitTestSceneViewGizmo(TopLayout(), Vec2(0.0f, 0.0f), 10.0f), SceneViewAxis::PositiveY);
}

TEST(SceneViewAxesTest, TopViewLayoutPutsPositiveZUp)
{
    const SceneViewGizmoLayout layout = TopLayout();
    const SceneViewGizmoHead* z = FindHead(layout, SceneViewAxis::PositiveZ);
    const SceneViewGizmoHead* x = FindHead(layout, SceneViewAxis::PositiveX);
    ASSERT_NE(z, nullptr);
    ASSERT_NE(x, nullptr);
    EXPECT_TRUE(ApproximatelyEqual(z->offset, Vec2(0.0f, -30.0f), 1e-4f));
    EXPECT_TRUE(ApproximatelyEqual(x->offset, Vec2(30.0f, 0.0f), 1e-4f));
}

} // namespace gte
