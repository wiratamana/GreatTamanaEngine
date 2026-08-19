#pragma once

#include "MathTypes.h"

namespace gte {

// Coordinate convention: LEFT-HANDED, Y-up, Z-forward, X-right (see
// MathTypes.h). Right()/Up()/Forward() etc. below are the canonical unit
// axes - prefer them over hand-writing (1,0,0)/(0,1,0)/(0,0,1) so intent
// reads clearly at call sites (matches Unity's Vector3.right/up/forward).
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3() noexcept = default;
    constexpr Vec3(float x_, float y_, float z_) noexcept : x(x_), y(y_), z(z_) {}

    constexpr float& operator[](int i) noexcept { return i == 0 ? x : (i == 1 ? y : z); }
    constexpr float operator[](int i) const noexcept { return i == 0 ? x : (i == 1 ? y : z); }

    friend constexpr Vec3 operator+(const Vec3& a, const Vec3& b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    friend constexpr Vec3 operator-(const Vec3& a, const Vec3& b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    friend constexpr Vec3 operator-(const Vec3& a) noexcept { return {-a.x, -a.y, -a.z}; }
    // Component-wise (GLSL-style) - NOT a dot product, see Dot() below.
    friend constexpr Vec3 operator*(const Vec3& a, const Vec3& b) noexcept { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
    friend constexpr Vec3 operator*(const Vec3& a, float s) noexcept { return {a.x * s, a.y * s, a.z * s}; }
    friend constexpr Vec3 operator*(float s, const Vec3& a) noexcept { return a * s; }
    friend constexpr Vec3 operator/(const Vec3& a, float s) noexcept { return {a.x / s, a.y / s, a.z / s}; }

    Vec3& operator+=(const Vec3& o) noexcept { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) noexcept { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) noexcept { x *= s; y *= s; z *= s; return *this; }
    Vec3& operator/=(float s) noexcept { x /= s; y /= s; z /= s; return *this; }

    friend constexpr bool operator==(const Vec3& a, const Vec3& b) noexcept { return a.x == b.x && a.y == b.y && a.z == b.z; }
    friend constexpr bool operator!=(const Vec3& a, const Vec3& b) noexcept { return !(a == b); }

    // Implemented as functions rather than static const data members to
    // sidestep the static-initialization-order fiasco across translation
    // units - the same reason Unity's Vector3.right/up/forward are
    // properties, not fields.
    static constexpr Vec3 Zero() noexcept { return {0.0f, 0.0f, 0.0f}; }
    static constexpr Vec3 One() noexcept { return {1.0f, 1.0f, 1.0f}; }
    static constexpr Vec3 Right() noexcept { return {1.0f, 0.0f, 0.0f}; }
    static constexpr Vec3 Left() noexcept { return {-1.0f, 0.0f, 0.0f}; }
    static constexpr Vec3 Up() noexcept { return {0.0f, 1.0f, 0.0f}; }
    static constexpr Vec3 Down() noexcept { return {0.0f, -1.0f, 0.0f}; }
    static constexpr Vec3 Forward() noexcept { return {0.0f, 0.0f, 1.0f}; }
    static constexpr Vec3 Back() noexcept { return {0.0f, 0.0f, -1.0f}; }
};

constexpr float Dot(const Vec3& a, const Vec3& b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }

// Standard right-hand-rule cross product formula. With this engine's
// Right()/Up()/Forward() axes (X/Y/Z respectively), Cross(Right(), Up())
// == Forward() - matches Unity's Vector3.Cross(right, up) == forward
// exactly, even though the overall coordinate system is left-handed
// (handedness only changes how LookAtLH/PerspectiveFovLH_ZO are built -
// see Mat4.h - not this formula; see tests/Math/Vec3Tests.cpp).
constexpr Vec3 Cross(const Vec3& a, const Vec3& b) noexcept
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

inline float LengthSquared(const Vec3& v) noexcept { return Dot(v, v); }
inline float Length(const Vec3& v) noexcept { return std::sqrt(LengthSquared(v)); }

// Safe normalize: returns Zero() instead of dividing by (near) zero for a
// degenerate input, rather than producing NaN/Inf.
inline Vec3 Normalize(const Vec3& v) noexcept
{
    float len = Length(v);
    return len > kEpsilon ? v / len : Vec3::Zero();
}

inline Vec3 Lerp(const Vec3& a, const Vec3& b, float t) noexcept { return a + (b - a) * t; }

inline bool ApproximatelyEqual(const Vec3& a, const Vec3& b, float epsilon = kEpsilon) noexcept
{
    return ApproximatelyEqual(a.x, b.x, epsilon)
        && ApproximatelyEqual(a.y, b.y, epsilon)
        && ApproximatelyEqual(a.z, b.z, epsilon);
}

} // namespace gte
