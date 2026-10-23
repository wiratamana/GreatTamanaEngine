#include "ClusterViewerSettings.h"

#include "OrbitCamera.h"
#include "../Core/FeatureKit/FeatureDebug.h"
#include "../ECS/Components/Camera.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gte {
namespace {

constexpr std::string_view kSetKeys =
    "drawMode, frustum, cone, occlusion, culledStyle, debugCamera, cameraPreview, cameraSelected, "
    "showSceneCamera, planes, camX, camY, camZ, camYaw, camPitch, camRoll, camFov, camNear, camFar, camAspect, "
    "copySceneCamera, orbitYaw, orbitPitch, orbitDistance, orbitTargetX, orbitTargetY, orbitTargetZ, encodeWidth";

bool Finite(const Vec3& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool Finite(const Quat& q) noexcept
{
    return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
}

float ClampOrDefault(float value, float lo, float hi, float fallback) noexcept
{
    return std::isfinite(value) ? Clamp(value, lo, hi) : fallback;
}

// Values of a set request that are applied only when every pair is good.
struct Pending {
    ClusterViewerSettings settings;
    DebugCameraState camera;
    ClusterViewerOrbitRequest orbit;
    bool copySceneCamera = false;
    std::optional<float> pitch;
    std::optional<float> yaw;
    std::optional<float> roll;
};

using Status = ClusterViewerSetStatus;

Status SetBool(std::string_view value, bool& out, std::string& range)
{
    const std::optional<bool> parsed = fx::ParseDebugBool(value);
    if (!parsed) {
        range = "true or false";
        return Status::BadValue;
    }
    out = *parsed;
    return Status::Applied;
}

template <class Target>
Status SetFloat(std::string_view value, float lo, float hi, std::string_view rangeText, Target& out, std::string& range)
{
    const std::optional<double> parsed = fx::ParseDebugFloat(value);
    if (!parsed || *parsed < static_cast<double>(lo) || *parsed > static_cast<double>(hi)) {
        range = "a number in ";
        range += rangeText;
        return Status::BadValue;
    }
    out = static_cast<float>(*parsed);
    return Status::Applied;
}

Status ApplyPair(std::string_view key, std::string_view value, Pending& p, std::string& range)
{
    ClusterViewerSettings& s = p.settings;
    DebugCameraState& cam = p.camera;
    constexpr float kPos = kDebugCameraMaxPosition;

    if (key == "drawMode") {
        if (value == "normal") { s.drawMode = ClusterViewerDrawMode::Normal; return Status::Applied; }
        if (value == "clusters") { s.drawMode = ClusterViewerDrawMode::Clusters; return Status::Applied; }
        if (value == "cull") { s.drawMode = ClusterViewerDrawMode::Cull; return Status::Applied; }
        range = "normal, clusters or cull";
        return Status::BadValue;
    }
    if (key == "culledStyle") {
        if (value == "hidden") { s.culledStyle = ClusterViewerCulledStyle::Hidden; return Status::Applied; }
        if (value == "dim") { s.culledStyle = ClusterViewerCulledStyle::Dim; return Status::Applied; }
        range = "hidden or dim";
        return Status::BadValue;
    }
    if (key == "frustum") { return SetBool(value, s.frustum, range); }
    if (key == "cone") { return SetBool(value, s.cone, range); }
    if (key == "occlusion") { return SetBool(value, s.occlusion, range); }
    if (key == "debugCamera") { return SetBool(value, cam.show, range); }
    if (key == "cameraPreview") { return SetBool(value, s.cameraPreview, range); }
    if (key == "cameraSelected") { return SetBool(value, cam.selected, range); }
    if (key == "showSceneCamera") { return SetBool(value, s.showSceneCamera, range); }
    if (key == "planes") { return SetBool(value, s.showPlanes, range); }
    if (key == "copySceneCamera") {
        bool wanted = false;
        const Status status = SetBool(value, wanted, range);
        p.copySceneCamera = p.copySceneCamera || wanted;
        return status;
    }
    if (key == "camX") { return SetFloat(value, -kPos, kPos, "-1e7..1e7", cam.transform.position.x, range); }
    if (key == "camY") { return SetFloat(value, -kPos, kPos, "-1e7..1e7", cam.transform.position.y, range); }
    if (key == "camZ") { return SetFloat(value, -kPos, kPos, "-1e7..1e7", cam.transform.position.z, range); }
    if (key == "camYaw") { return SetFloat(value, -1.0e6f, 1.0e6f, "-1e6..1e6 degrees", p.yaw, range); }
    if (key == "camPitch") { return SetFloat(value, -1.0e6f, 1.0e6f, "-1e6..1e6 degrees", p.pitch, range); }
    if (key == "camRoll") { return SetFloat(value, -1.0e6f, 1.0e6f, "-1e6..1e6 degrees", p.roll, range); }
    if (key == "camFov") {
        return SetFloat(value, kDebugCameraMinFov, kDebugCameraMaxFov, "1..170", cam.fovYDegrees, range);
    }
    if (key == "camNear") {
        return SetFloat(value, kDebugCameraMinNear, kDebugCameraMaxNear, "1e-4..1e6", cam.nearZ, range);
    }
    if (key == "camFar") {
        return SetFloat(value, kDebugCameraMinNear * kDebugCameraFarOverNear, kDebugCameraMaxFar,
            "1.01e-4..1e7", cam.farZ, range);
    }
    if (key == "camAspect") {
        return SetFloat(value, kDebugCameraMinAspect, kDebugCameraMaxAspect, "0.1..10", cam.aspect, range);
    }
    if (key == "orbitYaw") { return SetFloat(value, -1.0e6f, 1.0e6f, "-1e6..1e6 degrees", p.orbit.yawDeg, range); }
    if (key == "orbitPitch") {
        return SetFloat(value, -OrbitCamera::kMaxPitchDegrees, OrbitCamera::kMaxPitchDegrees, "-85..85 degrees",
            p.orbit.pitchDeg, range);
    }
    if (key == "orbitDistance") {
        // Upper and lower limits depend on the mesh radius; the window clamps the value.
        return SetFloat(value, std::numeric_limits<float>::min(), std::numeric_limits<float>::max(), "a positive number",
            p.orbit.distance, range);
    }
    if (key == "orbitTargetX") { return SetFloat(value, -kPos, kPos, "-1e7..1e7", p.orbit.targetX, range); }
    if (key == "orbitTargetY") { return SetFloat(value, -kPos, kPos, "-1e7..1e7", p.orbit.targetY, range); }
    if (key == "orbitTargetZ") { return SetFloat(value, -kPos, kPos, "-1e7..1e7", p.orbit.targetZ, range); }
    if (key == "encodeWidth") {
        const std::optional<std::uint64_t> parsed = fx::ParseDebugUnsigned(value);
        if (!parsed || (*parsed != 640 && *parsed != 1280 && *parsed != 1920)) {
            range = "640, 1280 or 1920";
            return Status::BadValue;
        }
        s.encodeWidth = static_cast<std::uint32_t>(*parsed);
        return Status::Applied;
    }
    return Status::UnknownKey;
}

ClusterViewerSetResult Reject(Status status, std::string text)
{
    ClusterViewerSetResult result;
    result.status = status;
    result.error = std::move(text);
    return result;
}

} // namespace

Mat4 DebugCameraViewProjection(const DebugCameraState& camera)
{
    Camera lens;
    lens.fovYDegrees = camera.fovYDegrees;
    lens.nearZ = camera.nearZ;
    lens.farZ = camera.farZ;
    return lens.ProjectionMatrix(camera.aspect) * Camera::ViewMatrix(camera.transform);
}

bool DebugCameraIsValid(const DebugCameraState& camera, std::string* why)
{
    const auto fail = [&](const char* text) {
        if (why) {
            *why = text;
        }
        return false;
    };
    if (!Finite(camera.transform.position) || !Finite(camera.transform.rotation)) {
        return fail("camera pose is not finite");
    }
    const Vec3& pos = camera.transform.position;
    if (std::fabs(pos.x) > kDebugCameraMaxPosition || std::fabs(pos.y) > kDebugCameraMaxPosition
        || std::fabs(pos.z) > kDebugCameraMaxPosition) {
        return fail("camera position is out of range");
    }
    if (!(camera.fovYDegrees >= kDebugCameraMinFov && camera.fovYDegrees <= kDebugCameraMaxFov)) {
        return fail("camera fov must be 1..170");
    }
    if (!(camera.nearZ >= kDebugCameraMinNear && camera.nearZ <= kDebugCameraMaxNear)) {
        return fail("camera near must be 1e-4..1e6");
    }
    if (!(camera.farZ > camera.nearZ * kDebugCameraFarOverNear && camera.farZ <= kDebugCameraMaxFar)) {
        return fail("camera far must be above near and at most 1e7");
    }
    if (!(camera.aspect >= kDebugCameraMinAspect && camera.aspect <= kDebugCameraMaxAspect)) {
        return fail("camera aspect must be 0.1..10");
    }
    return true;
}

DebugCameraState ClampDebugCamera(DebugCameraState camera)
{
    const DebugCameraState defaults{};
    Vec3& pos = camera.transform.position;
    pos.x = ClampOrDefault(pos.x, -kDebugCameraMaxPosition, kDebugCameraMaxPosition, 0.0f);
    pos.y = ClampOrDefault(pos.y, -kDebugCameraMaxPosition, kDebugCameraMaxPosition, 0.0f);
    pos.z = ClampOrDefault(pos.z, -kDebugCameraMaxPosition, kDebugCameraMaxPosition, 0.0f);

    Quat& rot = camera.transform.rotation;
    rot = (Finite(rot) && Length(rot) > kEpsilon) ? Normalize(rot) : Quat::Identity();
    camera.transform.scale = Vec3::One();

    camera.fovYDegrees = ClampOrDefault(camera.fovYDegrees, kDebugCameraMinFov, kDebugCameraMaxFov, defaults.fovYDegrees);
    camera.nearZ = ClampOrDefault(camera.nearZ, kDebugCameraMinNear, kDebugCameraMaxNear, defaults.nearZ);
    camera.aspect = ClampOrDefault(camera.aspect, kDebugCameraMinAspect, kDebugCameraMaxAspect, defaults.aspect);
    const float minFar = camera.nearZ * (kDebugCameraFarOverNear + 0.01f);
    camera.farZ = std::isfinite(camera.farZ) ? Clamp(camera.farZ, minFar, kDebugCameraMaxFar) : std::max(defaults.farZ, minFar);
    return camera;
}

Quat ClusterViewerLookRotation(const Vec3& eye, const Vec3& target)
{
    const Vec3 forward = Normalize(target - eye);
    if (!Finite(forward) || LengthSquared(forward) < 0.5f) {
        return Quat::Identity();
    }
    Vec3 right = Cross(Vec3::Up(), forward);
    right = Length(right) > 1.0e-4f ? Normalize(right) : Vec3::Right();
    const Vec3 up = Cross(forward, right);
    const Mat4 basis(Vec4(right, 0.0f), Vec4(up, 0.0f), Vec4(forward, 0.0f), Vec4(0.0f, 0.0f, 0.0f, 1.0f));
    return Quat::FromMat4(basis);
}

DebugCameraState DefaultDebugCamera(const Vec3& center, float radius)
{
    const float r = (std::isfinite(radius) && radius > 0.0f) ? radius : 1.0f;
    const Vec3 c = Finite(center) ? center : Vec3::Zero();

    DebugCameraState camera;
    camera.transform.position = c + Vec3(0.0f, r * 0.15f, -r * 0.9f);
    camera.transform.rotation = ClusterViewerLookRotation(camera.transform.position, c + Vec3(0.0f, 0.0f, r * 0.4f));
    camera.fovYDegrees = 60.0f;
    camera.nearZ = std::max(0.1f, r * 0.002f);
    camera.farZ = std::max(r * 1.2f, camera.nearZ * 2.0f);
    return ClampDebugCamera(camera);
}

ClusterViewerSetResult ParseClusterViewerSet(
    std::span<const ClusterViewerKeyValue> pairs, ClusterViewerSettings& settings, DebugCameraState& camera)
{
    Pending pending;
    pending.settings = settings;
    pending.camera = camera;

    std::string range;
    for (const ClusterViewerKeyValue& pair : pairs) {
        range.clear();
        const Status status = ApplyPair(pair.first, pair.second, pending, range);
        if (status == Status::UnknownKey) {
            return Reject(status, "unknown key '" + pair.first + "'. Valid keys: " + std::string(kSetKeys));
        }
        if (status == Status::BadValue) {
            return Reject(status, "bad value '" + pair.second + "' for '" + pair.first + "': expected " + range);
        }
    }

    if (pending.pitch || pending.yaw || pending.roll) {
        const Vec3 current = pending.camera.transform.rotation.ToEulerDegrees();
        pending.camera.transform.rotation = Quat::FromEulerDegrees(
            pending.pitch.value_or(current.x), pending.yaw.value_or(current.y), pending.roll.value_or(current.z));
    }
    pending.camera.transform.scale = Vec3::One();

    if (!(pending.camera.farZ > pending.camera.nearZ * kDebugCameraFarOverNear)) {
        return Reject(Status::BadValue, "camFar must be above camNear * 1.01 (near " +
            std::to_string(pending.camera.nearZ) + ", far " + std::to_string(pending.camera.farZ) + ")");
    }

    settings = pending.settings;
    camera = pending.camera;
    ClusterViewerSetResult result;
    result.copySceneCamera = pending.copySceneCamera;
    result.orbit = pending.orbit;
    return result;
}

std::string_view ClusterViewerDrawModeName(ClusterViewerDrawMode mode) noexcept
{
    switch (mode) {
    case ClusterViewerDrawMode::Normal: return "normal";
    case ClusterViewerDrawMode::Clusters: return "clusters";
    case ClusterViewerDrawMode::Cull: return "cull";
    }
    return "normal";
}

std::string_view ClusterViewerCulledStyleName(ClusterViewerCulledStyle style) noexcept
{
    switch (style) {
    case ClusterViewerCulledStyle::Hidden: return "hidden";
    case ClusterViewerCulledStyle::Dim: return "dim";
    }
    return "dim";
}

} // namespace gte
