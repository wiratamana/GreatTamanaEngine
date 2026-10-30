// Unit tests for Quat (src/Math/Quat.h). RotatingForward*/RotatingRight*
// below double as the defining sanity check that this engine's rotation
// convention behaves like Unity's Quaternion.AngleAxis (see Quat.h).

#include "Math/Quat.h"
#include "Math/Mat4.h"

#include <gtest/gtest.h>

#include <cmath>

namespace gte {
namespace {

TEST(QuatTests, IdentityDoesNotRotateAVector)
{
    Vec3 v(1.0f, 2.0f, 3.0f);
    EXPECT_TRUE(ApproximatelyEqual(Quat::Identity().RotateVector(v), v));
}

// Matches Unity's Quaternion.AngleAxis(90, Vector3.up) * Vector3.forward
// == Vector3.right exactly (hand-verified via the RotateVector formula).
TEST(QuatTests, RotatingForwardByPositive90AroundUpGivesRight)
{
    Quat q = Quat::FromAxisAngle(Vec3::Up(), kHalfPi);
    Vec3 result = q.RotateVector(Vec3::Forward());
    EXPECT_TRUE(ApproximatelyEqual(result, Vec3::Right(), 1e-5f));
}

TEST(QuatTests, RotatingRightByPositive90AroundForwardGivesUp)
{
    Quat q = Quat::FromAxisAngle(Vec3::Forward(), kHalfPi);
    Vec3 result = q.RotateVector(Vec3::Right());
    EXPECT_TRUE(ApproximatelyEqual(result, Vec3::Up(), 1e-5f));
}

TEST(QuatTests, MultiplyOperatorMatchesRotateVectorComposition)
{
    Quat a = Quat::FromAxisAngle(Vec3::Up(), DegToRad(30.0f));
    Quat b = Quat::FromAxisAngle(Vec3::Right(), DegToRad(60.0f));
    Vec3 v(0.3f, 0.7f, -0.4f);

    Vec3 viaOperator = (a * b).RotateVector(v);
    Vec3 viaSequence = a.RotateVector(b.RotateVector(v));

    EXPECT_TRUE(ApproximatelyEqual(viaOperator, viaSequence, 1e-4f));
}

TEST(QuatTests, ConjugateOfUnitQuaternionIsItsInverse)
{
    Quat q = Normalize(Quat(0.1f, 0.2f, 0.3f, 0.9f));
    Quat shouldBeIdentity = q * q.Conjugate();
    EXPECT_TRUE(RepresentSameRotation(shouldBeIdentity, Quat::Identity(), 1e-4f));
}

TEST(QuatTests, ToMat4AndFromMat4RoundTrip)
{
    Quat original = Normalize(Quat::FromAxisAngle(Normalize(Vec3(1.0f, 2.0f, -1.0f)), DegToRad(73.0f)));
    Mat4 asMatrix = original.ToMat4();
    Quat reconstructed = Quat::FromMat4(asMatrix);

    // q and -q represent the identical rotation - compare rotations, not
    // raw components.
    EXPECT_TRUE(RepresentSameRotation(original, reconstructed, 1e-4f));
}

TEST(QuatTests, FromEulerAndToEulerRoundTripForNonDegenerateAngles)
{
    Quat original = Quat::FromEulerDegrees(30.0f, 45.0f, 60.0f);
    Vec3 euler = original.ToEulerDegrees();
    Quat reconstructed = Quat::FromEulerDegrees(euler.x, euler.y, euler.z);

    EXPECT_TRUE(RepresentSameRotation(original, reconstructed, 1e-3f));
}

// Rotation angle between two quaternions in degrees (stable near identity, unlike acos(dot)).
static float AngleBetweenDegrees(const Quat& a, const Quat& b)
{
    const Quat d = a.Inverse() * b;
    const float v = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    return RadToDeg(2.0f * std::atan2(v, std::fabs(d.w)));
}

TEST(QuatTests, ToEulerRoundTripSweepIncludingPitchPoles)
{
    int bad = 0;
    for (float x = -180.0f; x <= 180.0f; x += 15.0f) {
        for (float y = -180.0f; y <= 180.0f; y += 15.0f) {
            for (float z = -180.0f; z <= 180.0f; z += 15.0f) {
                const Quat q = Quat::FromEulerDegrees(x, y, z);
                const Vec3 e = q.ToEulerDegrees();
                if (AngleBetweenDegrees(q, Quat::FromEulerDegrees(e.x, e.y, e.z)) > 0.01f) {
                    ++bad;
                }
            }
        }
    }
    EXPECT_EQ(bad, 0);
}

TEST(QuatTests, ToEulerAtMinusNinetyPitchKeepsRotation)
{
    const Quat q = Quat::FromEulerDegrees(-90.0f, 30.0f, 40.0f);
    const Vec3 e = q.ToEulerDegrees();

    EXPECT_NEAR(e.x, -90.0f, 0.01f);
    EXPECT_NEAR(e.y, 70.0f, 0.05f);
    EXPECT_NEAR(e.z, 0.0f, 0.01f);
}

TEST(QuatTests, SlerpAtEndpointsReturnsInputs)
{
    Quat a = Quat::Identity();
    Quat b = Quat::FromAxisAngle(Vec3::Up(), kHalfPi);

    EXPECT_TRUE(RepresentSameRotation(Slerp(a, b, 0.0f), a, 1e-4f));
    EXPECT_TRUE(RepresentSameRotation(Slerp(a, b, 1.0f), b, 1e-4f));
}

TEST(QuatTests, SlerpAtHalfwayMatchesHalfTheAngleAxisRotation)
{
    // Both endpoints share the same rotation axis, so slerping between
    // them is exactly linear in angle - a safe, well-defined special case.
    Quat a = Quat::Identity();
    Quat b = Quat::FromAxisAngle(Vec3::Up(), kHalfPi);
    Quat halfway = Quat::FromAxisAngle(Vec3::Up(), kHalfPi * 0.5f);

    Quat slerped = Slerp(a, b, 0.5f);
    EXPECT_TRUE(ApproximatelyEqual(slerped.RotateVector(Vec3::Forward()), halfway.RotateVector(Vec3::Forward()), 1e-4f));
}

TEST(QuatTests, NlerpAtEndpointsReturnsInputs)
{
    Quat a = Quat::Identity();
    Quat b = Quat::FromAxisAngle(Vec3::Right(), DegToRad(40.0f));

    EXPECT_TRUE(RepresentSameRotation(Nlerp(a, b, 0.0f), a, 1e-4f));
    EXPECT_TRUE(RepresentSameRotation(Nlerp(a, b, 1.0f), b, 1e-4f));
}

} // namespace
} // namespace gte
