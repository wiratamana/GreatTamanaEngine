#pragma once

#include <filesystem>

namespace gte {

// Resolves the one "Project" folder every Editor feature that needs a
// stable, on-disk authoring location uses - the directory containing the
// built .exe (via SDL_GetBasePath()), plus a "Project" subfolder, exactly
// matching Panels/ProjectPanel.cpp's own long-standing convention. Pulled
// out as its own small, UNCONDITIONALLY-GTE_ENABLE_EDITOR-compiled helper
// (i.e. NOT gated behind the separate GTE_ENABLE_PROJECT_PANEL switch) so
// any core Editor feature - not just the "Project" panel itself - can
// resolve the same folder consistently. See
// task_manager/scene-serialization-1/PHASE5_EDITOR_SCENE_IO_AND_PROJECT_ROOT_HELPER.md
// for why this had to be extracted rather than reused from
// ProjectPanelData.h directly (that header is GTE_ENABLE_PROJECT_PANEL-only).
// Never throws; always returns SOME path (falls back to "./Project" if
// SDL can't determine the executable's own directory for some reason).
std::filesystem::path ResolveProjectRootDirectory();

} // namespace gte
