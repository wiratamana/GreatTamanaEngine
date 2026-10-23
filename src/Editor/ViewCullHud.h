#pragma once

// Text lines of the Scene cull view HUD. Pure: no ImGui, no Vulkan.

#include "../Core/FeatureKit/ViewCullLink.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gte {

enum class ViewCullHudTone : std::uint8_t { Normal, Dim, Good, Warn };

struct ViewCullHudLine {
    std::string text;
    ViewCullHudTone tone = ViewCullHudTone::Normal;
};

// At most 12 short lines. Counts come from one frame (the visible count's own frame), never a wrapped difference.
std::vector<ViewCullHudLine> FormatViewCullHud(const fx::ViewCullReport& report);

} // namespace gte
