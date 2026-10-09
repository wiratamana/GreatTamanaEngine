#include "ProjectAssemblyHotReload.h"

#include "../Core.h"
#include "../Logging.h"
#include "ProjectAssemblyBuildRunner.h"
#include "ProjectAssemblyHotReloadDebugStatus.h"
#include "../../Renderer/Renderer.h"
#include "../../Scene/SceneBuilder.h"
// Deliberately NO "../../Editor/ProjectRootPath.h" include here, and NO call
// to gte::ExecutableDirectory() anywhere in this file - this is gte_core-tier
// code (src/Core/Plugins/, same CMake source list as ProjectAssemblyHost.cpp/
// ProjectAssemblyBuildRunner.cpp) and gte::ExecutableDirectory() is
// gte_editor-tier - see this function's own outputDirectory/buildDirectory
// PARAMETERS (resolved by the caller, EditorHost::Run(), PHASE3) and
// PHASE0_MASTER_STRATEGY.md Section 2.2 item 6 for the full reasoning.

#include <windows.h>

namespace gte {

// editor-core-separation-15 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 4), PHASE2 - HOOK POINT A's real body. Refreshes
// core.GetAssetDatabase() exactly ONCE per cycle (LDD-HR7), then builds a
// full SceneDocument snapshot of the live world via
// Scene/SceneBuilder.h's BuildSceneDocumentFromRegistry() - the SAME
// function EditorHotReloadDebugCapability::BuildSceneSnapshotJson() (GET
// /project_assembly/debug/scene_snapshot) already uses for the identical
// live world.
HotReloadStateSnapshot CaptureProjectAssemblyHotReloadState(Core& core, const std::filesystem::path& projectRootDirectory)
{
    // LDD-HR7 - refreshed here, exactly once per cycle. Safe even if
    // projectRootDirectory doesn't exist (RefreshFromDirectory()'s own
    // existing, already-relied-upon tolerant behavior - mirrors
    // Editor/SceneIO.cpp's own SaveScene()'s identical comment).
    core.GetAssetDatabase().RefreshFromDirectory(projectRootDirectory);

    HotReloadStateSnapshot snapshot;
    snapshot.document = BuildSceneDocumentFromRegistry(core.GetGame().GetRegistry(), core.GetAssetDatabase());

    GTE_LOG_INFO("ProjectAssemblyHotReload",
        "CaptureProjectAssemblyHotReloadState: captured " + std::to_string(snapshot.document.entities.size()) + " entities.");
    return snapshot;
}

// editor-core-separation-15 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 4), PHASE3 - HOOK POINT B's real body. Reuses the SAME
// Scene/SceneBuilder.h ClearEntireScene() + ReconstructSceneFromDocument()
// the Editor's own Ctrl+O LoadScene() (Editor/SceneIO.cpp) now goes
// through.
void RestoreProjectAssemblyHotReloadState(Core& core, Renderer& renderer, const HotReloadStateSnapshot& snapshot,
    const std::filesystem::path& /*projectRootDirectory*/)
{
    // LDD-HR7 - reuses the SAME core.GetAssetDatabase() instance
    // CaptureProjectAssemblyHotReloadState() already refreshed earlier in
    // THIS SAME cycle (PHASE2) - deliberately NOT refreshed a second time
    // here (nothing could have added a new on-disk asset during the
    // freeze - the whole engine, including any file-watching, was frozen
    // solid the entire time, LDD-HR4).
    ClearEntireScene(core.GetGame().GetRegistry());
    ReconstructSceneFromDocument(
        core.GetGame(), renderer, snapshot.document, core.GetAssetDatabase(), core.GetSceneServicesDescriptorSet().Layout());

    GTE_LOG_INFO("ProjectAssemblyHotReload",
        "RestoreProjectAssemblyHotReloadState: restored " + std::to_string(snapshot.document.entities.size()) + " entities.");
}

namespace {

// editor-core-separation-14 campaign, PHASE4. Keeps Windows from marking
// the main window "Not Responding" during a long, deliberately-blocking
// hot-reload compile (LDD-HR4) - PURELY cosmetic OS bookkeeping, never
// touches game/render state, never calls SDL_PollEvent() (which would let a
// queued input event leak into a "frame" that, per LDD-HR4, must not run
// any game/render logic this iteration - this function is called from
// INSIDE TryRunProjectAssemblyBuildSynchronously()'s own poll loop, NOT
// from EditorHost::Run()'s own per-frame SDL_PollEvent() block).
//
// hWnd = nullptr is DELIBERATE, not "forgot to filter": ImGui multi-viewport
// is enabled in this codebase (ImGuiConfigFlags_ViewportsEnable,
// ImGuiEditorLayer.cpp) - any panel dragged outside the main OS window
// becomes its own real, extra SDL3-created top-level window (src/Window/
// Window.h's own header comment), created/destroyed on THIS SAME (main)
// thread by imgui_impl_sdl3.cpp's platform callbacks. A NULL hWnd makes
// PeekMessage() service EVERY window this thread owns, not only the main
// one - restricting this to just the main window's own HWND would starve
// any currently-torn-off panel window of its own message pump, so IT would
// be the one Windows marks "Not Responding" instead.
//
// EVERY message, including WM_CLOSE, is dispatched normally below -
// deliberately NOT swallowed. This is safe: SDL3's own Win32 window
// procedure handles WM_CLOSE by pushing an SDL_EVENT_WINDOW_CLOSE_REQUESTED
// event into SDL's OWN internal event queue and returning - it never calls
// DestroyWindow()/PostQuitMessage() itself, and nothing reads that queue
// until EditorHost::Run()'s own per-frame SDL_PollEvent() call, which
// cannot run until this whole synchronous freeze returns. "nothing is acted
// on until the cycle finishes" therefore already holds for every message
// type, with zero special-casing needed - dropping WM_CLOSE via a
// `continue` would be a real bug, not a safe simplification:
// PeekMessage(..., PM_REMOVE) permanently removes the message from the
// queue, so skipping DispatchMessage() for it means SDL's own window
// procedure never runs for that click at all and the close request is
// silently LOST forever (the user has to click the close button again
// after the freeze ends), not merely deferred as intended.
void PumpWindowsMessagesDuringHotReloadFreeze()
{
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

} // namespace

void PerformProjectAssemblyHotReload(const std::string& projectName, Core& core, Renderer& renderer,
    EditorHost* editorHost, const std::filesystem::path& outputDirectory, const std::filesystem::path& buildDirectory,
    const std::filesystem::path& projectRootDirectory)
{
    GTE_LOG_INFO("ProjectAssemblyHotReload", "Hot reload requested for '" + projectName + "' - freezing engine.");
    ProjectAssemblyHotReloadDebugStatus::Instance().Set("CapturingState", projectName);

    const HotReloadStateSnapshot snapshot = CaptureProjectAssemblyHotReloadState(core, projectRootDirectory);

    if (buildDirectory.empty()) {
        GTE_LOG_ERROR("ProjectAssemblyHotReload", "Aborting - could not resolve the CMake build directory for '" + projectName + "'.");
        ProjectAssemblyHotReloadDebugStatus::Instance().Finish("CriticalFailure", "could not resolve CMake build directory - nothing changed");
        return;
    }

    ProjectAssemblyHotReloadDebugStatus::Instance().Set("BackingUpBinaries", projectName);
    if (!BackupProjectAssemblyBinaries(projectName, outputDirectory)) {
        GTE_LOG_ERROR("ProjectAssemblyHotReload", "Aborting - could not back up current binaries for '" + projectName + "'.");
        ProjectAssemblyHotReloadDebugStatus::Instance().Finish("CriticalFailure", "backup failed before any teardown - nothing changed");
        return;
    }

    ProjectAssemblyHotReloadDebugStatus::Instance().Set("Unloading", projectName);
    // Locks GetHotReloadEngineStateMutex() INTERNALLY (PHASE1) - do not
    // wrap this call in an outer lock on the same mutex (non-recursive,
    // would deadlock).
    core.GetProjectAssemblyHost().UnloadProjectAssembly(projectName, core, renderer);

    ProjectAssemblyHotReloadDebugStatus::Instance().Set("Compiling", projectName);
    BuildOutcome outcome;
    const bool buildRan = TryRunProjectAssemblyBuildSynchronously(
        projectName, buildDirectory.string(), outcome, &PumpWindowsMessagesDuringHotReloadFreeze);
    if (!buildRan) {
        // Rejected by the shared in-flight guard (an async "Compile" build
        // for this SAME project is already running) - the project is
        // ALREADY unloaded at this point (see above), so this is not a
        // harmless no-op like a rejected async trigger would be; treat it
        // as a compile failure and fall through to the rollback path
        // below, exactly like any other failed-to-produce-a-good-binary
        // case. KNOWN, ACCEPTED NARROW RISK: if that OTHER, still-running
        // async build's own child `cmake --build` process still holds an
        // open write handle on this SAME project's output .dll at this
        // exact moment, the rollback below's file copy can race it (most
        // likely outcome: the copy fails with a sharing violation, which
        // RestoreProjectAssemblyBinariesFromBackup()'s own return value
        // check below turns into an honest CriticalFailure rather than a
        // silently-wrong result) - not fully closable from this phase
        // alone without PHASE2 exposing a plain, pre-flight "is this
        // project already in flight" query, which does not exist today.
        GTE_LOG_ERROR("ProjectAssemblyHotReload",
            "Synchronous build for '" + projectName + "' was rejected (already building elsewhere) - rolling back.");
        outcome.success = false;
    } else {
        GTE_LOG_INFO("ProjectAssemblyHotReload",
            "Build finished with exit code " + std::to_string(outcome.exitCode) +
            " - hot reload will now attempt to " + (outcome.success ? "load the result." : "roll back to the last known-good binaries."));
    }

    bool reloadedSuccessfully = false;
    if (outcome.success) {
        ProjectAssemblyHotReloadDebugStatus::Instance().Set("ReloadingNewCode", projectName);
        ProjectAssemblyHost& host = core.GetProjectAssemblyHost();
        reloadedSuccessfully =
            host.LoadOneProjectAssemblyFromExactPath(outputDirectory / (projectName + "_Game.dll"), core, editorHost)
            && host.LoadOneProjectAssemblyFromExactPathIfExists(outputDirectory / (projectName + "_Editor.dll"), core, editorHost);
    }

    if (!reloadedSuccessfully) {
        GTE_LOG_WARNING("ProjectAssemblyHotReload",
            "Compile/reload failed for '" + projectName + "' - rolling back to the last known-good binaries.");
        ProjectAssemblyHotReloadDebugStatus::Instance().Set("RollingBack", projectName);
        if (!RestoreProjectAssemblyBinariesFromBackup(projectName, outputDirectory)) {
            // The backup copy-back itself failed (missing/unreadable backup,
            // or a sharing violation against a straggling build process -
            // see the in-flight-rejection comment above) - whatever is
            // currently sitting at outputDirectory is now of UNKNOWN
            // provenance (could be the failed compile's own bad output,
            // could be a partially-overwritten file). Loading it anyway and
            // reporting "RolledBack" would be actively dishonest (it may
            // well be running the BROKEN new code while claiming to have
            // rolled back) - abort here instead, matching the existing
            // "rollback itself failed" CriticalFailure shape immediately
            // below.
            GTE_LOG_ERROR("ProjectAssemblyHotReload",
                "CRITICAL: restoring the backup binaries failed for '" + projectName + "' - this project is now UNLOADED, and the binaries on disk are of unknown provenance. Manual intervention required.");
            ProjectAssemblyHotReloadDebugStatus::Instance().Finish("CriticalFailure",
                "restoring the backup binaries failed - project is now unloaded, binaries on disk are of unknown provenance, manual intervention required");
            return;
        }
        ProjectAssemblyHost& host = core.GetProjectAssemblyHost();
        const bool rolledBack =
            host.LoadOneProjectAssemblyFromExactPath(outputDirectory / (projectName + "_Game.dll"), core, editorHost)
            && host.LoadOneProjectAssemblyFromExactPathIfExists(outputDirectory / (projectName + "_Editor.dll"), core, editorHost);
        if (!rolledBack) {
            GTE_LOG_ERROR("ProjectAssemblyHotReload",
                "CRITICAL: rollback itself failed for '" + projectName + "' - this project is now UNLOADED. Manual intervention required.");
            ProjectAssemblyHotReloadDebugStatus::Instance().Finish("CriticalFailure",
                "rollback itself failed - project is now unloaded, manual intervention required");
            return;
        }
    }

    ProjectAssemblyHotReloadDebugStatus::Instance().Set("RestoringState", projectName);
    RestoreProjectAssemblyHotReloadState(core, renderer, snapshot, projectRootDirectory);

    GTE_LOG_INFO("ProjectAssemblyHotReload",
        "Hot reload cycle for '" + projectName + "' complete (" + (reloadedSuccessfully ? "new code" : "rolled back") + ") - engine unfrozen.");
    ProjectAssemblyHotReloadDebugStatus::Instance().Finish(reloadedSuccessfully ? "Success" : "RolledBack", "");
}

} // namespace gte
