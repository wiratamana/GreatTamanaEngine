#include "EyeRelativeCamera.h"

#include "Mat4d.h"

namespace gte {

Mat4 MakeEyeRelativeViewProjection(const Mat4& viewProjection, const Mat4& projection) noexcept
{
    Mat4 out = viewProjection;
    out.columns[3] = projection.columns[3];
    return out;
}

EyeRelativeCamera MakeEyeRelativeCameraFromMatrix(const Mat4& relativeViewProjection) noexcept
{
    EyeRelativeCamera camera;
    const Mat4d relative = Mat4d::FromMat4(relativeViewProjection);
    Mat4d inverse;
    if (!relative.TryInverse(inverse)) {
        return camera;
    }
    camera.valid = true;
    camera.viewProjection = relativeViewProjection;
    camera.inverse = inverse.ToMat4();
    return camera;
}

EyeRelativeCamera MakeEyeRelativeCamera(const Mat4& viewProjection, const Vec3& eye) noexcept
{
    EyeRelativeCamera camera;
    const Mat4d relative = Mat4d::FromMat4(viewProjection)
        * Mat4d::Translation(static_cast<double>(eye.x), static_cast<double>(eye.y), static_cast<double>(eye.z));
    Mat4d inverse;
    if (!relative.TryInverse(inverse)) {
        return camera;
    }
    camera.valid = true;
    camera.viewProjection = relative.ToMat4();
    camera.inverse = inverse.ToMat4();
    return camera;
}

Mat4 MakePreviousViewProjectionRelative(const Mat4& previousViewProjection, const Vec3& currentEye) noexcept
{
    const Mat4d shifted = Mat4d::FromMat4(previousViewProjection)
        * Mat4d::Translation(static_cast<double>(currentEye.x), static_cast<double>(currentEye.y),
            static_cast<double>(currentEye.z));
    return shifted.ToMat4();
}

Mat4 ShiftRelativeViewProjection(const Mat4& previousRelative, const Vec3& previousEye, const Vec3& currentEye) noexcept
{
    const Mat4d shifted = Mat4d::FromMat4(previousRelative)
        * Mat4d::Translation(static_cast<double>(currentEye.x) - static_cast<double>(previousEye.x),
            static_cast<double>(currentEye.y) - static_cast<double>(previousEye.y),
            static_cast<double>(currentEye.z) - static_cast<double>(previousEye.z));
    return shifted.ToMat4();
}

} // namespace gte
