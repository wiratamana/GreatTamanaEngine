#pragma once

#include <filesystem>
#include <string>

namespace gte {

class Core;
class Renderer;
class EditorHost;

// editor-core-separation-14 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 3). THE orchestrator - see PHASE4_ORCHESTRATOR_AND_ROUTE_WIRING.md
// for this function's REAL, permanent body. PHASE3 (this file) only
// declares the PERMANENT signature and gives it a TEMPORARY, honest,
// minimal body so EditorHost::Run()'s new drain point (PHASE3) and every
// piece of wiring around it can compile and be smoke-tested THIS phase,
// before PHASE4's real pause/backup/unload/compile/reload logic exists.
// Called SYNCHRONOUSLY from EditorHost::Run()'s own main loop - blocks the
// calling (main) thread for its entire duration once PHASE4 replaces this
// body (LDD-HR4). Targets exactly one project (LDD-HR5).
// `outputDirectory`/`buildDirectory` are RESOLVED BY THE CALLER
// (EditorHost::Run()'s drain point, gte_editor-tier) and handed in as plain
// values - this function must NEVER call gte::ExecutableDirectory() itself:
// this file lives in src/Core/Plugins/, compiled into gte_core (see every
// OTHER file in this same folder in CMakeLists.txt's gte_core source list),
// and gte::ExecutableDirectory() is gte_editor-tier (defined only in
// src/Editor/ProjectRootPath.cpp, never linked into gte_core alone) -
// mirrors ResolveProjectAssemblyOutputDirectory()/ResolveCMakeBuildDirectory()'s
// own identical "take the resolved directory as an explicit parameter,
// never resolve it internally" precedent (ProjectAssemblyBuildRunner.h),
// for the exact same layering reason.
void PerformProjectAssemblyHotReload(const std::string& projectName, Core& core, Renderer& renderer,
    EditorHost* editorHost, const std::filesystem::path& outputDirectory, const std::filesystem::path& buildDirectory);

} // namespace gte
