#include "ClusterDrawTypes.h"

#include "../../Renderer/Culling/ClusterCullMath.h"

namespace gte::fx {

ClusterCamera ClusterCameraOf(const View& view)
{
    ClusterCamera camera;
    camera.viewProjection = view.viewProjection;
    camera.orthographic = IsOrthographicProjection(view.viewProjection);
    camera.eyeOrDirection = camera.orthographic ? OrthographicViewDirection(view.viewProjection) : view.eye;
    camera.valid = CameraIsFinite(view.viewProjection, camera.eyeOrDirection);
    return camera;
}

} // namespace gte::fx
