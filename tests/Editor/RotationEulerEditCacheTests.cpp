// Unit tests for RotationEulerEditCache and RotationFieldProbe (src/Editor/).

#include "Editor/RotationEulerEditCache.h"
#include "Editor/RotationFieldProbe.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace gte {
namespace {

constexpr Entity kEntityA{ 1, 1 };
constexpr Entity kEntityB{ 2, 1 };

TEST(RotationEulerEditCacheTests, CommittedLargeAngleSurvivesResolve)
{
    RotationEulerEditCache cache;
    Vec3 edited = cache.Resolve(kEntityA, Quat::Identity());
    edited.x = 9999.0f;
    const Quat q = cache.Commit(edited);

    const Vec3 shown = cache.Resolve(kEntityA, q);

    EXPECT_FLOAT_EQ(shown.x, 9999.0f);
    EXPECT_FLOAT_EQ(shown.y, 0.0f);
    EXPECT_FLOAT_EQ(shown.z, 0.0f);
}

TEST(RotationEulerEditCacheTests, ExternalQuaternionChangeRederivesCanonicalEuler)
{
    RotationEulerEditCache cache;
    Vec3 edited = cache.Resolve(kEntityA, Quat::Identity());
    edited.x = 120.0f;
    cache.Commit(edited);

    const Quat external = Quat::FromEulerDegrees(10.0f, 20.0f, 30.0f);
    const Vec3 shown = cache.Resolve(kEntityA, external);

    EXPECT_NEAR(shown.x, 10.0f, 1e-3f);
    EXPECT_NEAR(shown.y, 20.0f, 1e-3f);
    EXPECT_NEAR(shown.z, 30.0f, 1e-3f);
}

TEST(RotationEulerEditCacheTests, DifferentEntityRederivesCanonicalEuler)
{
    RotationEulerEditCache cache;
    Vec3 edited = cache.Resolve(kEntityA, Quat::Identity());
    edited.x = 9999.0f;
    const Quat q = cache.Commit(edited);
    ASSERT_FLOAT_EQ(cache.Resolve(kEntityA, q).x, 9999.0f);

    const Vec3 shown = cache.Resolve(kEntityB, q);

    EXPECT_LE(std::fabs(shown.x), 90.01f);
}

TEST(RotationEulerEditCacheTests, JitterAroundNinetyKeepsYawAndRollZero)
{
    RotationEulerEditCache cache;
    Vec3 shown = cache.Resolve(kEntityA, Quat::Identity());
    shown.x = 90.0f;
    Quat q = cache.Commit(shown);

    for (int i = 0; i < 100; ++i) {
        shown = cache.Resolve(kEntityA, q);
        EXPECT_EQ(shown.y, 0.0f);
        EXPECT_EQ(shown.z, 0.0f);
        shown.x += (i % 2 == 0) ? 0.05f : -0.05f;
        q = cache.Commit(shown);
    }
}

TEST(RotationEulerEditCacheTests, EditingYKeepsLargeX)
{
    RotationEulerEditCache cache;
    Vec3 shown = cache.Resolve(kEntityA, Quat::Identity());
    shown.x = 9999.0f;
    Quat q = cache.Commit(shown);
    shown = cache.Resolve(kEntityA, q);
    shown.y = 15.0f;
    q = cache.Commit(shown);

    shown = cache.Resolve(kEntityA, q);

    EXPECT_FLOAT_EQ(shown.x, 9999.0f);
    EXPECT_FLOAT_EQ(shown.y, 15.0f);
}

TEST(RotationEulerEditCacheTests, IdleLargeAngleDoesNotRederive)
{
    RotationEulerEditCache cache;
    Vec3 shown = cache.Resolve(kEntityA, Quat::Identity());
    shown.x = 9999.0f;
    const Quat q = cache.Commit(shown);
    const std::uint32_t before = cache.rederiveCount;

    for (int i = 0; i < 100; ++i) {
        EXPECT_FLOAT_EQ(cache.Resolve(kEntityA, q).x, 9999.0f);
    }

    EXPECT_EQ(cache.rederiveCount, before);
}

TEST(RotationEulerEditCacheTests, RebindKeepsEulerWhenStoredQuaternionIsNormalized)
{
    RotationEulerEditCache cache;
    Quat q = cache.Commit(Vec3(9999.0f, 10.0f, 0.0f));
    q = Quat(q.x * 1.5f, q.y * 1.5f, q.z * 1.5f, q.w * 1.5f); // A writer scaled it; same rotation.
    cache.Rebind(q);

    const Vec3 shown = cache.Resolve(kInvalidEntity, q);

    EXPECT_FLOAT_EQ(shown.x, 9999.0f);
    EXPECT_FLOAT_EQ(shown.y, 10.0f);
}

TEST(RotationEulerEditCacheTests, NonFiniteAndHugeInputsAreSanitized)
{
    RotationEulerEditCache cache;
    const Quat q = cache.Commit(Vec3(1e30f, std::numeric_limits<float>::quiet_NaN(), -1e30f));

    EXPECT_FLOAT_EQ(cache.euler.x, RotationEulerEditCache::kMaxDegrees);
    EXPECT_FLOAT_EQ(cache.euler.y, 0.0f);
    EXPECT_FLOAT_EQ(cache.euler.z, -RotationEulerEditCache::kMaxDegrees);
    EXPECT_TRUE(std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w));
}

TEST(RotationFieldProbeTests, RecordsFieldCentersAndExpiresAfterOneFrame)
{
    RotationFieldProbe probe;
    EXPECT_FALSE(probe.Report(10, 0).drawn);

    probe.Record(10, kEntityA, Vec3(9999.0f, 0.0f, 0.0f), 100.0f, 50.0f, 306.0f, 4.0f);

    const RotationFieldReport report = probe.Report(11, 5);
    EXPECT_TRUE(report.drawn);
    EXPECT_EQ(report.rederiveCount, 5u);
    EXPECT_FLOAT_EQ(report.shown.x, 9999.0f);
    EXPECT_FLOAT_EQ(report.axisCenter[0].x, 149.5f); // Field width floor((306 - 8) / 3) = 99.
    EXPECT_FLOAT_EQ(report.axisCenter[1].x, 252.5f); // 100 + (99 + 4) + 99 / 2
    EXPECT_FLOAT_EQ(report.axisCenter[1].y, 50.0f);
    EXPECT_LT(report.axisCenter[1].x, report.axisCenter[2].x);
    EXPECT_FALSE(probe.Report(12, 5).drawn);
}

} // namespace
} // namespace gte
