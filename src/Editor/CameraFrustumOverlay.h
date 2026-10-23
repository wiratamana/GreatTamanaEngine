#pragma once

// Camera frustum lines over a rendered image: builds the 2D segment list and draws it with ImGui.

#include "ViewportProjection.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

struct ImDrawList;

namespace gte {

using OverlayColor = std::array<std::uint8_t, 4>;

inline constexpr OverlayColor kDebugCameraColor{ 255, 220, 60, 255 };
inline constexpr OverlayColor kDebugCameraSelectedColor{ 255, 120, 40, 255 };
inline constexpr OverlayColor kSceneCameraViewerColor{ 80, 220, 255, 255 };
inline constexpr OverlayColor kSceneCameraBodyColor{ 230, 230, 230, 255 };
inline constexpr OverlayColor kSceneCameraSelectedColor{ 255, 120, 40, 255 };

// Projects the camera into [rectMin, rectMax) and appends its lines to `out`.
// `fullFrustum` false keeps only the body (apex lines and the direction arrow).
void AppendCameraOverlay(const FrustumInput& camera, bool fullFrustum, const Mat4& viewProjection, Vec2 rectMin,
    Vec2 rectMax, OverlayColor color, float width, std::vector<OverlaySegment2D>& out);

// Draws screen-space segments clipped to [clipMin, clipMax).
void DrawOverlaySegments(ImDrawList* drawList, std::span<const OverlaySegment2D> segments, Vec2 clipMin, Vec2 clipMax);

} // namespace gte
