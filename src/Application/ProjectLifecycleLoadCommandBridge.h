#pragma once

// Cross-thread bridge for a network-driven Project Assembly load request.
// Used only by IProjectLifecycleCapability::OpenProjectAssembly() - the
// ImGui-facing OpenProjectAssemblyOnMainThread() path never submits into
// this bridge, since it already runs on the same thread this bridge's own
// drain point (EditorHost::Run()) runs on. See SyncCommandBridge.h for the
// handoff mechanism.

#include "SyncCommandBridge.h"

#include <string>

namespace gte {

// Only the project's own name is carried - the main-thread drain point
// resolves the real .dll paths itself.
struct LoadProjectAssemblyCommandRequest {
    std::string projectName;
};

// Outcome of one load attempt.
struct LoadProjectAssemblyCommandResult {
    // True only if both the required _Game.dll load and the optional
    // _Editor.dll load succeeded.
    bool loadSucceeded = false;
};

// A Project Assembly load is a single LoadLibraryW() + one
// GTE_RegisterProject call - longer than the shared template's 3000ms
// default, nowhere near a multi-minute operation. Thin forwarding override
// restores the correct default.
class ProjectLifecycleLoadCommandBridge
    : public SyncCommandBridge<LoadProjectAssemblyCommandRequest, LoadProjectAssemblyCommandResult> {
public:
    SubmitResult SubmitAndWait(LoadProjectAssemblyCommandRequest request, int timeoutMilliseconds = 5000)
    {
        return SyncCommandBridge::SubmitAndWait(std::move(request), timeoutMilliseconds);
    }
};

} // namespace gte
