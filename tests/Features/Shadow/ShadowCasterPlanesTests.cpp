// Tests for the caster bound of a shadow cascade (BuildShadowCasterPlanes in src/Features/Shadow/ShadowMath.h).
// The bound must never reject a caster that can change a texel a receiver reads. No GPU device is involved.

#include "Core/FeatureKit/ClusterPartPlanning.h"
#include "Features/Shadow/ShadowMath.h"
#include "Renderer/Culling/CullingTypes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <random>
#include <utility>
#include <vector>

namespace gte {
namespace {

constexpr float kPi = 3.14159265358979f;

struct D3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

D3 operator+(const D3& a, const D3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
D3 operator-(const D3& a, const D3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
D3 operator*(const D3& a, double s) { return { a.x * s, a.y * s, a.z * s }; }
double Dot3(const D3& a, const D3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
D3 Cross3(const D3& a, const D3& b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
double Len3(const D3& a) { return std::sqrt(Dot3(a, a)); }
D3 Unit3(const D3& a) { return a * (1.0 / Len3(a)); }
D3 ToD3(const Vec3& v) { return { v.x, v.y, v.z }; }

// A perspective camera run through the same inverse view-projection the renderer hands to the shadow code.
struct Camera {
    ShadowViewFrustum frustum;
    Mat4 viewProjection = Mat4::Identity();
};

Camera MakeCamera(const Vec3& eye, const Vec3& target, const Vec3& up, float fovDegrees, float aspect, float nearZ,
    float farZ)
{
    Camera camera;
    const Mat4 view = Mat4::LookAtLH(eye, target, up);
    const Mat4 proj = Mat4::PerspectiveFovLH_ZO(fovDegrees * kPi / 180.0f, aspect, nearZ, farZ, /*flipY=*/true);
    camera.viewProjection = proj * view;
    Mat4 inverse;
    if (camera.viewProjection.TryInverse(inverse)) {
        camera.frustum = ExtractViewFrustum(eye, inverse);
    }
    return camera;
}

// The live demo view: a camera at z = -417.8 looking along +Z, 60 degrees, a 351 x 447 game view.
Camera DemoCamera()
{
    return MakeCamera(Vec3(0.0f, 0.0f, -417.8247f), Vec3(0.0f, 0.0f, 0.0f), Vec3::Up(), 60.0f, 351.0f / 447.0f, 0.1f, 1000.0f);
}

constexpr float kDemoViewWidth = 351.0f;
constexpr float kDemoViewHeight = 447.0f;

// Pitch 50 / yaw 30 sun, as in the demo: 50 degrees above the horizon, behind the camera.
Vec3 DemoSunTowardSun()
{
    const float pitch = 50.0f * kPi / 180.0f;
    const float yaw = 30.0f * kPi / 180.0f;
    return Normalize(Vec3(-std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)));
}

ShadowCascadeSet MakeSet(const Camera& camera, const Vec3& towardSun, const ShadowSettings& settings,
    std::optional<ShadowDepthRange> range, std::uint32_t resolution = 1024)
{
    return BuildShadowCascades(camera.frustum, towardSun, SanitizeShadowSettings(settings), resolution, range);
}

ShadowCasterPlanes BuildLane(const Camera& camera, const ShadowCascadeSet& set, std::uint32_t lane,
    const ShadowSettings& settings, float reach = 0.0f, float width = kDemoViewWidth, float height = kDemoViewHeight)
{
    return BuildShadowCasterPlanes(camera.frustum, set, lane, SanitizeShadowSettings(settings), width, height, reach);
}

std::span<const Plane> PlanesOf(const ShadowCasterPlanes& bound)
{
    return std::span<const Plane>(bound.planes.planes.data(), bound.planes.count);
}

AABB PointBox(const D3& p)
{
    const Vec3 v(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z));
    return AABB{ v, v };
}

bool Rejected(const ShadowCasterPlanes& bound, const D3& p)
{
    return IsAABBOutsideAnyPlane(PlanesOf(bound), PointBox(p));
}

D3 OnAxis(const Camera& camera, double depth)
{
    return ToD3(camera.frustum.eye) + ToD3(camera.frustum.forward) * depth;
}

// ---------------------------------------------------------------------------------------------
// Demo scene: the whole point of the bound
// ---------------------------------------------------------------------------------------------

// The 50,000-sheet field of the demo: 224 columns, spacing 2, centred on (0, 0, 8), each sheet 1 x 1 and flat.
std::vector<AABB> DemoFieldBoxes()
{
    std::vector<AABB> boxes;
    boxes.reserve(50000);
    for (int i = 0; i < 50000; ++i) {
        const float x = (static_cast<float>(i % 224) - 111.5f) * 2.0f;
        const float y = (static_cast<float>(i / 224) - 111.5f) * 2.0f;
        boxes.push_back(AABB{ Vec3(x - 0.5f, y - 0.5f, 8.0f), Vec3(x + 0.5f, y + 0.5f, 8.0f) });
    }
    return boxes;
}

fx::ClusterPartBox UnionOf(const std::vector<AABB>& boxes)
{
    std::vector<std::uint32_t> indices(boxes.size());
    for (std::size_t i = 0; i < indices.size(); ++i) {
        indices[i] = static_cast<std::uint32_t>(i);
    }
    return fx::UnionInstanceBoxes(boxes, indices);
}

TEST(ShadowCasterPlanesTest, DemoFieldIsOutsideTheFirstTwoLanesAndInsideTheLast)
{
    const Camera camera = DemoCamera();
    ASSERT_TRUE(camera.frustum.valid);
    const ShadowSettings settings;
    const ShadowCascadeSet set = MakeSet(camera, DemoSunTowardSun(), settings, ShadowDepthRange{ 304.437f, 512.0f });
    ASSERT_TRUE(set.valid);
    ASSERT_EQ(set.count, 4u);

    const std::vector<AABB> field = DemoFieldBoxes();
    const fx::ClusterPartBox all = UnionOf(field);
    ASSERT_TRUE(all.bounded);
    std::vector<fx::ClusterMeshGroup> coverAll{ { fx::MeshToken{ 1, 1 }, {}, ClusterMaterialKind::None, {} } };
    coverAll[0].instances.resize(field.size());

    const bool expectSkipped[4] = { true, true, false, false };
    for (std::uint32_t lane = 0; lane < 4; ++lane) {
        const ShadowCasterPlanes bound = BuildLane(camera, set, lane, settings);
        ASSERT_TRUE(bound.valid) << "lane " << lane;
        const std::array<Plane, 6> box = ExtractFrustumPlanes(set.cascades[lane].viewProjection);
        const std::array<Plane, 6> chosen = fx::ChooseCullPlanes(box, bound.planes, field);
        const std::vector<std::uint8_t> skip =
            fx::PlanClusterPartSkips(chosen, ClusterCullFlags::Frustum, field, all, coverAll);
        EXPECT_EQ(skip[0] != 0, expectSkipped[lane]) << "lane " << lane;
    }
}

TEST(ShadowCasterPlanesTest, TheDemoFieldKeepsAllItsSheetsInTheLanesThatHoldReceivers)
{
    const Camera camera = DemoCamera();
    const ShadowSettings settings;
    const ShadowCascadeSet set = MakeSet(camera, DemoSunTowardSun(), settings, ShadowDepthRange{ 304.437f, 512.0f });
    ASSERT_TRUE(set.valid);

    // Lane 2 holds the field itself as receivers: a sheet lit from the sun side must stay.
    const ShadowCasterPlanes bound = BuildLane(camera, set, 2, settings);
    ASSERT_TRUE(bound.valid);
    const std::vector<AABB> field = DemoFieldBoxes();
    std::size_t kept = 0;
    for (const AABB& box : field) {
        kept += IsAABBOutsideAnyPlane(PlanesOf(bound), box) ? 0 : 1;
    }
    EXPECT_GT(kept, 40000u);
    EXPECT_LT(kept, field.size()); // The sheets beside the view are rejected.
}

// ---------------------------------------------------------------------------------------------
// The slab of a lane
// ---------------------------------------------------------------------------------------------

ShadowSettings FourCascades()
{
    ShadowSettings s;
    s.cascadeCount = 4;
    return s;
}

TEST(ShadowCasterPlanesTest, FirstLaneStartsAtViewNearAndLastLaneEndsAtShadowDistance)
{
    const Camera camera = DemoCamera();
    const ShadowSettings settings = FourCascades();
    const ShadowCascadeSet set = MakeSet(camera, DemoSunTowardSun(), settings, ShadowDepthRange{ 50.0f, 100.0f });
    ASSERT_TRUE(set.valid);
    ASSERT_GT(set.shadowDistance, set.cascades[3].sliceEnd + 100.0f);

    // Receivers shallower than the first slice still read cascade 0.
    const ShadowCasterPlanes first = BuildLane(camera, set, 0, settings);
    ASSERT_TRUE(first.valid);
    EXPECT_FALSE(Rejected(first, OnAxis(camera, 0.5 * set.cascades[0].sliceStart)));

    // Receivers deeper than the last split still read the last cascade up to the fade end.
    const ShadowCasterPlanes last = BuildLane(camera, set, 3, settings);
    ASSERT_TRUE(last.valid);
    EXPECT_FALSE(Rejected(last, OnAxis(camera, 0.5 * (set.cascades[3].sliceEnd + set.shadowDistance))));
}

TEST(ShadowCasterPlanesTest, FallbackSlabKeepsCastersForLookupsThatLeaveTheEarlierMap)
{
    const Camera camera = DemoCamera();
    const ShadowSettings settings = FourCascades();
    // Sun in front of the camera: the sweep only goes deeper, so a shallower point is covered by the fallback slab alone.
    const Vec3 sunInFront = Normalize(Vec3(0.0f, 0.5f, 0.85f));
    const ShadowCascadeSet set = MakeSet(camera, sunInFront, settings, ShadowDepthRange{ 304.437f, 512.0f });
    ASSERT_TRUE(set.valid);

    const ShadowCasterPlanes lane = BuildLane(camera, set, 2, settings);
    ASSERT_TRUE(lane.valid);
    const double depth = 0.5 * (set.cascades[1].sliceStart + set.cascades[2].sliceStart);
    ASSERT_LT(depth, set.cascades[2].sliceStart);
    EXPECT_FALSE(Rejected(lane, OnAxis(camera, depth)));
    // Shallower than the earlier slab's start is not read by this lane.
    EXPECT_TRUE(Rejected(lane, OnAxis(camera, 0.5 * set.cascades[1].sliceStart)));
}

// Shallowest and deepest depth on the view axis, within [from, to], that the bound keeps.
std::pair<double, double> KeptDepthSpan(
    const Camera& camera, const ShadowCasterPlanes& bound, double from, double to)
{
    double shallowest = to;
    double deepest = from;
    for (double depth = from; depth <= to; depth += 0.05) {
        if (!Rejected(bound, OnAxis(camera, depth))) {
            shallowest = std::min(shallowest, depth);
            deepest = std::max(deepest, depth);
        }
    }
    return { shallowest, deepest };
}

TEST(ShadowCasterPlanesTest, ReachExtendsTheDepthIntervalTowardTheCameraOrAway)
{
    const Camera camera = DemoCamera();
    const ShadowSettings settings = FourCascades();
    const ShadowDepthRange range{ 304.437f, 512.0f };

    // Sun in front: lookups move deeper, so lane 2 reads receivers shallower than its slab. About 16 * 0.86 units.
    const ShadowCascadeSet front = MakeSet(camera, Normalize(Vec3(0.0f, 0.5f, 0.85f)), settings, range);
    ASSERT_TRUE(front.valid);
    const double frontFrom = 0.5 * front.cascades[1].sliceStart;
    const double frontTo = front.cascades[2].sliceStart;
    const auto noReach = KeptDepthSpan(camera, BuildLane(camera, front, 2, settings, 0.0f), frontFrom, frontTo);
    const auto withReach = KeptDepthSpan(camera, BuildLane(camera, front, 2, settings, 16.0f), frontFrom, frontTo);
    EXPECT_LT(withReach.first, noReach.first - 10.0);

    // Sun behind: lookups move toward the camera, so lane 1 reads receivers deeper than its slab. About 16 * 0.56 units.
    const ShadowCascadeSet behind = MakeSet(camera, DemoSunTowardSun(), settings, range);
    ASSERT_TRUE(behind.valid);
    const double behindFrom = behind.cascades[1].sliceEnd - 20.0;
    const double behindTo = behind.cascades[1].sliceEnd + 80.0;
    const auto noReachBehind = KeptDepthSpan(camera, BuildLane(camera, behind, 1, settings, 0.0f), behindFrom, behindTo);
    const auto withReachBehind = KeptDepthSpan(camera, BuildLane(camera, behind, 1, settings, 16.0f), behindFrom, behindTo);
    EXPECT_GT(withReachBehind.second, noReachBehind.second + 6.0);
}

TEST(ShadowCasterPlanesTest, PadGrowsWithTexelSizeFilterAndNormalOffset)
{
    const Camera camera = DemoCamera();
    ShadowSettings tight = FourCascades();
    tight.normalOffsetTexels = 0.0f;
    tight.filterRadiusTexels = 0.0f;
    ShadowSettings wide = FourCascades();
    wide.normalOffsetTexels = 4.0f;
    wide.filterRadiusTexels = 4.0f;
    const ShadowDepthRange range{ 304.437f, 512.0f };
    const ShadowCascadeSet setTight = MakeSet(camera, DemoSunTowardSun(), tight, range);
    const ShadowCascadeSet setWide = MakeSet(camera, DemoSunTowardSun(), wide, range);
    const ShadowCasterPlanes a = BuildLane(camera, setTight, 2, tight);
    const ShadowCasterPlanes b = BuildLane(camera, setWide, 2, wide);
    ASSERT_TRUE(a.valid);
    ASSERT_TRUE(b.valid);

    std::mt19937 rng(5);
    std::uniform_real_distribution<double> uniform(-1.0, 1.0);
    std::size_t keptTight = 0;
    std::size_t keptWide = 0;
    for (int i = 0; i < 4000; ++i) {
        const D3 p = ToD3(camera.frustum.eye) + D3{ uniform(rng) * 700.0, uniform(rng) * 700.0, 300.0 + 400.0 * (uniform(rng) + 1.0) };
        const bool tightKeeps = !Rejected(a, p);
        const bool wideKeeps = !Rejected(b, p);
        EXPECT_TRUE(!tightKeeps || wideKeeps) << "a wider pad must keep what a tighter one keeps";
        keptTight += tightKeeps ? 1 : 0;
        keptWide += wideKeeps ? 1 : 0;
    }
    EXPECT_GT(keptWide, keptTight);
}

TEST(ShadowCasterPlanesTest, CascadeCountOneUsesTheWholeShadowDistance)
{
    const Camera camera = DemoCamera();
    ShadowSettings settings;
    settings.cascadeCount = 1;
    const ShadowCascadeSet set = MakeSet(camera, DemoSunTowardSun(), settings, std::nullopt);
    ASSERT_TRUE(set.valid);
    ASSERT_EQ(set.count, 1u);
    const ShadowCasterPlanes bound = BuildLane(camera, set, 0, settings);
    ASSERT_TRUE(bound.valid);
    EXPECT_FALSE(Rejected(bound, OnAxis(camera, 2.0 * camera.frustum.viewNear)));
    EXPECT_FALSE(Rejected(bound, OnAxis(camera, 0.95 * set.shadowDistance)));
}

// ---------------------------------------------------------------------------------------------
// Refusals and degenerate input
// ---------------------------------------------------------------------------------------------

TEST(ShadowCasterPlanesTest, OrthographicOrNoisyOrInvalidInputGivesNoPlanes)
{
    const Camera camera = DemoCamera();
    EXPECT_TRUE(IsPerspectiveViewProjection(camera.viewProjection));
    EXPECT_FALSE(IsPerspectiveViewProjection(Mat4::OrthographicLH_ZO(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 100.0f)));
    EXPECT_FALSE(IsPerspectiveViewProjection(Mat4::Identity()));

    const ShadowSettings settings = FourCascades();
    const ShadowCascadeSet set = MakeSet(camera, DemoSunTowardSun(), settings, ShadowDepthRange{ 304.437f, 512.0f });
    ASSERT_TRUE(set.valid);
    EXPECT_TRUE(BuildLane(camera, set, 0, settings).valid);

    // Invalid inputs keep the cascade box.
    EXPECT_FALSE(BuildLane(camera, set, 4, settings).valid);
    EXPECT_FALSE(BuildLane(camera, set, 0, settings, 0.0f, 0.0f, 447.0f).valid);
    EXPECT_FALSE(BuildLane(camera, set, 0, settings, 0.0f, 351.0f, -1.0f).valid);
    EXPECT_FALSE(BuildShadowCasterPlanes(ShadowViewFrustum{}, set, 0, settings, 351.0f, 447.0f).valid);
    EXPECT_FALSE(BuildShadowCasterPlanes(camera.frustum, ShadowCascadeSet{}, 0, settings, 351.0f, 447.0f).valid);

    Camera broken = camera;
    broken.frustum.rays[2].x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(BuildLane(broken, set, 0, settings).valid);
    broken = camera;
    broken.frustum.eye.y = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(BuildLane(broken, set, 0, settings).valid);

    // A cascade without a light direction (never built) has no bound.
    ShadowCascadeSet noLight = set;
    noLight.cascades[0].lightForward = Vec3::Zero();
    EXPECT_FALSE(BuildLane(camera, noLight, 0, settings).valid);

    // Far from the origin with a tiny near plane the float frustum is too noisy to bound anything.
    const Camera distant = MakeCamera(Vec3(100000.0f, 90000.0f, -80000.0f), Vec3(100000.0f, 90000.0f, 0.0f), Vec3::Up(),
        60.0f, 1.0f, 0.05f, 5000.0f);
    if (distant.frustum.valid) {
        const ShadowCascadeSet distantSet = MakeSet(distant, DemoSunTowardSun(), settings, std::nullopt);
        if (distantSet.valid) {
            EXPECT_FALSE(BuildLane(distant, distantSet, 0, settings).valid);
        }
    }
}

TEST(ShadowCasterPlanesTest, PlanesAreFiniteUnitLengthAndHoldTheSlabCentroid)
{
    const Camera camera = DemoCamera();
    const ShadowSettings settings = FourCascades();
    const D3 forward = ToD3(camera.frustum.forward);
    const D3 suns[] = {
        forward, forward * -1.0, { 0.0, 1.0, 0.0 }, { 0.0, -1.0, 0.0 }, { 1.0, 0.0, 0.0 },
        Unit3(Cross3(ToD3(camera.frustum.rays[0]), ToD3(camera.frustum.rays[1]))), // Lies in a side plane.
        Unit3(D3{ 0.001, 1.0, 0.001 }),
    };
    for (const D3& sun : suns) {
        const Vec3 toSun(static_cast<float>(sun.x), static_cast<float>(sun.y), static_cast<float>(sun.z));
        const ShadowCascadeSet set = MakeSet(camera, toSun, settings, ShadowDepthRange{ 304.437f, 512.0f });
        ASSERT_TRUE(set.valid);
        for (std::uint32_t lane = 0; lane < set.count; ++lane) {
            const ShadowCasterPlanes bound = BuildLane(camera, set, lane, settings);
            ASSERT_TRUE(bound.valid) << "lane " << lane;
            ASSERT_GE(bound.planes.count, 4u);
            const double mid = 0.5 * (set.cascades[lane].sliceStart + set.cascades[lane].sliceEnd);
            const D3 centroid = OnAxis(camera, mid);
            for (const Plane& plane : PlanesOf(bound)) {
                EXPECT_TRUE(std::isfinite(plane.distance));
                EXPECT_NEAR(Len3(ToD3(plane.normal)), 1.0, 1e-3);
                EXPECT_GE(Dot3(ToD3(plane.normal), centroid) + plane.distance, -1e-3);
            }
        }
    }
}

TEST(ShadowCasterPlanesTest, SunBelowTheHorizonStillGivesPlanes)
{
    const Camera camera = DemoCamera();
    const ShadowSettings settings = FourCascades();
    const ShadowCascadeSet set = MakeSet(camera, Normalize(Vec3(0.3f, -0.6f, -0.5f)), settings, ShadowDepthRange{ 304.437f, 512.0f });
    ASSERT_TRUE(set.valid);
    for (std::uint32_t lane = 0; lane < set.count; ++lane) {
        const ShadowCasterPlanes bound = BuildLane(camera, set, lane, settings);
        EXPECT_TRUE(bound.valid) << "lane " << lane;
        const double mid = 0.5 * (set.cascades[lane].sliceStart + set.cascades[lane].sliceEnd);
        EXPECT_FALSE(Rejected(bound, OnAxis(camera, mid))) << "lane " << lane;
    }
}

// ---------------------------------------------------------------------------------------------
// Randomized proof: a caster that can shadow something a receiver reads is never rejected
// ---------------------------------------------------------------------------------------------

struct Rng {
    std::mt19937_64 gen;
    explicit Rng(std::uint64_t seed)
        : gen(seed)
    {
    }
    double Uniform(double a, double b) { return a + (b - a) * std::uniform_real_distribution<double>(0.0, 1.0)(gen); }
    double LogUniform(double a, double b) { return std::exp(Uniform(std::log(a), std::log(b))); }
    int Int(int a, int b) { return std::uniform_int_distribution<int>(a, b)(gen); }
    bool Chance(double p) { return Uniform(0.0, 1.0) < p; }
    D3 Direction()
    {
        while (true) {
            const D3 v{ Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1) };
            const double length = Len3(v);
            if (length > 1e-3 && length <= 1.0) {
                return v * (1.0 / length);
            }
        }
    }
};

// One map read of the receiver shader: the cascade whose map is read and the world point it looks up.
struct MapRead {
    std::uint32_t cascade = 0;
    D3 point;
};

TEST(ShadowCasterPlanesTest, NoCasterThatShadowsAReceiverIsRejected)
{
    constexpr int kCases = 2500;
    constexpr int kReceivers = 64;
    constexpr double kTapTexels = 1.0; // Nearest-texel read and bilinear footprint.
    Rng rng(20241105);
    std::uint64_t usedLanes = 0;
    std::uint64_t pointTests = 0;
    std::uint64_t falseRejects = 0;
    std::uint64_t greedyTests = 0;
    std::uint64_t greedyFalseRejects = 0;

    for (int testCase = 0; testCase < kCases; ++testCase) {
        const double span = rng.Chance(0.7) ? rng.Uniform(5, 500) : rng.Uniform(500, 3000);
        const Vec3 eye(static_cast<float>(rng.Uniform(-span, span)), static_cast<float>(rng.Uniform(-span, span)),
            static_cast<float>(rng.Uniform(-span, span)));
        D3 forwardD = rng.Direction();
        if (rng.Chance(0.1)) {
            forwardD = Unit3(D3{ 0.0, rng.Chance(0.5) ? 1.0 : -1.0, 0.02 }); // Nearly straight up or down.
        }
        const Vec3 target = eye + Vec3(static_cast<float>(forwardD.x * 1000.0), static_cast<float>(forwardD.y * 1000.0),
                                      static_cast<float>(forwardD.z * 1000.0));
        const float fov = static_cast<float>(rng.Uniform(20, 120));
        const float aspect = static_cast<float>(rng.LogUniform(0.5, 2.5));
        const float nearZ = static_cast<float>(rng.LogUniform(0.05, 2.0));
        const float farZ = static_cast<float>(rng.LogUniform(std::max(100.0, nearZ * 50.0), 5000.0));
        const double viewHeight = rng.Uniform(200, 1500);
        const double viewWidth = aspect * viewHeight;
        const Camera camera = MakeCamera(eye, target, Vec3::Up(), fov, aspect, nearZ, farZ);
        if (!camera.frustum.valid) {
            continue;
        }

        ShadowSettings settings;
        settings.cascadeCount = static_cast<std::uint32_t>(rng.Int(1, 4));
        settings.splitLambda = static_cast<float>(rng.Uniform(0, 1));
        settings.cascadeBlendFraction = static_cast<float>(rng.Uniform(0, 0.5));
        settings.distanceFadeFraction = static_cast<float>(rng.Uniform(0, 0.5));
        settings.casterRange = static_cast<float>(rng.Chance(0.4) ? 200.0 : rng.LogUniform(1, 3000));
        settings.normalOffsetTexels = static_cast<float>(rng.Chance(0.5) ? 1.0 : rng.Uniform(0, 4));
        settings.filterRadiusTexels = static_cast<float>(rng.Chance(0.5) ? 1.5 : rng.Uniform(0, 4));
        settings.maxDistance = static_cast<float>(rng.Chance(0.5) ? 20000.0 : rng.LogUniform(std::max(2.0 * nearZ, 0.1 * farZ), 1.5 * farZ));
        settings.stabilizeCascades = rng.Chance(0.7);
        const ShadowSettings safe = SanitizeShadowSettings(settings);
        const std::uint32_t resolutions[4] = { 512, 1024, 2048, 4096 };

        D3 sun = rng.Direction();
        const double sunMode = rng.Uniform(0, 1);
        if (sunMode < 0.08) {
            sun = D3{ rng.Uniform(-1e-3, 1e-3), rng.Chance(0.5) ? 1.0 : -1.0, rng.Uniform(-1e-3, 1e-3) };
        } else if (sunMode < 0.16) {
            sun = D3{ sun.x, 0.0, sun.z };
            sun = Len3(sun) < 1e-3 ? D3{ 1, 0, 0 } : sun;
        } else if (sunMode < 0.22) {
            sun = ToD3(camera.frustum.forward) * (rng.Chance(0.5) ? 1.0 : -1.0);
        }
        sun = Unit3(sun);
        const Vec3 toSun(static_cast<float>(sun.x), static_cast<float>(sun.y), static_cast<float>(sun.z));

        std::optional<ShadowDepthRange> range;
        if (rng.Chance(0.5)) {
            const double shadowFar = std::min<double>(camera.frustum.viewFar, safe.maxDistance);
            if (shadowFar > camera.frustum.viewNear * 1.5) {
                const double a = rng.LogUniform(camera.frustum.viewNear, shadowFar * 0.9);
                const double b = std::min(shadowFar, a * rng.LogUniform(1.02, 40.0));
                range = ShadowDepthRange{ static_cast<float>(a), static_cast<float>(b) };
            }
        }
        const ShadowCascadeSet set = BuildShadowCascades(camera.frustum, toSun, safe, resolutions[rng.Int(0, 3)], range);
        if (!set.valid) {
            continue;
        }

        std::array<ShadowCasterPlanes, 4> bounds{};
        std::array<std::array<Plane, 6>, 4> boxPlanes{};
        std::array<std::array<Plane, 6>, 4> greedy{};
        for (std::uint32_t c = 0; c < set.count; ++c) {
            bounds[c] = BuildShadowCasterPlanes(camera.frustum, set, c, safe, static_cast<float>(viewWidth),
                static_cast<float>(viewHeight), 0.0f);
            boxPlanes[c] = ExtractFrustumPlanes(set.cascades[c].viewProjection);
            if (!bounds[c].valid) {
                continue;
            }
            std::vector<AABB> samples;
            for (int k = 0; k < 48; ++k) {
                const D3 p = OnAxis(camera, rng.Uniform(camera.frustum.viewNear, set.shadowDistance))
                    + rng.Direction() * rng.Uniform(0, 2.0 * set.cascades[c].radius);
                samples.push_back(PointBox(p));
            }
            greedy[c] = fx::ChooseCullPlanes(boxPlanes[c], bounds[c].planes, samples);
            ++usedLanes;
        }

        // Light axes (rows 0 and 1 of the light rotation) for the filter taps.
        const Vec3 lightForward = Normalize(-set.directionTowardSun);
        const Vec3 upHint = std::abs(Dot(lightForward, Vec3::Up())) > 0.999f ? Vec3::Forward() : Vec3::Up();
        const Mat4 lightRotation = Mat4::LookAtLH(Vec3::Zero(), lightForward, upHint);
        const D3 axisX{ lightRotation(0, 0), lightRotation(0, 1), lightRotation(0, 2) };
        const D3 axisY{ lightRotation(1, 0), lightRotation(1, 1), lightRotation(1, 2) };
        const D3 towardSun = ToD3(set.directionTowardSun);
        const D3 forward = ToD3(camera.frustum.forward);
        const D3 eyeD = ToD3(camera.frustum.eye);

        const auto clipOf = [&](std::uint32_t k, const D3& p) {
            const Mat4& m = set.cascades[k].viewProjection;
            double c4[4];
            for (int r = 0; r < 4; ++r) {
                c4[r] = m(r, 0) * p.x + m(r, 1) * p.y + m(r, 2) * p.z + m(r, 3);
            }
            return D3{ c4[0] / c4[3], c4[1] / c4[3], c4[2] / c4[3] };
        };
        const auto insideMap = [&](std::uint32_t k, const D3& p) {
            const D3 n = clipOf(k, p);
            return std::abs(n.x) <= 1.0 && std::abs(n.y) <= 1.0 && n.z >= 0.0 && n.z <= 1.0;
        };
        // The cascade box planes are float planes; a point at their very edge is not a fair test of the bound.
        const auto insideBoxShrunk = [&](std::uint32_t k, const D3& p) {
            const D3 n = clipOf(k, p);
            const double margin = (0.01 + 2e-6 * std::max({ std::abs(p.x), std::abs(p.y), std::abs(p.z) })) / set.cascades[k].radius;
            return std::abs(n.x) <= 1.0 - margin && std::abs(n.y) <= 1.0 - margin && n.z >= margin && n.z <= 1.0 - margin;
        };

        const auto check = [&](std::uint32_t lane, const D3& lookup) {
            if (!bounds[lane].valid) {
                return;
            }
            const double tmax = Dot3(lookup, ToD3(set.cascades[lane].lightForward)) - set.cascades[lane].lightNearZ;
            if (!(tmax >= 0.0)) {
                return;
            }
            const double sweeps[4] = { 0.0, tmax, rng.Uniform(0, tmax), rng.Uniform(0, tmax) };
            for (const double t : sweeps) {
                const D3 caster = lookup + towardSun * t;
                ++pointTests;
                falseRejects += Rejected(bounds[lane], caster) ? 1 : 0;
                if (insideBoxShrunk(lane, caster)) {
                    ++greedyTests;
                    greedyFalseRejects += IsAABBOutsideFrustum(greedy[lane], PointBox(caster)) ? 1 : 0;
                }
            }
        };

        for (int k = 0; k < kReceivers; ++k) {
            const double shadowDistance = set.shadowDistance;
            double depth = 0.0;
            const double mode = rng.Uniform(0, 1);
            if (mode < 0.5) {
                depth = rng.Uniform(camera.frustum.viewNear, shadowDistance);
            } else if (mode < 0.85) {
                const std::uint32_t pick = static_cast<std::uint32_t>(rng.Int(0, static_cast<int>(set.count) - 1));
                const double edge = rng.Chance(0.5) ? set.cascades[pick].sliceStart : set.cascades[pick].sliceEnd;
                depth = edge + rng.Uniform(-1, 1) * 0.02 * edge;
            } else {
                depth = rng.Chance(0.5) ? camera.frustum.viewNear + rng.Uniform(0, 0.05) * shadowDistance
                                         : shadowDistance - rng.Uniform(0, 0.05) * shadowDistance;
            }
            depth = std::clamp(depth, static_cast<double>(camera.frustum.viewNear), shadowDistance);

            // True camera rays (forward component 1), also up to one pixel outside the view (camera jitter).
            const double tanY = std::tan(0.5 * fov * kPi / 180.0);
            const double tanX = tanY * aspect;
            const D3 trueForward = Unit3(ToD3(target) - ToD3(eye));
            const D3 rightAxis = Unit3(Cross3(D3{ 0, 1, 0 }, trueForward));
            const D3 upAxis = Cross3(trueForward, rightAxis);
            const double u = rng.Chance(0.3) ? (rng.Chance(0.5) ? -1.0 : 1.0) : rng.Uniform(-1, 1);
            const double v = rng.Chance(0.3) ? (rng.Chance(0.5) ? -1.0 : 1.0) : rng.Uniform(-1, 1);
            const double uJitter = u + rng.Uniform(-1, 1) * 2.0 / viewWidth;
            const double vJitter = v + rng.Uniform(-1, 1) * 2.0 / viewHeight;
            const D3 ray = trueForward + rightAxis * (uJitter * tanX) + upAxis * (vJitter * tanY);
            const D3 receiver = eyeD + ray * depth;

            const double depthOfReceiver = Dot3(receiver - eyeD, forward);
            if (depthOfReceiver >= shadowDistance) {
                continue;
            }
            std::uint32_t pick = set.count - 1;
            for (std::uint32_t i = 0; i < set.count; ++i) {
                if (depthOfReceiver < set.cascades[i].sliceEnd) {
                    pick = i;
                    break;
                }
            }

            std::vector<MapRead> reads;
            const auto sampleCascade = [&](std::uint32_t c) {
                const double texel = set.cascades[c].texelWorldSize;
                const D3 offset = rng.Direction() * (safe.normalOffsetTexels * texel * (rng.Chance(0.5) ? 1.0 : rng.Uniform(0, 1)));
                const D3 lookup = receiver + offset;
                if (!insideMap(c, lookup)) {
                    return false;
                }
                const double tap = (safe.filterRadiusTexels + kTapTexels) * texel * (rng.Chance(0.5) ? 1.0 : rng.Uniform(0, 1));
                const double angle = rng.Uniform(0, 6.283185307179586);
                reads.push_back({ c, lookup });
                reads.push_back({ c, lookup + axisX * (tap * std::cos(angle)) + axisY * (tap * std::sin(angle)) });
                return true;
            };
            if (!sampleCascade(pick) && pick + 1 < set.count) {
                sampleCascade(pick + 1);
            }
            if (pick + 1 < set.count) {
                const double width = std::max(0.0, static_cast<double>(set.cascades[pick].sliceEnd) - set.cascades[pick + 1].sliceStart);
                if (width > 0.0 && depthOfReceiver > set.cascades[pick].sliceEnd - width) {
                    sampleCascade(pick + 1);
                }
            }
            for (const MapRead& read : reads) {
                check(read.cascade, read.point);
            }
        }
    }

    EXPECT_GT(usedLanes, 3000u);
    EXPECT_GT(pointTests, 500000u);
    EXPECT_GT(greedyTests, 100000u);
    EXPECT_EQ(falseRejects, 0u);
    EXPECT_EQ(greedyFalseRejects, 0u);
}

} // namespace
} // namespace gte
