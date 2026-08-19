#pragma once

#include "MathTypes.h"
#include "Vec3.h"

namespace gte {

struct Mat4; // fwd decl - see Quat.cpp for ToMat4()/FromMat4()

// Rotation quaternion, components (x, y, z, w) with the vector part first
// (matches GLM/Unity/Unreal layout) - Identity() is (0,0,0,1). Composition
// convention matches Mat4 (see Mat4.h): `p * q` means "apply q first, then
// p" - i.e. `(p * q) * v == p * (q * v)`.
//
// The rotation algebra itself (multiply, conjugate, slerp, axis-angle,
// matrix conversion) is ordinary handedness-agnostic math - it does not
// care which axis is "forward" (see MathTypes.h). Combined with this
// engine's Y-up/Z-forward/X-right axes (see Vec3.h), a positive-angle
// rotation behaves exactly like Unity's Quaternion.AngleAxis - e.g.
// rotating Vec3::Forward() by +90 degrees around Vec3::Up() yields
// Vec3::Right() (hand-verified in tests/Math/QuatTests.cpp).
// alignas(16) for the same future-SIMD-readiness reason as Vec4 (see
// Vec4.h) - Quat is likewise exactly 4 floats, so this doesn't change
// sizeof(Quat) or any public field/call site.
struct alignas(16) Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    constexpr Quat() noexcept = default;
    constexpr Quat(float x_, float y_, float z_, float w_) noexcept : x(x_), y(y_), z(z_), w(w_) {}

    static constexpr Quat Identity() noexcept { return Quat(0.0f, 0.0f, 0.0f, 1.0f); }

    // `angleRadians` follows the standard right-hand-rule sign convention
    // around `axis` - independent of the engine's left-handed world axes,
    // see MathTypes.h.
    static Quat FromAxisAngle(const Vec3& axis, float angleRadians) noexcept;

    // Composition order is Yaw(Y) * Pitch(X) * Roll(Z) - i.e. roll is
    // applied first (in the object's local space), then pitch, then yaw -
    // matching Unity's documented Quaternion.Euler application order (Z,
    // then X, then Y). Gimbal lock exists at pitch = +/-90 degrees, same as
    // every other Euler-angle representation - prefer storing/animating
    // rotations as Quat directly and only using Euler angles at the
    // human-facing edges (Editor fields, designer-authored data).
    static Quat FromEulerDegrees(float pitchX, float yawY, float rollZ) noexcept;
    Vec3 ToEulerDegrees() const noexcept; // returns (pitchX, yawY, rollZ)

    static Quat FromMat4(const Mat4& m) noexcept;
    Mat4 ToMat4() const noexcept;

    Quat Conjugate() const noexcept { return Quat(-x, -y, -z, w); }
    // Conjugate() scaled by 1/|q|^2 - handles non-unit-length input safely,
    // unlike just using Conjugate() directly (which is only the inverse
    // for a UNIT quaternion).
    Quat Inverse() const noexcept;

    Vec3 RotateVector(const Vec3& v) const noexcept;
};

Quat operator*(const Quat& a, const Quat& b) noexcept; // Hamilton product - see struct comment for composition order
Quat operator*(const Quat& q, float s) noexcept;
Quat operator+(const Quat& a, const Quat& b) noexcept;
Vec3 operator*(const Quat& q, const Vec3& v) noexcept; // shorthand for q.RotateVector(v)

float Dot(const Quat& a, const Quat& b) noexcept;
float LengthSquared(const Quat& q) noexcept;
float Length(const Quat& q) noexcept;
Quat Normalize(const Quat& q) noexcept;

// Spherical linear interpolation - the correct way to blend two rotations
// at a constant angular speed (e.g. animation blending). Falls back to a
// normalized linear interpolation (NLerp) when the two inputs are nearly
// identical, the standard way to avoid dividing by a near-zero sine there
// (see Quat.cpp).
Quat Slerp(const Quat& a, const Quat& b, float t) noexcept;

// Normalized linear interpolation - cheaper than Slerp, non-constant
// angular speed, but visually indistinguishable from it for small angles
// between a and b (e.g. per-frame damping/smoothing).
Quat Nlerp(const Quat& a, const Quat& b, float t) noexcept;

bool ApproximatelyEqual(const Quat& a, const Quat& b, float epsilon = kEpsilon) noexcept;

// q and -q represent the IDENTICAL rotation - use this instead of
// ApproximatelyEqual() when comparing rotations rather than raw components
// (e.g. in round-trip tests), otherwise a mathematically-correct-but-
// negated result looks like a failure.
bool RepresentSameRotation(const Quat& a, const Quat& b, float epsilon = kEpsilon) noexcept;

} // namespace gte
