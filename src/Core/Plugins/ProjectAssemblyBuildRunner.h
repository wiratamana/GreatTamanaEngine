// src/Core/Plugins/ProjectAssemblyBuildRunner.h
//
// editor-core-separation-11 campaign (Project Assembly system), PHASE6
// (PHASE6_COMPILE_TRIGGER_AND_BUILD_AUTOMATION.md). Runs `cmake --build` as
// a real child process on a dedicated background thread
// (JobSystem::RegisterBackgroundThread() - NEVER JobSystem::Schedule(),
// which would tie up a fixed worker-pool thread for the build's entire,
// potentially multi-minute duration - see this phase's own doc file for the
// full reasoning, and JobContinuation.cpp's WatchDependencyWithFallback()
// for the real, working precedent this mirrors), streaming its
// stdout/stderr into GTE_LOG_INFO/GTE_LOG_WARNING/GTE_LOG_ERROR
// ("ProjectAssemblyBuild" category) as it runs, never a raw printf/console
// window.
//
// SHUTDOWN BEHAVIOR (this phase's own flagged open design decision,
// resolved as option (a) - "let it block"): this file registers its
// background thread with JobSystem exactly like JobContinuation.cpp's
// polling-fallback thread does, but, UNLIKE that thread, it never polls
// JobSystem::IsShuttingDown() and never calls TerminateProcess() on the
// child. If the user closes GreatTamanaEditor.exe while a build is still
// running, process exit BLOCKS until the child `cmake --build` process
// actually finishes and this thread's own blocking ReadFile() loop returns
// (JobSystem::~JobSystem()'s JoinAllBackgroundThreads() call). This is
// deliberate: forcibly killing a `cmake --build` child mid-write risks
// leaving a half-written, corrupt `_Game.dll`/`_Editor.dll` on disk, which
// this whole system's LDD4 ("Project Assemblies are never hot-reloaded,
// only picked up on next launch") implicitly assumes never happens - a
// build that is still in flight when the user closes the Editor will
// simply finish first, exactly like waiting for any other save-to-disk
// operation to complete before quitting. See this phase's own
// PHASE6_COMPLETION_REPORT.md for the full decision record.
#pragma once

#include <filesystem>
#include <string>

namespace gte {

// Kicks off an ASYNCHRONOUS build of `<projectName>_Game` and (if it
// exists) `<projectName>_Editor` against `buildDirectory`. Returns
// immediately - the actual build happens on a background thread. Safe to
// call again while a previous build for a DIFFERENT project is still
// running; calling it again for the SAME project while its own previous
// build is still in flight is a harmless, logged no-op (see this file's
// own .cpp for the simple, single-flag-per-project guard), never two
// overlapping child processes racing each other's output.
void TriggerProjectAssemblyCompile(const std::string& projectName, const std::string& buildDirectory);

// Walks upward from `startDirectory` (typically gte::ExecutableDirectory())
// looking for a real CMakeCache.txt file, up to `maxParentLevels` parent
// directories (default 5) - see this phase's own doc file's "STEP 2"
// reasoning: the CMake BUILD directory cannot be CMAKE_BINARY_DIR (a
// configure-time-only CMake variable, unavailable to running C++ code), and
// must never be hardcoded as a single ".." (this correctly handles both a
// Ninja single-config tree, confirmed today, and a Visual-Studio-style
// multi-config tree's differing folder depths, without hardcoding either
// shape). Returns an empty path (after logging one GTE_LOG_ERROR) if no
// CMakeCache.txt is found within the bound.
std::filesystem::path ResolveCMakeBuildDirectory(const std::filesystem::path& startDirectory, int maxParentLevels = 5);

} // namespace gte
