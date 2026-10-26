#include "ProjectAssemblyHotReload.h"

#include "../Core.h"
#include "../Logging.h"
#include "ProjectAssemblyBuildRunner.h"
#include "ProjectAssemblyHotReloadDebugStatus.h"
#include "../../Renderer/Renderer.h"
#include "../../Scene/SceneBuilder.h"
// NOTE: Core-tier file: must not include Editor/ProjectRootPath.h or call ExecutableDirectory();
// the caller passes the resolved output/build directories.

#include <windows.h>

namespace gte {

// Refreshes the asset database once, then snapshots the live world into a SceneDocument.
HotReloadStateSnapshot CaptureProjectAssemblyHotReloadState(Core& core, const std::filesystem::path& projectRootDirectory)
{
    // Refreshed once per cycle; tolerates a missing projectRootDirectory.
    core.GetAssetDatabase().RefreshFromDirectory(projectRootDirectory);

    HotReloadStateSnapshot snapshot;
    snapshot.document = BuildSceneDocumentFromRegistry(core.GetGame().GetRegistry(), core.GetAssetDatabase());

    GTE_LOG_INFO("ProjectAssemblyHotReload",
        "CaptureProjectAssemblyHotReloadState: captured " + std::to_string(snapshot.document.entities.size()) + " entities.");
    return snapshot;
}

// Rebuilds the live world from the captured snapshot (same path as the Editor's scene load).
void RestoreProjectAssemblyHotReloadState(Core& core, Renderer& renderer, const HotReloadStateSnapshot& snapshot,
    const std::filesystem::path& /*projectRootDirectory*/)
{
    // Reuses the asset database refreshed during capture; the engine was frozen, so nothing new appeared on disk.
    ClearEntireScene(core.GetGame().GetRegistry());
    ReconstructSceneFromDocument(
        core.GetGame(), renderer, snapshot.document, core.GetAssetDatabase(), core.GetSceneServicesDescriptorSet().LayoutHandle());

    GTE_LOG_INFO("ProjectAssemblyHotReload",
        "RestoreProjectAssemblyHotReloadState: restored " + std::to_string(snapshot.document.entities.size()) + " entities.");
}

namespace {

// Keeps Windows from marking the window "Not Responding" during the blocking compile. Does not poll SDL events.
// A null hWnd services every window this thread owns (torn-off ImGui viewport windows too);
// WM_CLOSE is dispatched, not dropped, so SDL queues the close request until the freeze ends.
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
    // Locks GetHotReloadEngineStateMutex() internally; do not wrap in an outer lock (non-recursive).
    if (!core.GetProjectAssemblyHost().UnloadProjectAssembly(projectName, core, renderer)) {
        GTE_LOG_ERROR("ProjectAssemblyHotReload", "Aborting - the GPU did not go idle, '" + projectName + "' stays loaded.");
        ProjectAssemblyHotReloadDebugStatus::Instance().Finish("CriticalFailure", "GPU did not go idle - nothing changed");
        return;
    }

    ProjectAssemblyHotReloadDebugStatus::Instance().Set("Compiling", projectName);
    BuildOutcome outcome;
    const bool buildRan = TryRunProjectAssemblyBuildSynchronously(
        projectName, buildDirectory.string(), outcome, &PumpWindowsMessagesDuringHotReloadFreeze);
    if (!buildRan) {
        // Rejected by the in-flight build guard, but the project is already unloaded: treat as a compile
        // failure and roll back. A straggling build holding the .dll can fail the copy (reported as CriticalFailure).
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
            // Backup copy-back failed: the binaries on disk are of unknown provenance, so abort
            // instead of loading them and reporting "RolledBack".
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
