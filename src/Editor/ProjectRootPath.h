#pragma once

#include <filesystem>

namespace gte {

// Resolves the one "Project" folder every Editor feature that needs a
// stable, on-disk authoring location uses - the directory containing the
// built .exe (via SDL_GetBasePath()), plus a "Project" subfolder, exactly
// matching Panels/ProjectPanel.cpp's own long-standing convention. Pulled
// out as its own small helper - GTE_ENABLE_EDITOR no longer exists anywhere
// in this codebase (i.e. NOT gated behind the separate GTE_ENABLE_PROJECT_PANEL switch) so
// any core Editor feature - not just the "Project" panel itself - can
// resolve the same folder consistently. See
// task_manager/scene-serialization-1/PHASE5_EDITOR_SCENE_IO_AND_PROJECT_ROOT_HELPER.md
// for why this had to be extracted rather than reused from
// ProjectPanelData.h directly (that header is GTE_ENABLE_PROJECT_PANEL-only).
// Never throws; always returns SOME path (falls back to "./Project" if
// SDL can't determine the executable's own directory for some reason).
std::filesystem::path ResolveProjectRootDirectory();

// editor-core-separation-3 campaign, PHASE2
// (PHASE2_PLUGIN_HOST_AND_HELLO_WORLD_HANDSHAKE_PROBE.md) - the plain
// "directory containing the built .exe" itself, WITHOUT the "Project"
// subfolder ResolveProjectRootDirectory() appends above - PluginHost's own
// plugins/ folder lives directly next to the .exe, not inside "Project".
// Shares the exact same SDL_GetBasePath()-based resolution/fallback logic as
// ResolveProjectRootDirectory() (see ProjectRootPath.cpp) - never throws,
// always returns SOME path (falls back to "." if SDL can't determine the
// executable's own directory for some reason).
std::filesystem::path ExecutableDirectory();

} // namespace gte
