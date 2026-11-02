// Unit tests for BendLimitFromPmxJoint and AssignBendLimits (src/Physics/JointBendLimit.h).

#include "Physics/JointBendLimit.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <unordered_map>

namespace gte {
namespace {

Joint MakeJoint(std::int32_t bodyA, std::int32_t bodyB, float limitDegreesXZ)
{
    Joint joint;
    joint.type = JointType::SpringDof6;
    joint.rigidBodyAIndex = bodyA;
    joint.rigidBodyBIndex = bodyB;
    const float r = DegToRad(limitDegreesXZ);
    joint.rotateLowerLimit = Vec3(-r, 0.0f, -r);
    joint.rotateUpperLimit = Vec3(r, 0.0f, r);
    return joint;
}

RigidBody MakeBody(std::int32_t boneIndex, RigidBodyMotionType type)
{
    RigidBody body;
    body.boneIndex = boneIndex;
    body.motionType = type;
    return body;
}

// Bones: 0 anchor, 1 -> 2 -> 3 a straight strand. Bodies 0..3 sit on bones 0..3.
struct StrandFixture {
    SkeletonData skeleton;
    PhysicsData physics;
    DynamicChainDefinition chain;
    std::unordered_map<std::int32_t, std::int32_t> boneToBody;

    StrandFixture()
    {
        for (int i = 0; i < 4; ++i) {
            Bone bone;
            bone.parentBoneIndex = i - 1;
            skeleton.bones.push_back(bone);
            physics.rigidBodies.push_back(MakeBody(i, i == 0 ? RigidBodyMotionType::Static : RigidBodyMotionType::Dynamic));
            boneToBody[i] = i;
        }
        chain.rootBoneIndex = 0;
        chain.jointBoneIndices = { 1, 2, 3 };
        chain.jointSettings.resize(3);
        chain.parentJointIndex = { -1, 0, 1 };
    }
};

} // namespace

TEST(JointBendLimitTests, LimitIsHypotOfXAndZ)
{
    EXPECT_NEAR(RadToDeg(BendLimitFromPmxJoint(MakeJoint(0, 1, 3.0f))), 4.2426f, 0.001f);

    Joint joint = MakeJoint(0, 1, 0.0f);
    joint.rotateLowerLimit = Vec3(DegToRad(-2.0f), DegToRad(-4.0f), DegToRad(-2.0f));
    joint.rotateUpperLimit = Vec3(DegToRad(2.0f), DegToRad(4.0f), DegToRad(2.0f));
    EXPECT_NEAR(RadToDeg(BendLimitFromPmxJoint(joint)), 2.828f, 0.001f);
}

TEST(JointBendLimitTests, AsymmetricLimitsUseTheLargerMagnitude)
{
    Joint joint = MakeJoint(0, 1, 0.0f);
    joint.rotateLowerLimit = Vec3(DegToRad(-3.0f), 0.0f, DegToRad(-1.0f));
    joint.rotateUpperLimit = Vec3(DegToRad(1.0f), 0.0f, DegToRad(4.0f));
    EXPECT_NEAR(RadToDeg(BendLimitFromPmxJoint(joint)), 5.0f, 0.001f);
}

TEST(JointBendLimitTests, LockedJointIsZero)
{
    EXPECT_EQ(BendLimitFromPmxJoint(MakeJoint(0, 1, 0.0f)), 0.0f);
}

TEST(JointBendLimitTests, LowerAboveUpperOrOtherTypeIsFree)
{
    Joint joint = MakeJoint(0, 1, 3.0f);
    joint.rotateLowerLimit.x = DegToRad(5.0f); // lower > upper on X.
    EXPECT_EQ(BendLimitFromPmxJoint(joint), kPi);

    joint = MakeJoint(0, 1, 3.0f);
    joint.type = JointType::P2P;
    EXPECT_EQ(BendLimitFromPmxJoint(joint), kPi);
}

TEST(JointBendLimitTests, HugeLimitIsCappedAtPi)
{
    EXPECT_EQ(BendLimitFromPmxJoint(MakeJoint(0, 1, 179.0f)), kPi);
}

TEST(JointBendLimitTests, EachJointGetsTheLimitOfItsOwnEdge)
{
    StrandFixture f;
    f.physics.joints = { MakeJoint(0, 1, 3.0f), MakeJoint(1, 2, 5.0f), MakeJoint(2, 3, 7.0f) };

    const std::vector<std::int32_t> pmx = AssignBendLimits(f.chain, f.skeleton, f.physics, f.boneToBody);

    ASSERT_EQ(pmx.size(), 3u);
    EXPECT_EQ(pmx[0], 0);
    EXPECT_EQ(pmx[1], 1);
    EXPECT_EQ(pmx[2], 2);
    EXPECT_NEAR(f.chain.jointSettings[0].bendLimitRadians, BendLimitFromPmxJoint(f.physics.joints[0]), 1e-6f);
    EXPECT_NEAR(f.chain.jointSettings[1].bendLimitRadians, BendLimitFromPmxJoint(f.physics.joints[1]), 1e-6f);
    EXPECT_NEAR(f.chain.jointSettings[2].bendLimitRadians, BendLimitFromPmxJoint(f.physics.joints[2]), 1e-6f);
}

TEST(JointBendLimitTests, ReversedJointOrderGivesTheSameResult)
{
    StrandFixture f;
    f.physics.joints = { MakeJoint(1, 0, 3.0f), MakeJoint(2, 1, 5.0f), MakeJoint(3, 2, 7.0f) };

    const std::vector<std::int32_t> pmx = AssignBendLimits(f.chain, f.skeleton, f.physics, f.boneToBody);

    EXPECT_EQ(pmx, (std::vector<std::int32_t>{ 0, 1, 2 }));
    EXPECT_NEAR(RadToDeg(f.chain.jointSettings[1].bendLimitRadians), 7.0711f, 0.001f);
}

TEST(JointBendLimitTests, RootChildSearchesEveryBodyOnTheAnchorBone)
{
    StrandFixture f;
    f.physics.rigidBodies.push_back(MakeBody(0, RigidBodyMotionType::Static)); // second static body on the anchor bone.
    f.physics.joints = { MakeJoint(4, 1, 3.0f), MakeJoint(1, 2, 5.0f), MakeJoint(2, 3, 7.0f) };

    const std::vector<std::int32_t> pmx = AssignBendLimits(f.chain, f.skeleton, f.physics, f.boneToBody);

    EXPECT_EQ(pmx[0], 0);
    EXPECT_NEAR(RadToDeg(f.chain.jointSettings[0].bendLimitRadians), 4.2426f, 0.001f);
}

TEST(JointBendLimitTests, MissingPmxJointIsFreeAndReported)
{
    StrandFixture f;
    f.physics.joints = { MakeJoint(0, 1, 3.0f), MakeJoint(2, 3, 7.0f) }; // nothing between bodies 1 and 2.

    const std::vector<std::int32_t> pmx = AssignBendLimits(f.chain, f.skeleton, f.physics, f.boneToBody);

    EXPECT_EQ(pmx[1], -1);
    EXPECT_EQ(f.chain.jointSettings[1].bendLimitRadians, kPi);
    EXPECT_EQ(pmx[0], 0);
    EXPECT_EQ(pmx[2], 1);
}

TEST(JointBendLimitTests, SeveralJointsForOnePairTakeTheSmallest)
{
    StrandFixture f;
    f.physics.joints = { MakeJoint(0, 1, 9.0f), MakeJoint(1, 0, 2.0f), MakeJoint(0, 1, 5.0f), MakeJoint(1, 2, 5.0f),
        MakeJoint(2, 3, 5.0f) };

    const std::vector<std::int32_t> pmx = AssignBendLimits(f.chain, f.skeleton, f.physics, f.boneToBody);

    EXPECT_EQ(pmx[0], 1);
    EXPECT_NEAR(RadToDeg(f.chain.jointSettings[0].bendLimitRadians), 2.8284f, 0.001f);
}

TEST(JointBendLimitTests, InvalidBodyIndicesAreSkipped)
{
    StrandFixture f;
    f.physics.joints = { MakeJoint(-1, 1, 3.0f), MakeJoint(1, 1, 3.0f), MakeJoint(0, 99, 3.0f) };

    const std::vector<std::int32_t> pmx = AssignBendLimits(f.chain, f.skeleton, f.physics, f.boneToBody);

    EXPECT_EQ(pmx, (std::vector<std::int32_t>{ -1, -1, -1 }));
}

} // namespace gte
