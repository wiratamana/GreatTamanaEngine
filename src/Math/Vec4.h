#pragma once

#include "MathTypes.h"
#include "Vec3.h"

namespace gte {

// Plain 4D vector - mainly used as Mat4's column type and for homogeneous
// points/directions (w=1/w=0) and clip-space results (see Mat4.h).
// alignas(16): matches SSE/NEON's 128-bit register width, so a future SIMD
// implementation can reinterpret this struct's storage as a single
// __m128/float32x4_t via a plain aligned load/store - see MathTypes.h.
// Does NOT change sizeof(Vec4) (already 16 bytes = 4 floats) or any public
// field/call site - purely an alignment guarantee, free today.
struct alignas(16) Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    constexpr Vec4() noexcept = default;
    constexpr Vec4(float x_, float y_, float z_, float w_) noexcept : x(x_), y(y_), z(z_), w(w_) {}
    constexpr Vec4(const Vec3& v, float w_) noexcept : x(v.x), y(v.y), z(v.z), w(w_) {}

    constexpr float& operator[](int i) noexcept { return i == 0 ? x : (i == 1 ? y : (i == 2 ? z : w)); }
    constexpr float operator[](int i) const noexcept { return i == 0 ? x : (i == 1 ? y : (i == 2 ? z : w)); }

    constexpr Vec3 XYZ() const noexcept { return Vec3(x, y, z); }

    friend constexpr Vec4 operator+(const Vec4& a, const Vec4& b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
    friend constexpr Vec4 operator-(const Vec4& a, const Vec4& b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
    friend constexpr Vec4 operator-(const Vec4& a) noexcept { return {-a.x, -a.y, -a.z, -a.w}; }
    // Component-wise (GLSL-style) - NOT a dot product, see Dot() below.
    friend constexpr Vec4 operator*(const Vec4& a, const Vec4& b) noexcept { return {a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w}; }
    friend constexpr Vec4 operator*(const Vec4& a, float s) noexcept { return {a.x * s, a.y * s, a.z * s, a.w * s}; }
    friend constexpr Vec4 operator*(float s, const Vec4& a) noexcept { return a * s; }
    friend constexpr Vec4 operator/(const Vec4& a, float s) noexcept { return {a.x / s, a.y / s, a.z / s, a.w / s}; }

    Vec4& operator+=(const Vec4& o) noexcept { x += o.x; y += o.y; z += o.z; w += o.w; return *this; }
    Vec4& operator-=(const Vec4& o) noexcept { x -= o.x; y -= o.y; z -= o.z; w -= o.w; return *this; }
    Vec4& operator*=(float s) noexcept { x *= s; y *= s; z *= s; w *= s; return *this; }

    friend constexpr bool operator==(const Vec4& a, const Vec4& b) noexcept { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }
    friend constexpr bool operator!=(const Vec4& a, const Vec4& b) noexcept { return !(a == b); }

    static constexpr Vec4 Zero() noexcept { return {0.0f, 0.0f, 0.0f, 0.0f}; }
    static constexpr Vec4 One() noexcept { return {1.0f, 1.0f, 1.0f, 1.0f}; }
};

constexpr float Dot(const Vec4& a, const Vec4& b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
inline float LengthSquared(const Vec4& v) noexcept { return Dot(v, v); }
inline float Length(const Vec4& v) noexcept { return std::sqrt(LengthSquared(v)); }

inline Vec4 Normalize(const Vec4& v) noexcept
{
    float len = Length(v);
    return len > kEpsilon ? v / len : Vec4::Zero();
}

inline Vec4 Lerp(const Vec4& a, const Vec4& b, float t) noexcept { return a + (b - a) * t; }

inline bool ApproximatelyEqual(const Vec4& a, const Vec4& b, float epsilon = kEpsilon) noexcept
{
    return ApproximatelyEqual(a.x, b.x, epsilon)
        && ApproximatelyEqual(a.y, b.y, epsilon)
        && ApproximatelyEqual(a.z, b.z, epsilon)
        && ApproximatelyEqual(a.w, b.w, epsilon);
}

} // namespace gte
