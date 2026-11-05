#pragma once

#include "MathTypes.h"
#include "Vec3.h"
#include "Vec4.h"

#if defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace gte {

struct Quat; // fwd decl - see Quat.h/.cpp for the quaternion<->matrix conversions

// 4x4 matrix, COLUMN-MAJOR storage (columns[c][r] is column c, row r) -
// bit-identical to a GLSL/HLSL `mat4` uniform's expected memory layout, so
// Data() can be uploaded to a Vulkan uniform/push-constant buffer with zero
// transpose step (see Renderer/Buffer.h). Multiplication convention is
// COLUMN-VECTOR: a transform is applied as `M * v`, and composing two
// transforms `A * B` means "apply B first, then A" - i.e.
// `(A * B) * v == A * (B * v)`, so a child's world matrix is
// `parentWorld * localTransform`. This matches Unity/GLM; it deliberately
// does NOT match Unreal's FMatrix, which is row-major and uses `v * M` -
// column-major/column-vector was chosen here purely so this type uploads to
// our Vulkan/GLSL pipeline as-is, no silent conversion - the same "match
// the GPU side exactly" philosophy AGENTS.md already applies to render
// target formats, just applied to matrices instead.
//
// Coordinate system: LEFT-HANDED, Y-up, Z-forward, X-right (see
// MathTypes.h). Most of what's below (multiply, transpose, inverse,
// translate/scale/rotate builders) is ordinary handedness-agnostic linear
// algebra - the genuinely LEFT-HANDED-specific pieces are LookAtLH and
// PerspectiveFovLH_ZO/OrthographicLH_ZO, called out individually below.
// alignas(16) here is technically redundant (each column is already
// alignas(16) - see Vec4.h - so Mat4 inherits that automatically), stated
// explicitly for clarity: a future SIMD Mat4*Mat4/Mat4*Vec4 implementation
// can load/store each column as one __m128/float32x4_t register with zero
// change to this struct's layout, Data(), or any existing call site.
struct alignas(16) Mat4 {
    Vec4 columns[4];

    Mat4() noexcept = default; // all-zero, NOT identity - see Identity() below
    Mat4(const Vec4& c0, const Vec4& c1, const Vec4& c2, const Vec4& c3) noexcept
        : columns{c0, c1, c2, c3} {}

    float& operator()(int row, int col) noexcept { return columns[col][row]; }
    float operator()(int row, int col) const noexcept { return columns[col][row]; }

    // Contiguous column-major float[16], suitable for direct upload to a
    // GLSL `mat4` uniform - see class comment above.
    const float* Data() const noexcept { return &columns[0].x; }

    static Mat4 Identity() noexcept;
    static Mat4 Zero() noexcept { return Mat4(); }

    static Mat4 Translation(const Vec3& t) noexcept;
    static Mat4 Scaling(const Vec3& s) noexcept;
    static Mat4 FromQuat(const Quat& q) noexcept;

    // Translate * Rotate * Scale, in that order - the standard local
    // (parent-space) transform for position/rotation/scale, matching
    // Unity/Unreal's Transform component semantics.
    static Mat4 TRS(const Vec3& position, const Quat& rotation, const Vec3& scale) noexcept;

    // LEFT-HANDED view matrix: transforms WORLD space into VIEW space,
    // where +Z is "in front of the camera" (matching this engine's
    // Z-forward world convention) - NOT OpenGL's -Z-forward view space.
    static Mat4 LookAtLH(const Vec3& eye, const Vec3& target, const Vec3& up) noexcept;

    // LEFT-HANDED, ZERO-TO-ONE depth range perspective projection - matches
    // Vulkan's expected clip-space depth range ([0,1], not OpenGL's
    // [-1,1]). `flipY`, when true, negates the Y scale term to also correct
    // for Vulkan's clip-space Y axis being flipped relative to OpenGL's -
    // the alternative (and equally valid) fix is a negative-height viewport
    // at draw time instead; pick exactly one, never both, or the image
    // ends up right-side up again by accident and masks the other bug.
    static Mat4 PerspectiveFovLH_ZO(float fovYRadians, float aspect, float nearZ, float farZ, bool flipY = false) noexcept;

