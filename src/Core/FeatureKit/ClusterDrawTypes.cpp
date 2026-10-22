#include "ClusterDrawTypes.h"

#include "../../Renderer/Culling/ClusterCullMath.h"

namespace gte::fx {

ClusterCamera MakeClusterCamera(const Mat4& viewProjection, const Vec3& eye)
{
    ClusterCamera camera;
    camera.viewProjection = viewProjection;
    camera.orthographic = IsOrthographicProjection(viewProjection);
    camera.eyeOrDirection = camera.orthographic ? OrthographicViewDirection(viewProjection) : eye;
    camera.valid = CameraIsFinite(viewProjection, camera.eyeOrDirection);
    return camera;
}

ClusterCamera ClusterCameraOf(const View& view)
{
    return MakeClusterCamera(view.viewProjection, view.eye);
}

} // namespace gte::fx
