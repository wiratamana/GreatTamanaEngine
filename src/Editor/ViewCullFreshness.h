#pragma once

// Detects a report whose frame counter stopped moving. Pure: no ImGui.

#include <cstdint>

namespace gte {

inline constexpr std::uint64_t kViewCullStaleUiFrames = 4;

struct ViewCullFreshness {
    bool seen = false;
    std::uint64_t reportFrame = 0;
    std::uint64_t changedAt = 0;
    std::uint64_t lastUi = 0;

    // Call once per UI frame while the panel is drawn. A gap in UI frames restarts the watch.
    // True when the report frame stood still for more than kViewCullStaleUiFrames UI frames.
    bool Update(std::uint64_t currentReportFrame, std::uint64_t uiFrame) noexcept
    {
        if (!seen || uiFrame != lastUi + 1 || currentReportFrame != reportFrame) {
            seen = true;
            reportFrame = currentReportFrame;
            changedAt = uiFrame;
        }
        lastUi = uiFrame;
        return uiFrame - changedAt > kViewCullStaleUiFrames;
    }
};

// UI-only state of the Scene panel cull box.
struct ScenePanelCullViewUi {
    bool open = false; // The user opened the box while every setting is still at its default.
    ViewCullFreshness freshness;
    bool stale = false; // Rewritten every UI frame by the panel.
};

} // namespace gte
