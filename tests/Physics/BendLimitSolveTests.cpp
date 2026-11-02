// Unit tests for SolveBendLimit (src/Physics/ChainConstraints.h).

#include "Physics/ChainConstraints.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace gte {
namespace {

float AngleDegrees(const Vec3& a, const Vec3& b)
{
    return RadToDeg(std::acos(Clamp(Dot(Normalize(a), Normalize(b)), -1.0f, 1.0f)));
}

VerletParticle MakeChild(const Vec3& position)
{
    VerletParticle child;
    child.position = position;
    child.previousPosition = position;
    return child;
}

const Vec3 kParent = Vec3::Zero();
const Vec3 kChildAnim(1.0f, 0.0f, 0.0f); // animated direction +X, length 1.

} // namespace

TEST(BendLimitSolveTests, ZeroLimitPutsChildOnTheAnimatedDirectionKeepingLength)
{
    VerletParticle child = MakeChild(Vec3(0.0f, 2.0f, 0.0f));

    EXPECT_TRUE(SolveBendLimit(child, kParent, kParent, kChildAnim, 0.0f));

    EXPECT_NEAR(child.position.x, 2.0f, 1e-5f);
    EXPECT_NEAR(child.position.y, 0.0f, 1e-5f);
    EXPECT_NEAR(child.position.z, 0.0f, 1e-5f);
}

TEST(BendLimitSolveTests, ChildIsClampedToTheConeBoundaryInThePlane)
{
    VerletParticle child = MakeChild(Vec3(0.0f, 1.5f, 0.0f)); // 90 degrees from +X.

    EXPECT_TRUE(SolveBendLimit(child, kParent, kParent, kChildAnim, DegToRad(30.0f)));

    EXPECT_NEAR(AngleDegrees(child.position, Vec3(1.0f, 0.0f, 0.0f)), 30.0f, 0.01f);
    EXPECT_NEAR(Length(child.position), 1.5f, 1e-5f);
    EXPECT_NEAR(child.position.z, 0.0f, 1e-5f); // stays in the plane of both directions.
    EXPECT_GT(child.position.y, 0.0f);
}

TEST(BendLimitSolveTests, ChildInsideTheConeIsUnchanged)
{
    const Vec3 start(1.0f, 0.1f, 0.0f);
    VerletParticle child = MakeChild(start);

    EXPECT_FALSE(SolveBendLimit(child, kParent, kParent, kChildAnim, DegToRad(30.0f)));

    EXPECT_EQ(child.position, start);
}

TEST(BendLimitSolveTests, FreeLimitPinnedChildAndZeroAnimatedSegmentChangeNothing)
{
    const Vec3 start(0.0f, 2.0f, 0.0f);

    VerletParticle freeChild = MakeChild(start);
    EXPECT_FALSE(SolveBendLimit(freeChild, kParent, kParent, kChildAnim, kPi));
    EXPECT_EQ(freeChild.position, start);

    VerletParticle pinned = MakeChild(start);
    pinned.pinned = true;
    EXPECT_FALSE(SolveBendLimit(pinned, kParent, kParent, kChildAnim, 0.0f));
    EXPECT_EQ(pinned.position, start);

    VerletParticle degenerate = MakeChild(start);
    EXPECT_FALSE(SolveBendLimit(degenerate, kParent, kParent, kParent, 0.0f));
    EXPECT_EQ(degenerate.position, start);
}

TEST(BendLimitSolveTests, OppositeDirectionGivesFiniteResultAtTheLimit)
{
    VerletParticle child = MakeChild(Vec3(-2.0f, 0.0f, 0.0f)); // exactly opposite of +X.

    EXPECT_TRUE(SolveBendLimit(child, kParent, kParent, kChildAnim, DegToRad(45.0f)));

    EXPECT_TRUE(std::isfinite(child.position.x) && std::isfinite(child.position.y) && std::isfinite(child.position.z));
    EXPECT_NEAR(AngleDegrees(child.position, Vec3(1.0f, 0.0f, 0.0f)), 45.0f, 0.01f);
    EXPECT_NEAR(Length(child.position), 2.0f, 1e-5f);
}

TEST(BendLimitSolveTests, ChildOnTheParentIsPlacedAlongTheAnimatedSegment)
{
    VerletParticle child = MakeChild(Vec3(0.0f, 0.0f, 0.0f));

    EXPECT_TRUE(SolveBendLimit(child, kParent, kParent, Vec3(0.0f, 3.0f, 0.0f), DegToRad(10.0f)));

    EXPECT_NEAR(child.position.y, 3.0f, 1e-5f);
}

TEST(BendLimitSolveTests, ParentMovesTheConeApexWithIt)
{
    // Simulated parent is displaced; the cone direction still comes from the animated segment.
    const Vec3 parentSim(5.0f, 5.0f, 0.0f);
    VerletParticle child = MakeChild(Vec3(5.0f, 7.0f, 0.0f));

    EXPECT_TRUE(SolveBendLimit(child, parentSim, kParent, kChildAnim, 0.0f));

    EXPECT_NEAR(child.position.x, 7.0f, 1e-5f);
    EXPECT_NEAR(child.position.y, 5.0f, 1e-5f);
}

} // namespace gte
