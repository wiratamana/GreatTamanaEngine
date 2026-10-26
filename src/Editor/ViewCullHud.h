#pragma once

// Text lines and pure request logic of the Scene cull view. No ImGui, no Vulkan.

#include "ViewCullFreshness.h"
#include "../Core/FeatureKit/ViewCullLink.h"

#include <array>
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

// True when the report equals the all-default cull state.
constexpr bool ViewCullReportIsDefault(const fx::ViewCullReport& r) noexcept
{
    return r.choice == fx::ViewCullChoice::FollowSettings && r.source == fx::ViewCullSource::Live && !r.cone;
}

// Starts or continues a user change. A pending reset is replaced: the change starts from defaults.
fx::ViewCullRequest& BeginViewCullChange(fx::ViewCullLink& link);

struct ViewCullChoiceOption {
    fx::ViewCullChoice choice;
    bool enabled;
};

// The combo entries in display order. Frustum + Hi-Z is disabled while the cull source is frozen.
std::array<ViewCullChoiceOption, 4> ViewCullChoiceOptions(const fx::ViewCullReport& report);

} // namespace gte
