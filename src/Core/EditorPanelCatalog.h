#pragma once

#include <cstddef>
#include <string>

// Canonical, exhaustive list of every named Editor panel this engine's
// default dock layout creates (see DockLayout.cpp's own
// BuildDefaultDockLayout()/kAllPanelNames) - the SAME literal strings,
// factored out here so DockLayout.cpp and the Network layer's
// GET /activate_tab / GET /list_tabs routes (see NetworkRoutes.cpp,
// network-impl-7 campaign) can never silently drift apart from each other.
//
// Lives under src/Core/ (relocated from src/Editor/ by the
// editor-core-separation-2 campaign, PHASE1) because it is pure,
// gte_core-owned compile-time data (the canonical list of known Editor
// panel names) that BOTH gte_core-tier code (src/Network/NetworkRoutes.h,
// for GET /activate_tab/GET /list_tabs) and gte_editor-tier code
// (src/Editor/DockLayout.cpp, for building the default dock layout) need to
// agree on - the single source of truth belongs in gte_core precisely
// because it must be reachable from both tiers, and only gte_core is
// visible from both.
//
// GTE_ENABLE_PROJECT_PANEL is a PUBLIC compile definition on the gte_core
// target (see CMakeLists.txt's own target_compile_definitions() call), so
// it is visible here exactly as it already is inside DockLayout.cpp.
namespace gte {

inline constexpr const char* kKnownEditorPanelNames[] = {
    "Hierarchy",
    "Inspector",
    "Scene",
    "Game",
    "Memory",
    "Profiler",
    "Render Graph",
    "Jobs",
    "Atmosphere",
    "Log",
#if GTE_ENABLE_PROJECT_PANEL
    "Project",
#endif
};

inline constexpr std::size_t kKnownEditorPanelNameCount =
    sizeof(kKnownEditorPanelNames) / sizeof(kKnownEditorPanelNames[0]);

// Pure, case-sensitive, exact-string-match lookup - the ONE shared
// definition of "is this name one GET /activate_tab / GET /list_tabs are
// allowed to talk about" (see NetworkRoutes.cpp's ParseActivateTabQuery(),
// Phase 4). Deliberately case-sensitive, matching every existing exact-
// match convention in NetworkRoutes.cpp (see e.g. ParseGetTextureQuery()'s
// own "color"/"depth" exact-lowercase-only rule and its accompanying
// comment on why this codebase is consistently exact-case throughout this
// one file).
inline bool IsKnownEditorPanelName(const std::string& name) noexcept
{
    for (const char* candidate : kKnownEditorPanelNames) {
        if (name == candidate) {
            return true;
        }
    }
    return false;
}

} // namespace gte
