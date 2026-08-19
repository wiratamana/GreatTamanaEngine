#include "Quat.h"

#include "Mat4.h"

#include <cmath>

namespace gte {

Quat Quat::FromAxisAngle(const Vec3& axis, float angleRadians) noexcept
{
    Vec3 n = Normalize(axis);
    float half = angleRadians * 0.5f;
    float s = std::sin(half);
    return Quat(n.x * s, n.y * s, n.z * s, std::cos(half));
}

Quat Quat::FromEulerDegrees(float pitchX, float yawY, float rollZ) noexcept
{
    Quat qYaw = FromAxisAngle(Vec3::Up(), DegToRad(yawY));
    Quat qPitch = FromAxisAngle(Vec3::Right(), DegToRad(pitchX));
    Quat qRoll = FromAxisAngle(Vec3::Forward(), DegToRad(rollZ));
    return qYaw * qPitch * qRoll;
}

Vec3 Quat::ToEulerDegrees() const noexcept
{
    Mat4 m = ToMat4();

    // Decomposing R = Ry(yaw) * Rx(pitch) * Rz(roll) (see FromEulerDegrees
    // above) gives, among others:
    //   R(1,2) = -sin(pitch)
    //   R(1,0) = cos(pitch)*sin(roll), R(1,1) = cos(pitch)*cos(roll)
    //   R(0,2) = cos(pitch)*sin(yaw),  R(2,2) = cos(pitch)*cos(yaw)
    float sx = Clamp(-m(1, 2), -1.0f, 1.0f);
    float pitch = std::asin(sx);
    float cx = std::cos(pitch);

    float yaw, roll;
    if (cx > 1e-4f) {
        roll = std::atan2(m(1, 0), m(1, 1));
        yaw = std::atan2(m(0, 2), m(2, 2));
    } else {
        // Gimbal lock (pitch ~ +/-90 degrees): yaw and roll become the same
        // degree of freedom - conventionally dump all of it into yaw and
        // zero out roll, same fallback most engines use here.
        roll = 0.0f;
        yaw = std::atan2(-m(2, 0), m(0, 0));
    }

    return Vec3(RadToDeg(pitch), RadToDeg(yaw), RadToDeg(roll));
}

Mat4 Quat::ToMat4() const noexcept
{
    return Mat4::FromQuat(*this);
}

Quat Quat::FromMat4(const Mat4& m) noexcept
{
    // Shepperd's method - numerically robust across the full rotation
    // range (the naive single-sqrt(1+trace) formula blows up near
    // trace <= -1, i.e. rotations close to 180 degrees).
    float m00 = m(0, 0), m11 = m(1, 1), m22 = m(2, 2);
    float trace = m00 + m11 + m22;

    Quat q;
    if (trace > 0.0f) {
        float s = 0.5f / std::sqrt(trace + 1.0f);
        q.w = 0.25f / s;
        q.x = (m(2, 1) - m(1, 2)) * s;
        q.y = (m(0, 2) - m(2, 0)) * s;
        q.z = (m(1, 0) - m(0, 1)) * s;
    } else if (m00 > m11 && m00 > m22) {
        float s = 2.0f * std::sqrt(1.0f + m00 - m11 - m22);
        q.w = (m(2, 1) - m(1, 2)) / s;
        q.x = 0.25f * s;
        q.y = (m(0, 1) + m(1, 0)) / s;
        q.z = (m(0, 2) + m(2, 0)) / s;
    } else if (m11 > m22) {
        float s = 2.0f * std::sqrt(1.0f + m11 - m00 - m22);
        q.w = (m(0, 2) - m(2, 0)) / s;
        q.x = (m(0, 1) + m(1, 0)) / s;
        q.y = 0.25f * s;
        q.z = (m(1, 2) + m(2, 1)) / s;
    } else {
        float s = 2.0f * std::sqrt(1.0f + m22 - m00 - m11);
        q.w = (m(1, 0) - m(0, 1)) / s;
        q.x = (m(0, 2) + m(2, 0)) / s;
        q.y = (m(1, 2) + m(2, 1)) / s;
        q.z = 0.25f * s;
    }
    return q;
}

Quat Quat::Inverse() const noexcept
{
    float lenSq = LengthSquared(*this);
    if (lenSq <= kEpsilon)
        return Identity();
    float invLenSq = 1.0f / lenSq;
    Quat c = Conjugate();
    return Quat(c.x * invLenSq, c.y * invLenSq, c.z * invLenSq, c.w * invLenSq);
}

Vec3 Quat::RotateVector(const Vec3& v) const noexcept
{
    // Optimized q*v*q^-1 (avoids building the full quaternion product) -
    // standard, widely-published formula.
    Vec3 qv(x, y, z);
    Vec3 t = Cross(qv, v) * 2.0f;
    return v + t * w + Cross(qv, t);
}

Quat operator*(const Quat& a, const Quat& b) noexcept
{
    // Hamilton product - `a * b` means "apply b first, then a" (see
    // Quat.h struct comment).
    return Quat(
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}

Quat operator*(const Quat& q, float s) noexcept { return Quat(q.x * s, q.y * s, q.z * s, q.w * s); }
Quat operator+(const Quat& a, const Quat& b) noexcept { return Quat(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); }
Vec3 operator*(const Quat& q, const Vec3& v) noexcept { return q.RotateVector(v); }

float Dot(const Quat& a, const Quat& b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
float LengthSquared(const Quat& q) noexcept { return Dot(q, q); }
float Length(const Quat& q) noexcept { return std::sqrt(LengthSquared(q)); }

Quat Normalize(const Quat& q) noexcept
{
    float len = Length(q);
    return len > kEpsilon ? q * (1.0f / len) : Quat::Identity();
}

Quat Slerp(const Quat& a, const Quat& b, float t) noexcept
{
    Quat bb = b;
    float cosOmega = Dot(a, b);
    if (cosOmega < 0.0f) {
        // Take the shorter path around the 4D hypersphere - q and -q are
        // the same rotation, but interpolating toward the "far" one would
        // visibly take the long way round.
        bb = Quat(-b.x, -b.y, -b.z, -b.w);
        cosOmega = -cosOmega;
    }

    if (cosOmega > 0.9995f) {
        // Nearly identical/opposite rotations - sin(omega) below would be
        // ~0, so fall back to (normalized) linear interpolation instead of
        // dividing by it.
        return Nlerp(a, bb, t);
    }

    float omega = std::acos(Clamp(cosOmega, -1.0f, 1.0f));
    float sinOmega = std::sin(omega);
    float wa = std::sin((1.0f - t) * omega) / sinOmega;
    float wb = std::sin(t * omega) / sinOmega;
    return Normalize(a * wa + bb * wb);
}

Quat Nlerp(const Quat& a, const Quat& b, float t) noexcept
{
    Quat bb = b;
    if (Dot(a, b) < 0.0f)
        bb = Quat(-b.x, -b.y, -b.z, -b.w);
    return Normalize(a * (1.0f - t) + bb * t);
}

bool ApproximatelyEqual(const Quat& a, const Quat& b, float epsilon) noexcept
{
    return gte::ApproximatelyEqual(a.x, b.x, epsilon)
        && gte::ApproximatelyEqual(a.y, b.y, epsilon)
        && gte::ApproximatelyEqual(a.z, b.z, epsilon)
        && gte::ApproximatelyEqual(a.w, b.w, epsilon);
}

bool RepresentSameRotation(const Quat& a, const Quat& b, float epsilon) noexcept
{
    return std::fabs(Dot(a, b)) >= 1.0f - epsilon;
}

} // namespace gte
