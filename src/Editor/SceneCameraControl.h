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

// Cull view numbers of GET /scene_camera/state. Enum values are carried as their names.
struct SceneCameraCullView {
    bool valid = false; // A feature is linked and has reported.
    bool rendered = false;
    bool featureEnabled = false;
    std::uint64_t frame = 0;
    std::string choice;
    std::string mode;
    std::string source;
    bool cone = false;
    std::uint64_t testedAtVisibleFrame = 0;
    std::uint32_t visible = 0;
    std::uint64_t hidden = 0;
    bool visibleValid = false; // False: no count yet, or the counts are settling.
    std::uint64_t claimedInstances = 0;
    std::string occlusion;
    std::string hizReason;
    bool frozen = false;
    std::uint64_t frozenFrame = 0;
    std::string inertReason;
    bool refused = false;
    std::string note;
};

// Everything GET /scene_camera/state shows.
struct SceneCameraReport {
    bool rendered = false; // the Scene view was drawn last frame
    int imageWidth = 0;
    int imageHeight = 0;
    std::vector<SceneCameraInfo> cameras;
    std::uint64_t selectedEntity = 0; // 0 when no camera is selected
    std::vector<OverlaySegment2D> selectedFrustum; // image pixels
    SceneCameraCullView cullView;
};

class ISceneCameraControl {
public:
    virtual ~ISceneCameraControl() = default;

    virtual SceneCameraReport Report() const = 0;
    virtual SceneCameraResult Select(const std::string& entityName) = 0;

    // Camera pick at a point (x, y in 0..1) of the Scene image.
    virtual SceneCameraPickResult Pick(float x, float y) = 0;
    virtual std::optional<ClusterViewerImage> CaptureImage(bool overlay) = 0;
};

} // namespace gte
