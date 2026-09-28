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
#include <functional>
#include <string>

namespace gte {

// editor-core-separation-14 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 3), PHASE2. The value-type result of one full build attempt
// (both TARGETS: _Game, then _Editor if present) - shared by BOTH the
// existing async path (RunBuildThreadBody) and the new synchronous path
// below (TryRunProjectAssemblyBuildSynchronously).
struct BuildOutcome {
    bool success = false;
    int exitCode = 0;
    // Deliberately NOT the existing async path's own "relaunch
    // GreatTamanaEditor.exe..." wording (that advice is wrong for a
    // hot-reload caller) - each CALLER writes its own final summary log
    // line using this struct's exitCode/success; this field is reserved
    // for a future caller that wants a single ready-made human string
    // without composing one itself (unused by this phase - a future
    // orchestrator phase composes its own line instead).
    std::string finalSummaryLine;
};

// Builds <projectName>_Game (then _Editor, if that target exists),
// IDENTICAL sequencing/target-missing heuristic as the existing async
// path (RunBuildThreadBody) - BLOCKS THE CALLING THREAD for the whole
// duration. Does NOT touch the per-project in-flight guard itself - see
// TryRunProjectAssemblyBuildSynchronously() below for the one sanctioned
// way a NEW caller uses this together with that guard; RunBuildThreadBody
// (the EXISTING async path) continues to guard it exactly as it already
// does today, unchanged. `onIdleTick`, if provided, is invoked from
// RunOneBuildTarget()'s own poll loop on a fixed ~50ms cadence whenever no
// build output is currently available - see that function's own updated
// doc comment (PHASE2) for why. Pass an empty std::function (the default)
// from any call site that is NOT running on the main/window-owning thread
// (a background thread has no window to pump messages for - doing so
// would be a pointless no-op, never do it).
BuildOutcome RunProjectAssemblyBuildAndWait(const std::string& projectName, const std::string& buildDirectory,
    const std::function<void()>& onIdleTick = {});

// The synchronous counterpart of TriggerProjectAssemblyCompile() - shares
// the EXACT SAME per-project in-flight guard (g_inFlightProjects) as that
// existing async path: a hot-reload request for a project whose own async
// "Compile" build (the button OR /compile_only) is ALREADY running is
// rejected here (returns false, outOutcome left untouched), exactly
// mirroring TriggerProjectAssemblyCompile()'s own existing rejection of a
// second overlapping async request - and vice versa (a fresh async
// request is rejected while THIS function's own synchronous build is
// still running), since both now go through the SAME g_inFlightProjects
// set. Returns true once the build genuinely ran to completion (regardless
// of whether it succeeded - check outOutcome.success for that).
bool TryRunProjectAssemblyBuildSynchronously(const std::string& projectName, const std::string& buildDirectory,
    BuildOutcome& outOutcome, const std::function<void()>& onIdleTick = {});

// Kicks off an ASYNCHRONOUS build of `<projectName>_Game` and (if it
// exists) `<projectName>_Editor` against `buildDirectory`. Returns
// immediately - the actual build happens on a background thread. Safe to
// call again while a previous build for a DIFFERENT project is still
// running; calling it again for the SAME project while its own previous
// build is still in flight is a harmless, logged no-op (see this file's
// own .cpp for the simple, single-flag-per-project guard), never two
// overlapping child processes racing each other's output.
// editor-core-separation-12 campaign, PHASE2 (Project Assembly Hot Reload
// plan, BIG-STEP 1) - return type changed from void to bool so a caller
// (EditorHotReloadDebugCapability::TriggerCompileOnly()) can honestly
// report "started" vs. "rejected, a build for this project is already in
// flight" - confirmed zero existing callers anywhere in this codebase
// before this change, so this is a zero-risk signature change. Returns
// true the moment the background build thread is actually registered
// (before any compiler output exists yet) - false only when
// TryMarkInFlight() rejects it.
bool TriggerProjectAssemblyCompile(const std::string& projectName, const std::string& buildDirectory);

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

// editor-core-separation-13 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 2), PHASE4. Appends "project_assemblies" onto a CALLER-SUPPLIED
// executable directory - the ONE place this joining happens, replacing the
// identical inline expression at src/Editor/EditorHost.cpp's own
// LoadProjectAssemblies() call site (updated by this phase to call this
// function instead, to avoid a second, independently-drifting copy of the
// same path literal). Deliberately takes `executableDirectory` as an
// EXPLICIT, REQUIRED parameter, mirroring ResolveCMakeBuildDirectory()'s
// own "take the starting directory as a parameter, never resolve it
// internally" precedent immediately above - see this phase's own layering
// note (PHASE4_PROJECT_ASSEMBLY_HOST_UNLOAD_GPU_SAFETY_AND_BINARY_BACKUP.md,
// section 3.4) for exactly why (gte::ExecutableDirectory() is
// gte_editor-tier; this file is gte_core-tier).
std::filesystem::path ResolveProjectAssemblyOutputDirectory(const std::filesystem::path& executableDirectory);

// Copies the CURRENT, presumed-good <projectName>_Game.dll (and, if
// present, _Editor.dll) from `outputDirectory` to a dedicated backup slot,
// <outputDirectory>/.hotreload_backup/<name>_Game.dll.bak (/_Editor.dll.bak)
// - a plain file copy, safe to perform WHILE the original is still
// LoadLibraryW()'d (an already-mapped .dll permits shared-read access; only
// a WRITE-mode open, e.g. the linker overwriting it, is blocked).
// `outputDirectory` is an EXPLICIT, REQUIRED parameter (never defaulted to
// an internally-resolved value - see this phase's own layering note above)
// - the real, production caller passes
// ResolveProjectAssemblyOutputDirectory(gte::ExecutableDirectory()); a test
// passes a throwaway temp directory instead, with zero special-casing
// needed on either side. MUST be called BEFORE
// ProjectAssemblyHost::UnloadProjectAssembly() for the SAME projectName,
// every hot-reload cycle - the backup is unconditionally OVERWRITTEN each
// time. Returns false (GTE_LOG_ERROR, never throws) if the copy fails for
// any reason - the caller MUST treat false as "abort before ever calling
// UnloadProjectAssembly()".
bool BackupProjectAssemblyBinaries(const std::string& projectName, const std::filesystem::path& outputDirectory);

// The rollback half - copies the .hotreload_backup/<name>_*.dll.bak files
// BACK, overwriting whatever a failed compile may have left at the real
// <outputDirectory>/<name>_*.dll paths. Safe ONLY after
// UnloadProjectAssembly() has already run for this project (nothing has the
// target path open). Same explicit, required `outputDirectory` parameter as
// BackupProjectAssemblyBinaries() above, for the identical reason. Returns
// false (GTE_LOG_ERROR) if the backup itself is missing/unreadable.
bool RestoreProjectAssemblyBinariesFromBackup(const std::string& projectName, const std::filesystem::path& outputDirectory);

} // namespace gte
