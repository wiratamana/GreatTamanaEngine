#pragma once

// Placement of the cull view box over a view image. Pure: no ImGui.

#include "../Math/Vec2.h"

#include <optional>

namespace gte {

inline constexpr float kViewCullMargin = 8.0f;
inline constexpr float kViewCullMaxWidth = 300.0f;
inline constexpr float kViewCullMinWidth = 160.0f;
inline constexpr float kViewCullMinHeight = 64.0f;

struct ViewCullBox {
    Vec2 min;
    float width = 0.0f;
    float maxHeight = 0.0f;
};

// Top-right box below the switcher row; empty when the image is too small to show it.
std::optional<ViewCullBox> PlaceViewCullBox(Vec2 imageMin, Vec2 imageSize, float switcherBottom);

} // namespace gte
