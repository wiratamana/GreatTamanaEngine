// Unit tests for RotationEulerEditCache (src/Editor/RotationEulerEditCache.h).

#include "Editor/RotationEulerEditCache.h"

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
    const Quat q = cache.Commit(Vec3(9999.0f, 0.0f, 0.0f));

    const Vec3 shown = cache.Resolve(kEntityA, q);

    EXPECT_FLOAT_EQ(shown.x, 9999.0f);
    EXPECT_FLOAT_EQ(shown.y, 0.0f);
    EXPECT_FLOAT_EQ(shown.z, 0.0f);
}

TEST(RotationEulerEditCacheTests, ExternalQuaternionChangeRederivesCanonicalEuler)
{
    RotationEulerEditCache cache;
    Quat q = cache.Commit(Vec3(120.0f, 0.0f, 0.0f));
    cache.Resolve(kEntityA, q);

    const Quat external = Quat::FromEulerDegrees(10.0f, 20.0f, 30.0f);
    const Vec3 shown = cache.Resolve(kEntityA, external);

    EXPECT_NEAR(shown.x, 10.0f, 1e-3f);
    EXPECT_NEAR(shown.y, 20.0f, 1e-3f);
    EXPECT_NEAR(shown.z, 30.0f, 1e-3f);
}

TEST(RotationEulerEditCacheTests, DifferentEntityRederivesCanonicalEuler)
{
    RotationEulerEditCache cache;
    const Quat q = cache.Commit(Vec3(9999.0f, 0.0f, 0.0f));
    cache.Resolve(kEntityA, q);

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
    Quat q = cache.Commit(Vec3(9999.0f, 0.0f, 0.0f));
    Vec3 shown = cache.Resolve(kEntityA, q);
    shown.y = 15.0f;
    q = cache.Commit(shown);

    shown = cache.Resolve(kEntityA, q);

    EXPECT_FLOAT_EQ(shown.x, 9999.0f);
    EXPECT_FLOAT_EQ(shown.y, 15.0f);
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

} // namespace
} // namespace gte
