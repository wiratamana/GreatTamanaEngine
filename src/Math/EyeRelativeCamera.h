#pragma once

#include "Mat4.h"
#include "Vec3.h"

namespace gte {

// A camera with the eye at the origin: (world - eye) maps to clip and back. Built in double, so a camera far from the
// world origin keeps its digits.
struct EyeRelativeCamera {
    bool valid = false;
    Mat4 viewProjection = Mat4::Identity(); // (world - eye) to clip.
    Mat4 inverse = Mat4::Identity();        // Clip to (world - eye).
};

// Rotation and lens of `viewProjection` with the translation column of `projection`: VP * Translation(eye) without
// the rounding residue of a large eye position. Both matrices must come from the same camera.
Mat4 MakeEyeRelativeViewProjection(const Mat4& viewProjection, const Mat4& projection) noexcept;

// Eye-relative camera of an already eye-relative matrix. Invalid when it cannot be inverted.
EyeRelativeCamera MakeEyeRelativeCameraFromMatrix(const Mat4& relativeViewProjection) noexcept;

// Eye-relative camera from an absolute view-projection: VP * Translation(eye) in double.
EyeRelativeCamera MakeEyeRelativeCamera(const Mat4& viewProjection, const Vec3& eye) noexcept;

// previousViewProjection * Translation(currentEye) in double: maps (world - currentEye) to the previous clip space.
Mat4 MakePreviousViewProjectionRelative(const Mat4& previousViewProjection, const Vec3& currentEye) noexcept;

// previousRelative * Translation(currentEye - previousEye) in double. Both eyes are exact in double.
Mat4 ShiftRelativeViewProjection(const Mat4& previousRelative, const Vec3& previousEye, const Vec3& currentEye) noexcept;

} // namespace gte
