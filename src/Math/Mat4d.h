#pragma once

#include "Mat4.h"

namespace gte {

// Double 4x4, column-major like Mat4 (index = col * 4 + row). CPU only, never uploaded.
struct Mat4d {
    double m[16] = {};

    static Mat4d Identity() noexcept;
    static Mat4d FromMat4(const Mat4& a) noexcept;
    static Mat4d Translation(double x, double y, double z) noexcept;

    Mat4 ToMat4() const noexcept;

    // Gauss-Jordan with partial pivoting. False when singular or not finite.
    bool TryInverse(Mat4d& out) const noexcept;
    bool IsFinite() const noexcept;

    // Column-vector product `M * v` for a homogeneous point.
    void Transform(const double (&v)[4], double (&out)[4]) const noexcept;
};

// Same column-vector convention as Mat4: `a * b` applies b first.
Mat4d operator*(const Mat4d& a, const Mat4d& b) noexcept;

} // namespace gte
