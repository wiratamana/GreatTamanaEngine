// Tier 1: mouse ray, ground pick, handle list and hit tests of the water gizmos.

#include "Editor/ViewportProjection.h"
#include "Features/Water/Editor/WaterGizmoMath.h"

#include <gtest/gtest.h>

#include <cmath>

namespace gte {

namespace {

constexpr Vec2 kMin{ 10.0f, 20.0f };
constexpr Vec2 kMax{ 810.0f, 470.0f };

// Camera 120 m up looking down at the origin from the south, like the terrain camera.
Mat4 View()
{
    const Mat4 view = Mat4::LookAtLH(Vec3(0.0f, 120.0f, -160.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    const Mat4 proj = Mat4::PerspectiveFovLH_ZO(DegToRad(60.0f), 800.0f / 450.0f, 3.0f, 5000.0f, true);
    return proj * view;
}

} // namespace

TEST(WaterGizmoMathTests, RayThroughAProjectedPointHitsThatPointOnItsPlane)
{
    const Mat4 vp = View();
    for (int i = 0; i < 20; ++i) {
        const Vec3 world(-60.0f + 7.0f * i, 12.0f, -20.0f + 9.0f * (i % 7));
        Vec2 pixel;
        ASSERT_TRUE(ProjectToScreen(world, vp, kMin, kMax, pixel));
        WaterRay ray;
        ASSERT_TRUE(ScreenToRay(vp, kMin, kMax, pixel, ray));
        double x = 0.0;
        double z = 0.0;
        ASSERT_TRUE(RayHitPlaneY(ray, 12.0, x, z));
        EXPECT_NEAR(x, world.x, 0.05);
        EXPECT_NEAR(z, world.z, 0.05);
    }
}

TEST(WaterGizmoMathTests, DegenerateInputsAreRefused)
{
    WaterRay ray;
    EXPECT_FALSE(ScreenToRay(View(), kMin, kMin, Vec2(0, 0), ray));
    EXPECT_FALSE(ScreenToRay(Mat4::Zero(), kMin, kMax, Vec2(100, 100), ray));
    // A ray that looks up never reaches a plane below the camera.
    WaterRay up;
    up.origin[1] = 50.0;
    up.direction[1] = 1.0;
    double x = 0.0;
    double z = 0.0;
    EXPECT_FALSE(RayHitPlaneY(up, 0.0, x, z));
    WaterRay flat;
    flat.direction[0] = 1.0;
    EXPECT_FALSE(RayHitPlaneY(flat, 0.0, x, z));
}

TEST(WaterGizmoMathTests, GroundPickFindsTheSurfaceUnderThePixel)
{
    const Mat4 vp = View();
    const water::HeightFn ground = [](double x, double z) { return static_cast<float>(5.0 + 0.1 * x + 0.05 * z); };
    const Vec3 target(30.0f, 5.0f + 3.0f + 0.05f * 10.0f, 10.0f);
    Vec2 pixel;
    ASSERT_TRUE(ProjectToScreen(target, vp, kMin, kMax, pixel));
    WaterRay ray;
    ASSERT_TRUE(ScreenToRay(vp, kMin, kMax, pixel, ray));
    double x = 0, y = 0, z = 0;
    ASSERT_TRUE(RayHitGround(ray, ground, 2000.0, x, y, z));
    EXPECT_NEAR(y, ground(x, z), 0.01);
    EXPECT_NEAR(x, 30.0, 0.5);
    // A ray that never goes down finds nothing.
    WaterRay sky = ray;
    sky.direction[1] = 0.5;
    EXPECT_FALSE(RayHitGround(sky, ground, 2000.0, x, y, z));
}

TEST(WaterGizmoMathTests, HandlesPickMidpointsInsertAndCornersWin)
{
    const Mat4 vp = View();
    water::WaterBody lake = water::MakeBoxLake("L", 0, 0, 80, 60, 10.0f);
    lake.id = 4;
    std::vector<WaterHandle> handles;
    BuildWaterHandles(lake, vp, kMin, kMax, handles);
    ASSERT_EQ(handles.size(), 8u) << "4 corners and 4 edge midpoints";
    int corners = 0;
    for (const WaterHandle& h : handles) {
        corners += h.midpoint ? 0 : 1;
        EXPECT_EQ(h.bodyId, 4u);
    }
    EXPECT_EQ(corners, 4);

    // The cursor on a corner picks the corner even though a midpoint is also in range at a big radius.
    const int corner = PickWaterHandle(handles, handles[0].screen, 400.0f);
    ASSERT_GE(corner, 0);
    EXPECT_FALSE(handles[static_cast<std::size_t>(corner)].midpoint);
    EXPECT_EQ(handles[static_cast<std::size_t>(corner)].index, 0);

    int mid = -1;
    for (std::size_t i = 0; i < handles.size(); ++i) {
        mid = handles[i].midpoint && handles[i].index == 2 ? static_cast<int>(i) : mid;
    }
    ASSERT_GE(mid, 0);
    const int picked = PickWaterHandle(handles, handles[static_cast<std::size_t>(mid)].screen, 4.0f);
    ASSERT_GE(picked, 0);
    EXPECT_TRUE(handles[static_cast<std::size_t>(picked)].midpoint);
    EXPECT_EQ(handles[static_cast<std::size_t>(picked)].index, 2);
    EXPECT_EQ(PickWaterHandle(handles, Vec2(5000, 5000), 10.0f), -1);
}

TEST(WaterGizmoMathTests, RiverHandlesAreOpenAndOutlineInsertFindsTheNearestEdge)
{
    const Mat4 vp = View();
    water::WaterBody river = water::MakeRiverBody("R", { { -40, 0, 10.0f, 10 }, { 0, 20, 8.0f, 10 }, { 40, 0, 6.0f, 10 } });
    river.id = 9;
    std::vector<WaterHandle> handles;
    BuildWaterHandles(river, vp, kMin, kMax, handles);
    EXPECT_EQ(handles.size(), 5u) << "3 points and 2 midpoints, no closing edge";

    Vec2 a;
    Vec2 b;
    ASSERT_TRUE(ProjectToScreen(Vec3(0.0f, 8.4f, 20.0f), vp, kMin, kMax, a));
    ASSERT_TRUE(ProjectToScreen(Vec3(40.0f, 6.4f, 0.0f), vp, kMin, kMax, b));
    float distance = 0.0f;
    const Vec2 onSecondEdge = (a + b) * 0.5f;
    EXPECT_EQ(NearestOutlineInsertIndex(river, vp, kMin, kMax, onSecondEdge, distance), 2);
    EXPECT_LT(distance, 1.0f);

    water::WaterBody ocean = water::MakeOceanBody(0.0f);
    handles.clear();
    BuildWaterHandles(ocean, vp, kMin, kMax, handles);
    EXPECT_TRUE(handles.empty());
    EXPECT_EQ(NearestOutlineInsertIndex(ocean, vp, kMin, kMax, Vec2(0, 0), distance), -1);
}

TEST(WaterGizmoMathTests, DragPlaneHeightFollowsTheBody)
{
    water::WaterBody lake = water::MakeBoxLake("L", 0, 0, 10, 10, 33.0f);
    EXPECT_EQ(HandlePlaneY(lake, 0), 33.0);
    water::WaterBody river = water::MakeRiverBody("R", { { 0, 0, 12.0f, 5 }, { 10, 0, 9.0f, 5 } });
    EXPECT_EQ(HandlePlaneY(river, 1), 9.0);
}

} // namespace gte
