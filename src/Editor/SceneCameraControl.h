#pragma once

// The one surface the Scene cameras expose to the HTTP bridge. No ImGui, no Vulkan.

#include "ClusterViewerControl.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gte {

enum class SceneCameraError : std::uint8_t { None, NotRendered, NoCamera, NotFound, BadInput, Unavailable };

struct SceneCameraResult {
    bool ok = false;
    SceneCameraError error = SceneCameraError::None;
    std::string message;
};

struct SceneCameraPickResult {
    bool ok = false;
    bool hit = false;
    SceneCameraError error = SceneCameraError::None;
    std::string entityName;
    std::string message;
};

struct SceneCameraInfo {
    std::uint64_t entityId = 0;
    std::string name;
    float position[3]{};
    float rotationEuler[3]{}; // degrees: pitch, yaw, roll
    float fovYDegrees = 60.0f;
    float nearZ = 0.1f;
    float farZ = 1000.0f;
    bool selected = false;
    float apexScreenX = 0.0f; // 0..1 of the Scene image
    float apexScreenY = 0.0f;
    bool apexVisible = false;
};

// Position (world units) and look direction (degrees) of the Scene camera.
struct SceneCameraPose {
    float position[3]{};
    float yawDegrees = 0.0f;
    float pitchDegrees = 0.0f;
};

// Everything GET /scene_camera/state shows.
struct SceneCameraReport {
    bool rendered = false; // the Scene view was drawn last frame
    int imageWidth = 0;
    int imageHeight = 0;
    std::vector<SceneCameraInfo> cameras;
    std::uint64_t selectedEntity = 0; // 0 when no camera is selected
    std::vector<OverlaySegment2D> selectedFrustum; // image pixels
    SceneCameraPose pose; // the Scene camera; valid without a rendered Scene image
};

class ISceneCameraControl {
public:
    virtual ~ISceneCameraControl() = default;

    virtual SceneCameraReport Report() const = 0;
    virtual SceneCameraResult Select(const std::string& entityName) = 0;

    // Camera pick at a point (x, y in 0..1) of the Scene image.
    virtual SceneCameraPickResult Pick(float x, float y) = 0;

    // Moves the Scene camera. Position in world units, angles in degrees.
    virtual SceneCameraResult SetPose(const SceneCameraPose& pose) = 0;
    virtual std::optional<ClusterViewerImage> CaptureImage(bool overlay) = 0;
};

} // namespace gte
