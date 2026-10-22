// Tier-1 tests for cluster candidate selection and the kit's cluster data types. No device involved.

#include "Core/FeatureKit/ClusterDrawTypes.h"

#include "Core/ClusterCandidates.h"
#include "Renderer/Culling/ClusterCullMath.h"

#include <gtest/gtest.h>

#include <limits>
#include <unordered_set>
#include <vector>

namespace gte {
namespace {

Entity MakeEntity(std::uint32_t index)
{
    Entity e;
    e.index = index;
    e.generation = 1;
    return e;
}

ClusterCandidateInput Eligible(std::uint32_t index)
{
    ClusterCandidateInput in;
    in.entity = MakeEntity(index);
    in.mesh = fx::MeshToken{ index, 1 };
    in.world = Mat4::Identity();
    in.meshHasClusters = true;
    in.meshHasIndexBuffer = true;
    in.pipelineUntextured = true;
    in.gpuSkinned = false;
    return in;
}

std::vector<fx::ClusterInstance> Select(const std::vector<ClusterCandidateInput>& inputs, bool caps = true)
{
    return SelectClusterCandidates(inputs, caps);
}

TEST(ClusterCandidatesTests, AllClausesMetGivesOneInstance)
{
    const auto result = Select({ Eligible(1) });
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].entity, MakeEntity(1));
    EXPECT_EQ(result[0].mesh, (fx::MeshToken{ 1, 1 }));
    EXPECT_FLOAT_EQ(result[0].maxScale, 1.0f);
    EXPECT_EQ(result[0].flags, kClusterFlagUniformScale | kClusterFlagPositiveHand);
}

TEST(ClusterCandidatesTests, EachClauseAloneExcludes)
{
    ClusterCandidateInput noClusters = Eligible(1);
    noClusters.meshHasClusters = false;
    ClusterCandidateInput noIndex = Eligible(2);
    noIndex.meshHasIndexBuffer = false;
    ClusterCandidateInput textured = Eligible(3);
    textured.pipelineUntextured = false;
    ClusterCandidateInput skinned = Eligible(4);
    skinned.gpuSkinned = true;

    EXPECT_TRUE(Select({ noClusters }).empty());
    EXPECT_TRUE(Select({ noIndex }).empty());
    EXPECT_TRUE(Select({ textured }).empty());
    EXPECT_TRUE(Select({ skinned }).empty());
}

TEST(ClusterCandidatesTests, MissingCapsGiveNothing)
{
    EXPECT_TRUE(Select({ Eligible(1), Eligible(2) }, false).empty());
}

TEST(ClusterCandidatesTests, NonFiniteWorldMatrixIsNotACandidate)
{
    ClusterCandidateInput nan = Eligible(1);
    nan.world(0, 3) = std::numeric_limits<float>::quiet_NaN();
    ClusterCandidateInput inf = Eligible(2);
    inf.world(1, 1) = std::numeric_limits<float>::infinity();

    const auto result = Select({ nan, Eligible(3), inf });
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].entity, MakeEntity(3));
}

TEST(ClusterCandidatesTests, ZeroMatrixIsKeptWithNoFlags)
{
    ClusterCandidateInput zero = Eligible(1);
    zero.world = Mat4::Zero();
    const auto result = Select({ zero });
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].flags, 0u);
    EXPECT_FLOAT_EQ(result[0].maxScale, 0.0f);
}

TEST(ClusterCandidatesTests, ZeroLengthColumnIsNotUniform)
{
    ClusterCandidateInput in = Eligible(1);
    in.world = Mat4::Scaling(Vec3(0.0f, 1.0f, 1.0f));
    const auto result = Select({ in });
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].flags & kClusterFlagUniformScale, 0u);
}

