#include "SceneOverlay.h"

namespace gte {

namespace {

constexpr float kBodyWidth = 1.5f;
constexpr float kHoverWidth = 2.5f;
constexpr OverlayColor kHoverColor{ 255, 255, 255, 255 };

} // namespace

void AppendSceneOverlay(const SceneOverlayInput& input, Vec2 rectMin, Vec2 rectMax, std::vector<OverlaySegment2D>& segments,
    std::vector<OverlayQuad2D>& quads)
{
    for (const CameraPickCandidate& candidate : input.candidates) {
        const bool isSelected = candidate.id == input.selectedId;
        const bool isHover = input.hoverId && *input.hoverId == candidate.id;
        const OverlayColor color = isSelected ? kSceneCameraSelectedColor : (isHover ? kHoverColor : kSceneCameraBodyColor);
        AppendCameraOverlay(candidate.frustum, isSelected, input.viewProjection, rectMin, rectMax, color,
            isHover ? kHoverWidth : kBodyWidth, segments, isSelected ? &quads : nullptr);
    }
}

} // namespace gte
