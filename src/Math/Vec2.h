#pragma once

#include "MathTypes.h"

namespace gte {

// Plain 2D vector - mainly for UVs/screen-space values. See MathTypes.h for
// this library's overall conventions.
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() noexcept = default;
    constexpr Vec2(float x_, float y_) noexcept : x(x_), y(y_) {}

    constexpr float& operator[](int i) noexcept { return i == 0 ? x : y; }
    constexpr float operator[](int i) const noexcept { return i == 0 ? x : y; }

    friend constexpr Vec2 operator+(const Vec2& a, const Vec2& b) noexcept { return {a.x + b.x, a.y + b.y}; }
    friend constexpr Vec2 operator-(const Vec2& a, const Vec2& b) noexcept { return {a.x - b.x, a.y - b.y}; }
    friend constexpr Vec2 operator-(const Vec2& a) noexcept { return {-a.x, -a.y}; }
    // Component-wise (GLSL-style) - NOT a dot product, see Dot() below.
    friend constexpr Vec2 operator*(const Vec2& a, const Vec2& b) noexcept { return {a.x * b.x, a.y * b.y}; }
    friend constexpr Vec2 operator*(const Vec2& a, float s) noexcept { return {a.x * s, a.y * s}; }
    friend constexpr Vec2 operator*(float s, const Vec2& a) noexcept { return a * s; }
    friend constexpr Vec2 operator/(const Vec2& a, float s) noexcept { return {a.x / s, a.y / s}; }

    Vec2& operator+=(const Vec2& o) noexcept { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) noexcept { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) noexcept { x *= s; y *= s; return *this; }
    Vec2& operator/=(float s) noexcept { x /= s; y /= s; return *this; }

    friend constexpr bool operator==(const Vec2& a, const Vec2& b) noexcept { return a.x == b.x && a.y == b.y; }
    friend constexpr bool operator!=(const Vec2& a, const Vec2& b) noexcept { return !(a == b); }

    // Implemented as functions rather than static const data members to
    // sidestep the static-initialization-order fiasco across translation
    // units - the same reason Unity's Vector2.zero/one are properties, not
    // fields.
    static constexpr Vec2 Zero() noexcept { return {0.0f, 0.0f}; }
    static constexpr Vec2 One() noexcept { return {1.0f, 1.0f}; }
};

constexpr float Dot(const Vec2& a, const Vec2& b) noexcept { return a.x * b.x + a.y * b.y; }
inline float LengthSquared(const Vec2& v) noexcept { return Dot(v, v); }
inline float Length(const Vec2& v) noexcept { return std::sqrt(LengthSquared(v)); }

// Safe normalize: returns Zero() instead of dividing by (near) zero for a
// degenerate input, rather than producing NaN/Inf.
inline Vec2 Normalize(const Vec2& v) noexcept
{
    float len = Length(v);
    return len > kEpsilon ? v / len : Vec2::Zero();
}

inline Vec2 Lerp(const Vec2& a, const Vec2& b, float t) noexcept { return a + (b - a) * t; }

inline bool ApproximatelyEqual(const Vec2& a, const Vec2& b, float epsilon = kEpsilon) noexcept
{
    return ApproximatelyEqual(a.x, b.x, epsilon) && ApproximatelyEqual(a.y, b.y, epsilon);
}

} // namespace gte