TEST(ClusterCandidatesTests, UniformScaleToleranceIsRelative)
{
    ClusterCandidateInput inside = Eligible(1);
    inside.world = Mat4::Scaling(Vec3(1.0f, 1.0005f, 1.0f));
    ClusterCandidateInput outside = Eligible(2);
    outside.world = Mat4::Scaling(Vec3(1.0f, 1.002f, 1.0f));
    ClusterCandidateInput big = Eligible(3);
    big.world = Mat4::Scaling(Vec3(1000.0f, 1000.5f, 1000.0f));

    const auto result = Select({ inside, outside, big });
    ASSERT_EQ(result.size(), 3u);
    EXPECT_NE(result[0].flags & kClusterFlagUniformScale, 0u);
    EXPECT_EQ(result[1].flags & kClusterFlagUniformScale, 0u);
    EXPECT_NE(result[2].flags & kClusterFlagUniformScale, 0u);
    EXPECT_FLOAT_EQ(result[2].maxScale, 1000.5f);
}

TEST(ClusterCandidatesTests, MirroredMatrixLosesPositiveHand)
{
    ClusterCandidateInput in = Eligible(1);
    in.world = Mat4::Scaling(Vec3(-1.0f, 1.0f, 1.0f));
    const auto result = Select({ in });
    ASSERT_EQ(result.size(), 1u);
    EXPECT_NE(result[0].flags & kClusterFlagUniformScale, 0u);
    EXPECT_EQ(result[0].flags & kClusterFlagPositiveHand, 0u);
}

TEST(ClusterCandidatesTests, OrderFollowsInput)
{
    const auto result = Select({ Eligible(5), Eligible(2), Eligible(9) });
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0].entity, MakeEntity(5));
    EXPECT_EQ(result[1].entity, MakeEntity(2));
    EXPECT_EQ(result[2].entity, MakeEntity(9));
}

TEST(ClusterCandidatesTests, ClusterCameraOfPerspective)
{
    fx::View view;
    view.viewProjection = Mat4::PerspectiveFovLH_ZO(1.0f, 1.5f, 0.1f, 100.0f);
    view.eye = Vec3(1.0f, 2.0f, 3.0f);
    const fx::ClusterCamera camera = fx::ClusterCameraOf(view);
    EXPECT_TRUE(camera.valid);
    EXPECT_FALSE(camera.orthographic);
    EXPECT_FLOAT_EQ(camera.eyeOrDirection.x, 1.0f);
    EXPECT_FLOAT_EQ(camera.eyeOrDirection.y, 2.0f);
    EXPECT_FLOAT_EQ(camera.eyeOrDirection.z, 3.0f);
}

TEST(ClusterCandidatesTests, ClusterCameraOfOrthographicUsesDirection)
{
    fx::View view;
    view.viewProjection = Mat4::OrthographicLH_ZO(-5.0f, 5.0f, -5.0f, 5.0f, 0.1f, 50.0f);
    view.eye = Vec3(1.0f, 2.0f, 3.0f);
    const fx::ClusterCamera camera = fx::ClusterCameraOf(view);
    EXPECT_TRUE(camera.valid);
    EXPECT_TRUE(camera.orthographic);
    const Vec3 d = camera.eyeOrDirection;
    EXPECT_NEAR(d.x * d.x + d.y * d.y + d.z * d.z, 1.0f, 1e-4f);
}

TEST(ClusterCandidatesTests, ClusterCameraOfNonFiniteIsInvalid)
{
    fx::View view;
    view.viewProjection = Mat4::Identity();
    view.viewProjection(0, 0) = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(fx::ClusterCameraOf(view).valid);

    fx::View badEye;
    badEye.viewProjection = Mat4::PerspectiveFovLH_ZO(1.0f, 1.0f, 0.1f, 10.0f);
    badEye.eye = Vec3(std::numeric_limits<float>::infinity(), 0.0f, 0.0f);
    EXPECT_FALSE(fx::ClusterCameraOf(badEye).valid);
}

TEST(ClusterCandidatesTests, MeshTokenEqualityOrderAndHash)
{
    const fx::MeshToken a{ 1, 2 };
    const fx::MeshToken b{ 1, 2 };
    const fx::MeshToken c{ 1, 3 };
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_LT(a, c);
    EXPECT_EQ(fx::MeshTokenHash{}(a), fx::MeshTokenHash{}(b));
    std::unordered_set<fx::MeshToken, fx::MeshTokenHash> set{ a, b, c };
    EXPECT_EQ(set.size(), 2u);
}

} // namespace
} // namespace gte
