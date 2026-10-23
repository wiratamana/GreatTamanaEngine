#pragma once

// Cluster viewer settings, debug camera data and the text parser of the set keys. Pure: no ImGui, no GPU.

#include "CameraFrustumGizmo.h"
#include "../ECS/Components/Transform.h"
#include "../Math/Mat4.h"
#include "../Math/Quat.h"
#include "../Math/Vec3.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace gte {

enum class ClusterViewerDrawMode : std::uint8_t { Normal, Clusters, Cull };
enum class ClusterViewerCulledStyle : std::uint8_t { Hidden, Dim };
enum class ClusterViewerGizmoOperation : std::uint8_t { Translate, Rotate };

struct ClusterViewerSettings {
    ClusterViewerDrawMode drawMode = ClusterViewerDrawMode::Normal;
    bool frustum = true;
    bool cone = false;
    bool occlusion = false;
    ClusterViewerCulledStyle culledStyle = ClusterViewerCulledStyle::Dim;
    ClusterViewerGizmoOperation gizmoOperation = ClusterViewerGizmoOperation::Translate;
    bool cameraPreview = false;
    bool showSceneCamera = true;
    bool showPlanes = false; // debug camera: one inward-normal arrow per frustum plane
    std::uint32_t encodeWidth = 1280; // 640, 1280 or 1920: size of Verify and Hi-Z capture
};

// Ranges shared by the parser, the UI clamp and the validators.
inline constexpr float kDebugCameraMinFov = kFrustumMinFovDegrees;
inline constexpr float kDebugCameraMaxFov = kFrustumMaxFovDegrees;
inline constexpr float kDebugCameraMinNear = 1.0e-4f;
inline constexpr float kDebugCameraMaxNear = 1.0e6f;
inline constexpr float kDebugCameraMaxFar = kFrustumMaxFar;
inline constexpr float kDebugCameraFarOverNear = 1.01f; // far must exceed near by this factor
inline constexpr float kDebugCameraMinAspect = 0.1f;
inline constexpr float kDebugCameraMaxAspect = 10.0f;
inline constexpr float kDebugCameraMaxPosition = 1.0e7f;

// Perspective debug camera. Position and rotation come from `transform`; scale stays 1.
struct DebugCameraState {
    bool show = false;
    bool selected = false;
    Transform transform;
    float fovYDegrees = 60.0f;
    float nearZ = 1.0f;
    float farZ = 2000.0f;
    float aspect = 16.0f / 9.0f;
};

// Engine camera math: Camera::ProjectionMatrix(aspect) * Camera::ViewMatrix(transform).
Mat4 DebugCameraViewProjection(const DebugCameraState& camera);

// True when every lens value and the pose are finite and inside their ranges. `why` receives the first problem.
bool DebugCameraIsValid(const DebugCameraState& camera, std::string* why = nullptr);

// Clamps every lens value and the position into range. Non-finite values fall back to the defaults.
DebugCameraState ClampDebugCamera(DebugCameraState camera);

// Rotation that looks from `eye` to `target` with Y up (identity when the two coincide).
Quat ClusterViewerLookRotation(const Vec3& eye, const Vec3& target);

// Pose in front of a mesh of the given bounds. `show` and `selected` are false.
DebugCameraState DefaultDebugCamera(const Vec3& center, float radius);

// Orbit values a set request may carry. The window clamps them with its own orbit limits.
struct ClusterViewerOrbitRequest {
    std::optional<float> yawDeg;
    std::optional<float> pitchDeg;
    std::optional<float> distance;
    std::optional<float> targetX;
    std::optional<float> targetY;
    std::optional<float> targetZ;
};

enum class ClusterViewerSetStatus : std::uint8_t { Applied, UnknownKey, BadValue };

struct ClusterViewerSetResult {
    ClusterViewerSetStatus status = ClusterViewerSetStatus::Applied;
    std::string error;                  // full text, with the valid keys or the valid range
    bool copySceneCamera = false;       // one-time action: copy the selected Scene camera
    ClusterViewerOrbitRequest orbit;    // orbit values to hand to the window's orbit camera

    bool Ok() const noexcept { return status == ClusterViewerSetStatus::Applied; }
};

using ClusterViewerKeyValue = std::pair<std::string, std::string>;

// All pairs are parsed and checked on copies first. `settings` and `camera` change only when the result is Applied.
ClusterViewerSetResult ParseClusterViewerSet(
    std::span<const ClusterViewerKeyValue> pairs, ClusterViewerSettings& settings, DebugCameraState& camera);

// Lower case names used by the set keys and the state JSON.
std::string_view ClusterViewerDrawModeName(ClusterViewerDrawMode mode) noexcept;
std::string_view ClusterViewerCulledStyleName(ClusterViewerCulledStyle style) noexcept;

} // namespace gte
