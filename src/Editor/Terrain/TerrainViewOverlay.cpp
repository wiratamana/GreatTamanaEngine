#include "TerrainViewOverlay.h"

#include "TerrainOverlayData.h"
#include "../CameraFrustumOverlay.h"

#include <imgui.h>

#include <array>
#include <vector>

namespace gte {

void DrawTerrainBoundsOverlay(const TerrainUiBridge& bridge, const Mat4& viewProjection, Vec2 rectMin, Vec2 rectMax)
{
    const TerrainUiSnapshot& snapshot = bridge.snapshot;
    if (!snapshot.open || !snapshot.debugAabb || snapshot.nodes.empty()) {
        return;
    }
    std::array<std::vector<FrustumSegment>, kOverlayBucketCount> buckets;
    for (const terrain::TerrainNodeInfo& node : snapshot.nodes) {
        AppendNodeBoxSegments(node, buckets[static_cast<std::size_t>(OverlayBucketOf(node, bridge.overlayColorByState))]);
    }
    std::vector<OverlaySegment2D> segments;
    for (int bucket = 0; bucket < kOverlayBucketCount; ++bucket) {
        ProjectSegments(buckets[static_cast<std::size_t>(bucket)], viewProjection, rectMin, rectMax, OverlayBucketColor(bucket), 1.0f, segments);
    }
    DrawOverlaySegments(ImGui::GetWindowDrawList(), segments, rectMin, rectMax);
}

void GatherTerrainCameraInput(TerrainUiBridge& bridge, bool imageHovered)
{
    const TerrainUiSnapshot& snapshot = bridge.snapshot;
    if (!snapshot.open || snapshot.cameraMode == terrain::CameraMode::Script) {
        bridge.lookCaptured = false;
        return;
    }
    const bool orbit = snapshot.cameraMode == terrain::CameraMode::Orbit;
    const ImGuiIO& io = ImGui::GetIO();
    const bool dragButtonDown = ImGui::IsMouseDown(ImGuiMouseButton_Right) || (orbit && ImGui::IsMouseDown(ImGuiMouseButton_Left));
    const bool dragStarted = ImGui::IsMouseClicked(ImGuiMouseButton_Right) || (orbit && ImGui::IsMouseClicked(ImGuiMouseButton_Left));
    if (imageHovered && dragStarted) {
        bridge.lookCaptured = true;
    }
    if (!dragButtonDown) {
        bridge.lookCaptured = false;
    }
    const bool active = imageHovered || bridge.lookCaptured;
    if (!active || io.WantTextInput) {
        return;
    }

    terrain::CameraInput input;
    auto axis = [](ImGuiKey positive, ImGuiKey negative) {
        return (ImGui::IsKeyDown(positive) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(negative) ? 1.0f : 0.0f);
    };
    input.moveForward = axis(ImGuiKey_W, ImGuiKey_S) + axis(ImGuiKey_UpArrow, ImGuiKey_DownArrow);
    input.moveRight = axis(ImGuiKey_D, ImGuiKey_A) + axis(ImGuiKey_RightArrow, ImGuiKey_LeftArrow);
    input.moveUp = axis(ImGuiKey_E, ImGuiKey_Q);
    input.boost = io.KeyShift;
    input.look = bridge.lookCaptured;
    if (bridge.lookCaptured) {
        input.lookDeltaX = io.MouseDelta.x;
        input.lookDeltaY = io.MouseDelta.y;
    }
    input.wheel = imageHovered ? io.MouseWheel : 0.0f;

    const bool any = input.moveForward != 0.0f || input.moveRight != 0.0f || input.moveUp != 0.0f || input.look || input.wheel != 0.0f;
    if (any) {
        terrain::AccumulateInput(bridge.request.cameraInput, input);
        bridge.request.hasCameraInput = true;
    }
}

} // namespace gte
