#pragma once

#include "MathTypes.h"
#include "Vec3.h"
#include "Vec4.h"

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

    // Cofactor-expansion determinant, computed from the same adjugate
    // matrix Inverse()/TryInverse() below use internally - kept as one
    // shared routine (see Mat4.cpp) so Determinant() and the inverse can
    // never silently disagree with each other.
    float Determinant() const noexcept;

    // Returns Identity() and (debug builds only) asserts if this matrix is
    // singular (Determinant() ~ 0) - a singular matrix reaching Inverse()
    // almost always means a programming error upstream (e.g. a zero
    // scale). Callers that need to handle a singular matrix as a normal,
    // expected outcome should use TryInverse() instead.
    Mat4 Inverse() const noexcept;
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

Mat4 operator*(const Mat4& a, const Mat4& b) noexcept;
Vec4 operator*(const Mat4& m, const Vec4& v) noexcept;

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
