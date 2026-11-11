#include "Mat4d.h"

#include <cmath>
#include <utility>

namespace gte {

Mat4d Mat4d::Identity() noexcept
{
    Mat4d out;
    for (int i = 0; i < 4; ++i) {
        out.m[i * 4 + i] = 1.0;
    }
    return out;
}

Mat4d Mat4d::FromMat4(const Mat4& a) noexcept
{
    Mat4d out;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            out.m[c * 4 + r] = static_cast<double>(a(r, c));
        }
    }
    return out;
}

Mat4d Mat4d::Translation(double x, double y, double z) noexcept
{
    Mat4d out = Identity();
    out.m[12] = x;
    out.m[13] = y;
    out.m[14] = z;
    return out;
}

Mat4 Mat4d::ToMat4() const noexcept
{
    Mat4 out = Mat4::Identity();
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            out(r, c) = static_cast<float>(m[c * 4 + r]);
        }
    }
    return out;
}

bool Mat4d::IsFinite() const noexcept
{
    for (const double value : m) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}

bool Mat4d::TryInverse(Mat4d& out) const noexcept
{
    if (!IsFinite()) {
        return false;
    }
    double a[4][8] = {};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            a[r][c] = m[c * 4 + r];
        }
        a[r][4 + r] = 1.0;
    }
    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        for (int r = col + 1; r < 4; ++r) {
            if (std::abs(a[r][col]) > std::abs(a[pivot][col])) {
                pivot = r;
            }
        }
        if (!(std::abs(a[pivot][col]) > 1e-300)) {
            return false;
        }
        for (int c = 0; c < 8; ++c) {
            std::swap(a[col][c], a[pivot][c]);
        }
        const double scale = 1.0 / a[col][col];
        for (int c = 0; c < 8; ++c) {
            a[col][c] *= scale;
        }
        for (int r = 0; r < 4; ++r) {
            if (r == col) {
                continue;
            }
            const double factor = a[r][col];
            for (int c = 0; c < 8; ++c) {
                a[r][c] -= factor * a[col][c];
            }
        }
    }
    Mat4d result;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            result.m[c * 4 + r] = a[r][4 + c];
        }
    }
    if (!result.IsFinite()) {
        return false;
    }
    out = result;
    return true;
}

void Mat4d::Transform(const double (&v)[4], double (&out)[4]) const noexcept
{
    for (int r = 0; r < 4; ++r) {
        out[r] = m[r] * v[0] + m[4 + r] * v[1] + m[8 + r] * v[2] + m[12 + r] * v[3];
    }
}

Mat4d operator*(const Mat4d& a, const Mat4d& b) noexcept
{
    Mat4d out;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            double sum = 0.0;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[k * 4 + r] * b.m[c * 4 + k];
            }
            out.m[c * 4 + r] = sum;
        }
    }
    return out;
}

} // namespace gte