    // LEFT-HANDED, ZERO-TO-ONE depth range orthographic projection. Same
    // `flipY` caveat as PerspectiveFovLH_ZO above.
    static Mat4 OrthographicLH_ZO(float left, float right, float bottom, float top, float nearZ, float farZ, bool flipY = false) noexcept;

    Mat4 Transposed() const noexcept;

    // Cofactor-expansion determinant (the same routine TryInverse() uses).
    float Determinant() const noexcept;

    // Returns Identity() and asserts (debug builds) when singular. Use TryInverse() if that can happen legitimately.
    Mat4 Inverse() const noexcept;

    // False when the determinant is rounding noise relative to its own terms (scale independent), or not finite.
    bool TryInverse(Mat4& outInverse) const noexcept;

    // w=1: applies translation - use for positions/points. Does NOT
    // perform the perspective divide - if this matrix is a projection,
    // divide the result by its w yourself (see tests/Math/Mat4Tests.cpp
    // for worked examples).
    Vec3 TransformPoint(const Vec3& p) const noexcept;
    // w=0: ignores translation - use for directions.
    Vec3 TransformVector(const Vec3& v) const noexcept;
    // Correct way to transform a NORMAL under non-uniform scale: uses
    // Inverse().Transposed() rather than this matrix directly (a plain
    // TransformVector() would skew normals under non-uniform scale).
    Vec3 TransformNormal(const Vec3& n) const noexcept;
};

// Column-major product. The SSE2 path adds the four column terms in the same order as the scalar code, so results match bit for bit.
inline Vec4 operator*(const Mat4& m, const Vec4& v) noexcept
{
#if defined(__SSE2__)
    __m128 r = _mm_mul_ps(_mm_load_ps(&m.columns[0].x), _mm_set1_ps(v.x));
    r = _mm_add_ps(r, _mm_mul_ps(_mm_load_ps(&m.columns[1].x), _mm_set1_ps(v.y)));
    r = _mm_add_ps(r, _mm_mul_ps(_mm_load_ps(&m.columns[2].x), _mm_set1_ps(v.z)));
    r = _mm_add_ps(r, _mm_mul_ps(_mm_load_ps(&m.columns[3].x), _mm_set1_ps(v.w)));
    alignas(16) float out[4];
    _mm_store_ps(out, r);
    return Vec4(out[0], out[1], out[2], out[3]);
#else
    return Vec4(
        m.columns[0].x * v.x + m.columns[1].x * v.y + m.columns[2].x * v.z + m.columns[3].x * v.w,
        m.columns[0].y * v.x + m.columns[1].y * v.y + m.columns[2].y * v.z + m.columns[3].y * v.w,
        m.columns[0].z * v.x + m.columns[1].z * v.y + m.columns[2].z * v.z + m.columns[3].z * v.w,
        m.columns[0].w * v.x + m.columns[1].w * v.y + m.columns[2].w * v.z + m.columns[3].w * v.w);
#endif
}

inline Mat4 operator*(const Mat4& a, const Mat4& b) noexcept
{
    return Mat4(a * b.columns[0], a * b.columns[1], a * b.columns[2], a * b.columns[3]);
}

inline Vec3 Mat4::TransformPoint(const Vec3& p) const noexcept
{
    return ((*this) * Vec4(p.x, p.y, p.z, 1.0f)).XYZ();
}

inline Vec3 Mat4::TransformVector(const Vec3& v) const noexcept
{
    return ((*this) * Vec4(v.x, v.y, v.z, 0.0f)).XYZ();
}

inline bool ApproximatelyEqual(const Mat4& a, const Mat4& b, float epsilon = kEpsilon) noexcept
{
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (!ApproximatelyEqual(a(r, c), b(r, c), epsilon))
                return false;
    return true;
}

static_assert(sizeof(Mat4) == sizeof(float) * 16,
    "Mat4 must be exactly 16 tightly-packed floats for Data() to be a valid direct GPU upload source");

} // namespace gte
