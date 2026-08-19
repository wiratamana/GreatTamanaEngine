#include "Mat4.h"

#include "Quat.h"

#include <cassert>
#include <cmath>

namespace gte {

namespace {

// Classic column-major 4x4 adjugate + determinant, based on the widely-
// published "MESA glu invert matrix" cofactor-expansion algorithm (twelve
// shared 2x2 sub-determinants reused across the sixteen adjugate entries).
// Kept as one shared routine so Mat4::Determinant() and
// Mat4::Inverse()/TryInverse() can never disagree with each other.
// `m`/`adj` are column-major float[16] (index = col*4 + row), matching
// Mat4::Data().
void ComputeAdjugateAndDeterminant(const float m[16], float adj[16], float& det) noexcept
{
    adj[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    adj[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    adj[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    adj[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];

    adj[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    adj[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    adj[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    adj[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];

    adj[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    adj[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    adj[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    adj[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];

    adj[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    adj[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    adj[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    adj[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    det = m[0] * adj[0] + m[1] * adj[4] + m[2] * adj[8] + m[3] * adj[12];
}

} // namespace

Mat4 Mat4::Identity() noexcept
{
    return Mat4(
        Vec4(1.0f, 0.0f, 0.0f, 0.0f),
        Vec4(0.0f, 1.0f, 0.0f, 0.0f),
        Vec4(0.0f, 0.0f, 1.0f, 0.0f),
        Vec4(0.0f, 0.0f, 0.0f, 1.0f));
}

Mat4 Mat4::Translation(const Vec3& t) noexcept
{
    Mat4 m = Identity();
    m.columns[3] = Vec4(t.x, t.y, t.z, 1.0f);
    return m;
}

Mat4 Mat4::Scaling(const Vec3& s) noexcept
{
    Mat4 m = Identity();
    m.columns[0].x = s.x;
    m.columns[1].y = s.y;
    m.columns[2].z = s.z;
    return m;
}

Mat4 Mat4::FromQuat(const Quat& q) noexcept
{
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

    Mat4 m = Identity();
    m.columns[0] = Vec4(1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz), 2.0f * (xz - wy), 0.0f);
    m.columns[1] = Vec4(2.0f * (xy - wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx), 0.0f);
    m.columns[2] = Vec4(2.0f * (xz + wy), 2.0f * (yz - wx), 1.0f - 2.0f * (xx + yy), 0.0f);
    return m;
}

Mat4 Mat4::TRS(const Vec3& position, const Quat& rotation, const Vec3& scale) noexcept
{
    return Translation(position) * FromQuat(rotation) * Scaling(scale);
}

Mat4 Mat4::LookAtLH(const Vec3& eye, const Vec3& target, const Vec3& up) noexcept
{
    Vec3 zaxis = Normalize(target - eye);      // forward
    Vec3 xaxis = Normalize(Cross(up, zaxis));  // right
    Vec3 yaxis = Cross(zaxis, xaxis);          // up (already unit-length: zaxis/xaxis are orthonormal)

    return Mat4(
        Vec4(xaxis.x, yaxis.x, zaxis.x, 0.0f),
        Vec4(xaxis.y, yaxis.y, zaxis.y, 0.0f),
        Vec4(xaxis.z, yaxis.z, zaxis.z, 0.0f),
        Vec4(-Dot(xaxis, eye), -Dot(yaxis, eye), -Dot(zaxis, eye), 1.0f));
}

Mat4 Mat4::PerspectiveFovLH_ZO(float fovYRadians, float aspect, float nearZ, float farZ, bool flipY) noexcept
{
    float f = 1.0f / std::tan(fovYRadians * 0.5f);

    Mat4 m; // zero-initialized
    m.columns[0].x = f / aspect;
    m.columns[1].y = flipY ? -f : f;
    m.columns[2].z = farZ / (farZ - nearZ);
    m.columns[2].w = 1.0f;
    m.columns[3].z = -(nearZ * farZ) / (farZ - nearZ);
    return m;
}

Mat4 Mat4::OrthographicLH_ZO(float left, float right, float bottom, float top, float nearZ, float farZ, bool flipY) noexcept
{
    Mat4 m = Identity();
    m.columns[0].x = 2.0f / (right - left);
    m.columns[1].y = (flipY ? -1.0f : 1.0f) * 2.0f / (top - bottom);
    m.columns[2].z = 1.0f / (farZ - nearZ);
    m.columns[3].x = -(right + left) / (right - left);
    m.columns[3].y = -(top + bottom) / (top - bottom);
    m.columns[3].z = -nearZ / (farZ - nearZ);
    return m;
}

Mat4 Mat4::Transposed() const noexcept
{
    Mat4 result;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            result(r, c) = (*this)(c, r);
    return result;
}

float Mat4::Determinant() const noexcept
{
    float adj[16];
    float det;
    ComputeAdjugateAndDeterminant(Data(), adj, det);
    return det;
}

bool Mat4::TryInverse(Mat4& outInverse) const noexcept
{
    float adj[16];
    float det;
    ComputeAdjugateAndDeterminant(Data(), adj, det);

    if (std::fabs(det) <= kEpsilon)
        return false;

    float invDet = 1.0f / det;
    for (float& v : adj)
        v *= invDet;

    outInverse = Mat4(
        Vec4(adj[0], adj[1], adj[2], adj[3]),
        Vec4(adj[4], adj[5], adj[6], adj[7]),
        Vec4(adj[8], adj[9], adj[10], adj[11]),
        Vec4(adj[12], adj[13], adj[14], adj[15]));
    return true;
}

Mat4 Mat4::Inverse() const noexcept
{
    Mat4 result;
    bool ok = TryInverse(result);
    assert(ok && "Mat4::Inverse() called on a singular matrix - use TryInverse() if this can legitimately happen");
    return ok ? result : Mat4::Identity();
}

Vec3 Mat4::TransformPoint(const Vec3& p) const noexcept
{
    Vec4 r = (*this) * Vec4(p.x, p.y, p.z, 1.0f);
    return r.XYZ();
}

Vec3 Mat4::TransformVector(const Vec3& v) const noexcept
{
    Vec4 r = (*this) * Vec4(v.x, v.y, v.z, 0.0f);
    return r.XYZ();
}

Vec3 Mat4::TransformNormal(const Vec3& n) const noexcept
{
    Mat4 inv;
    if (!TryInverse(inv))
        return n; // degenerate transform - nothing sane to do, return input unchanged
    return inv.Transposed().TransformVector(n);
}

Vec4 operator*(const Mat4& m, const Vec4& v) noexcept
{
    return Vec4(
        m.columns[0].x * v.x + m.columns[1].x * v.y + m.columns[2].x * v.z + m.columns[3].x * v.w,
        m.columns[0].y * v.x + m.columns[1].y * v.y + m.columns[2].y * v.z + m.columns[3].y * v.w,
        m.columns[0].z * v.x + m.columns[1].z * v.y + m.columns[2].z * v.z + m.columns[3].z * v.w,
        m.columns[0].w * v.x + m.columns[1].w * v.y + m.columns[2].w * v.z + m.columns[3].w * v.w);
}

Mat4 operator*(const Mat4& a, const Mat4& b) noexcept
{
    return Mat4(
        a * b.columns[0],
        a * b.columns[1],
        a * b.columns[2],
        a * b.columns[3]);
}

} // namespace gte
