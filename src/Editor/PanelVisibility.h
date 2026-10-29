#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace gte {

// Panels whose visibility a debug caller may switch for the running session.
enum class TogglePanel : std::uint8_t { Hierarchy, Count };

inline constexpr std::size_t kTogglePanelCount = static_cast<std::size_t>(TogglePanel::Count);
inline constexpr std::array<std::string_view, kTogglePanelCount> kTogglePanelNames{ "Hierarchy" };

inline constexpr std::string_view TogglePanelName(TogglePanel panel) noexcept
{
    return kTogglePanelNames[static_cast<std::size_t>(panel)];
}

// Exact, case-sensitive match against kTogglePanelNames.
inline std::optional<TogglePanel> TogglePanelFromName(std::string_view name) noexcept
{
    for (std::size_t i = 0; i < kTogglePanelCount; ++i) {
        if (kTogglePanelNames[i] == name) {
            return static_cast<TogglePanel>(i);
        }
    }
    return std::nullopt;
}

// Panels hidden for this session. Never saved.
struct PanelHiddenFlags {
    std::array<bool, kTogglePanelCount> hidden{};

    bool IsHidden(TogglePanel panel) const noexcept { return hidden[static_cast<std::size_t>(panel)]; }
    void Set(TogglePanel panel, bool isHidden) noexcept { hidden[static_cast<std::size_t>(panel)] = isHidden; }
};

// A hidden panel is skipped before it does any work.
inline bool ShouldBuildPanel(const PanelHiddenFlags& flags, TogglePanel panel) noexcept
{
    return !flags.IsHidden(panel);
}

} // namespace gte
