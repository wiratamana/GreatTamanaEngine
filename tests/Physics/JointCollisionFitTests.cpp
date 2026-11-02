// Unit tests for FitJointCollisionToBindPose (src/Physics/JointCollisionFit.h). Hand-built chains, no ECS.

#include "Physics/JointCollisionFit.h"

#include <gtest/gtest.h>

#include <vector>

namespace gte {
namespace {

Collider MakeSphere(Vec3 center, float radius)
{
    Collider collider;
    collider.shape = ColliderShape::Sphere;
    collider.center = center;
    collider.size = Vec3(radius, 0.0f, 0.0f);
    return collider;
}

DynamicChainDefinition MakeChain(std::size_t jointCount, float collisionRadius)
{
    DynamicChainDefinition chain;
    chain.jointBoneIndices.resize(jointCount);
    chain.jointSettings.resize(jointCount);
    for (DynamicJointSettings& settings : chain.jointSettings) {
        settings.collisionRadius = collisionRadius;
    }
    return chain;
}

} // namespace

TEST(JointCollisionFitTests, SphereShrinksRadiusToTheSafetyFractionOfTheLargestFreeValue)
{
    DynamicChainDefinition chain = MakeChain(1, 0.2f);
    const std::vector<Vec3> bind = { Vec3(1.05f, 0.0f, 0.0f) };
    const std::vector<Collider> colliders = { MakeSphere(Vec3::Zero(), 1.0f) };

    FitJointCollisionToBindPose(chain, bind, colliders);

    EXPECT_NEAR(chain.jointSettings[0].collisionFitRadius, 0.05f * kCollisionFitSafety, 0.001f);
    EXPECT_GE(chain.jointSettings[0].collisionFitRadius, 0.0f);
    EXPECT_TRUE(chain.jointSettings[0].ignoredColliderIndices.empty());
}

TEST(JointCollisionFitTests, OriginInsideColliderIsIgnoredAndKeepsRadius)
{
    DynamicChainDefinition chain = MakeChain(1, 0.2f);
    const std::vector<Vec3> bind = { Vec3(0.5f, 0.0f, 0.0f) };
    const std::vector<Collider> colliders = { MakeSphere(Vec3::Zero(), 1.0f) };

    FitJointCollisionToBindPose(chain, bind, colliders);

    ASSERT_EQ(chain.jointSettings[0].ignoredColliderIndices.size(), 1u);
    EXPECT_EQ(chain.jointSettings[0].ignoredColliderIndices[0], 0);
    EXPECT_EQ(chain.jointSettings[0].collisionFitRadius, -1.0f);
}

TEST(JointCollisionFitTests, FarJointIsNotFitted)
{
    // A joint no collider touches keeps collisionFitRadius == -1 (the solver then uses collisionRadius).
    DynamicChainDefinition chain = MakeChain(1, 0.2f);
    const std::vector<Vec3> bind = { Vec3(3.0f, 0.0f, 0.0f) };
    const std::vector<Collider> colliders = { MakeSphere(Vec3::Zero(), 1.0f) };

    FitJointCollisionToBindPose(chain, bind, colliders);

    EXPECT_EQ(chain.jointSettings[0].collisionFitRadius, -1.0f);
    EXPECT_TRUE(chain.jointSettings[0].ignoredColliderIndices.empty());
}

TEST(JointCollisionFitTests, MaskMismatchLeavesJointUntouched)
{
    DynamicChainDefinition chain = MakeChain(1, 0.2f);
    chain.jointSettings[0].group = 1;
    chain.jointSettings[0].collisionMask = 0x0001; // does not accept group 2.
    Collider collider = MakeSphere(Vec3::Zero(), 1.0f);
    collider.group = 2;
    const std::vector<Vec3> bind = { Vec3(0.5f, 0.0f, 0.0f) };
    const std::vector<Collider> colliders = { collider };

    FitJointCollisionToBindPose(chain, bind, colliders);

    EXPECT_EQ(chain.jointSettings[0].collisionFitRadius, -1.0f);
    EXPECT_TRUE(chain.jointSettings[0].ignoredColliderIndices.empty());
}

TEST(JointCollisionFitTests, BoxFitsRadiusToFaceDistance)
{
    DynamicChainDefinition chain = MakeChain(1, 0.2f);
    Collider box;
    box.shape = ColliderShape::Box;
    box.size = Vec3(1.0f, 1.0f, 1.0f); // half extents.
    const std::vector<Vec3> bind = { Vec3(1.05f, 0.0f, 0.0f) };
    const std::vector<Collider> colliders = { box };

    FitJointCollisionToBindPose(chain, bind, colliders);

    EXPECT_NEAR(chain.jointSettings[0].collisionFitRadius, 0.05f * kCollisionFitSafety, 0.001f);
}

TEST(JointCollisionFitTests, CapsuleFitsRadiusToSurfaceDistance)
{
    DynamicChainDefinition chain = MakeChain(1, 0.2f);
    Collider capsule;
    capsule.shape = ColliderShape::Capsule;
    capsule.size = Vec3(1.0f, 2.0f, 0.0f); // radius, full height.
    const std::vector<Vec3> bind = { Vec3(1.05f, 0.0f, 0.0f) };
    const std::vector<Collider> colliders = { capsule };

    FitJointCollisionToBindPose(chain, bind, colliders);

    EXPECT_NEAR(chain.jointSettings[0].collisionFitRadius, 0.05f * kCollisionFitSafety, 0.001f);
}

TEST(JointCollisionFitTests, TwoCollidersTakeTheSmallerRadiusAndOnlyListFailedOrigins)
{
    DynamicChainDefinition chain = MakeChain(1, 0.2f);
    // Joint at x = 1.05: collider 0 allows 0.05, collider 1 (center at x = 2.2) allows 0.15, collider 2 holds the origin.
    const std::vector<Vec3> bind = { Vec3(1.05f, 0.0f, 0.0f) };
    const std::vector<Collider> colliders = {
        MakeSphere(Vec3::Zero(), 1.0f),
        MakeSphere(Vec3(2.2f, 0.0f, 0.0f), 1.0f),
        MakeSphere(Vec3(1.0f, 0.0f, 0.0f), 0.5f),
    };

    FitJointCollisionToBindPose(chain, bind, colliders);

    EXPECT_NEAR(chain.jointSettings[0].collisionFitRadius, 0.05f * kCollisionFitSafety, 0.001f);
    ASSERT_EQ(chain.jointSettings[0].ignoredColliderIndices.size(), 1u);
    EXPECT_EQ(chain.jointSettings[0].ignoredColliderIndices[0], 2);
}

TEST(JointCollisionFitTests, WrongSpanSizeDoesNothing)
{
    DynamicChainDefinition chain = MakeChain(2, 0.2f);
    const std::vector<Vec3> bind = { Vec3(0.5f, 0.0f, 0.0f) };
    const std::vector<Collider> colliders = { MakeSphere(Vec3::Zero(), 1.0f) };

    FitJointCollisionToBindPose(chain, bind, colliders);

    EXPECT_EQ(chain.jointSettings[0].collisionFitRadius, -1.0f);
    EXPECT_TRUE(chain.jointSettings[0].ignoredColliderIndices.empty());
}

} // namespace gte
