#pragma once

// Scene overlay lines shared by the Scene panel and the HTTP image: camera bodies and the selected frustum.
// Pure geometry: no ImGui, no Vulkan.

#include "CameraFrustumOverlay.h"
#include "CameraPick.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace gte {

struct SceneOverlayInput {
    std::span<const CameraPickCandidate> candidates;
    std::uint64_t selectedId = 0; // 0 when no camera is selected.
    std::optional<std::uint64_t> hoverId;
    Mat4 viewProjection = Mat4::Identity(); // The Scene camera, flipY = true.
};

// Every camera body and the selected frustum (with far fill), projected into [rectMin, rectMax).
void AppendSceneOverlay(const SceneOverlayInput& input, Vec2 rectMin, Vec2 rectMax, std::vector<OverlaySegment2D>& segments,
    std::vector<OverlayQuad2D>& quads);

} // namespace gte
