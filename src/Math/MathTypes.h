#pragma once

#include <algorithm>
#include <cmath>

// GreatTamanaEngine math library conventions (see also Mat4.h/Quat.h for the
// handful of pieces that are genuinely handedness-specific):
//
//   - Coordinate system: LEFT-HANDED, Y-up, Z-forward, X-right (matches
//     Unity/DirectX - NOT Unreal's Z-up/X-forward, and NOT OpenGL's
//     right-handed Y-up/-Z-forward). See Vec3::Right()/Up()/Forward().
//   - Angles: radians internally, everywhere. Degrees only exist at the
//     human-facing edges of the public API (e.g. Quat::FromEulerDegrees/
//     ToEulerDegrees), exactly like Unity's Inspector showing degrees while
//     everything underneath is radians.
//   - Vec2/Vec3/Vec4/Mat4/Quat algebra itself (dot/cross/length/normalize,
//     matrix multiply/transpose/inverse, quaternion multiply/slerp/
//     conjugate) is ordinary, handedness-agnostic linear algebra - it works
//     identically no matter which axis is called "forward". The only
//     genuinely LEFT-HANDED-specific pieces in this whole library are
//     Mat4::LookAtLH/PerspectiveFovLH_ZO/OrthographicLH_ZO (see Mat4.h) -
//     those bake in "which way is forward" by construction.
//   - This library was written from scratch on purpose (see project README
//     discussion) rather than depending on GLM - deliberately using
//     well-established, textbook/widely-published formulas throughout
//     (cross product, Hamilton quaternion product, Shepperd's method for
//     matrix->quaternion, the classic MESA-style 4x4 adjugate inverse, the
//     standard DirectX-style LH/zero-to-one-depth projection matrices) to
//     keep correctness risk low, with unit tests in tests/Math/ built
//     around hand-verified exact values rather than "trust me" numbers -
//     see each file's tests for the worked-out expected results.
//   - SIMD (SSE/AVX/NEON) intrinsics are intentionally NOT implemented yet -
//     left as a documented follow-up once there's a real profiling reason
//     to need it, same "don't build it before it's needed" philosophy as
//     the Tier 2 GPU test fixture noted in tests/CMakeLists.txt. The public
//     API is already shaped so adding it later costs nothing at existing
//     call sites: Vec4/Mat4/Quat are alignas(16) and store nothing but 4
//     (or, for Mat4, 16) contiguous floats (see Vec4.h/Mat4.h/Quat.h), so a
//     future SSE/NEON path only needs new .cpp bodies for
//     operator*/Dot/Normalize/etc. (reinterpreting that same memory as
//     __m128/float32x4_t) behind a compile-time flag, mirroring
//     GTE_ENABLE_EDITOR's "zero-touch when off" pattern - no struct field,
//     function signature, or call site needs to change either way. Vec2/
//     Vec3 are deliberately left plain and unaligned: SIMD payoff on a
//     3-wide vector is marginal, and forcing Vec3 to a 4-wide/padded layout
//     later would risk breaking any code that comes to assume its current
//     tight 12-byte packing (e.g. vertex/mesh data) - if that ever matters,
//     prefer a separate SIMD-friendly type over changing Vec3 itself.
namespace gte {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kHalfPi = 0.5f * kPi;

// Default tolerance for ApproximatelyEqual() below - deliberately loose
// enough to absorb ordinary float32 accumulation error (e.g. after several
// chained matrix multiplies), not a "two floats are bit-identical" check.
constexpr float kEpsilon = 1e-6f;

constexpr float DegToRad(float degrees) noexcept
{
    return degrees * (kPi / 180.0f);
}

constexpr float RadToDeg(float radians) noexcept
{
    return radians * (180.0f / kPi);
}

constexpr float Clamp(float value, float min, float max) noexcept
{
    return value < min ? min : (value > max ? max : value);
}

constexpr float Lerp(float a, float b, float t) noexcept
{
    return a + (b - a) * t;
}

// Relative-tolerance float comparison - scales the tolerance by the
// magnitude of the larger operand so this stays meaningful for both tiny
// and large values, instead of a fixed absolute epsilon that's too strict
// for big numbers and too loose for small ones.
inline bool ApproximatelyEqual(float a, float b, float epsilon = kEpsilon) noexcept
{
    return std::fabs(a - b) <= epsilon * std::max(1.0f, std::max(std::fabs(a), std::fabs(b)));
}

} // namespace gte
