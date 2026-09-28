#pragma once

// src/Application/ProjectLifecycleLoadCommandBridge.h
//
// editor-core-separation-17 campaign (On-Engine Project Workflow plan,
// BIG-STEP 3), PHASE2. A new, dedicated cross-thread bridge - structurally
// IDENTICAL to AssetImportCommandBridge.h's own proven shape (single
// global slot, mutex + condition_variable, SubmitAndWait()/
// IsCommandPending()/TryPeekPendingCommandRequest()/FulfillCommand()) -
// mirrors that header's own explicit, written justification for why a
// genuinely new capability gets its own bridge type rather than a new
// EngineCommandKind bolted onto an existing one.
//
// USED ONLY by the network-thread-facing
// IProjectLifecycleCapability::OpenProjectAssembly() call path
// (Core/EditorCapabilities.h) - the ImGui-facing
// OpenProjectAssemblyOnMainThread() path NEVER submits into this bridge;
// it is already running on the same thread this bridge's own drain point
// (EditorHost::Run()) runs on, and calling SubmitAndWait() from there would
// deadlock the whole Editor (see PHASE0_MASTER_STRATEGY.md, Section 2.2,
// for the full reasoning). Owned by EditorHost (a plain member, mirrors
// m_hotReloadCommandBridge/m_assetImportCommandBridge's own placement),
// wired into EditorProjectLifecycleCapability via a setter (PHASE3),
// mirroring EditorHotReloadDebugCapability::SetHotReloadCommandBridge()'s
// own precedent exactly.

#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>

namespace gte {

// Plain request payload - only ever needs the project's own name; the
// main-thread drain point resolves the real .dll paths itself
// (mirrors ProjectAssemblyHotReloadCommandBridge's own "only carries a
// project name, the main thread resolves paths" convention).
struct LoadProjectAssemblyCommandRequest {
    std::string projectName;
};

// Outcome of one load attempt.
struct LoadProjectAssemblyCommandResult {
    // True only if LoadOneProjectAssemblyFromExactPath() (the _Game.dll)
    // AND LoadOneProjectAssemblyFromExactPathIfExists() (the optional
    // _Editor.dll) both returned true - mirrors
    // ProjectAssemblyHotReload.cpp's own existing `&&`-chained success
    // check (lines ~176-177/204-205) exactly.
    bool loadSucceeded = false;
};

class ProjectLifecycleLoadCommandBridge {
public:
    ProjectLifecycleLoadCommandBridge() = default;
    ~ProjectLifecycleLoadCommandBridge() = default;

    ProjectLifecycleLoadCommandBridge(const ProjectLifecycleLoadCommandBridge&) = delete;
    ProjectLifecycleLoadCommandBridge& operator=(const ProjectLifecycleLoadCommandBridge&) = delete;

    // --- Called from the NETWORK thread (a route handler) only ----------

    struct SubmitResult {
        std::optional<LoadProjectAssemblyCommandResult> result;
        bool alreadyPending = false;
        bool timedOut = false;
    };
    // 5000ms default - a real Project Assembly load is a single
    // LoadLibraryW() + one GTE_RegisterProject call, never a multi-minute
    // operation (unlike the hot-reload bridge's own deliberately long
    // 600000ms default) - see this campaign's own PHASE0 file, Step 2.
    SubmitResult SubmitAndWait(LoadProjectAssemblyCommandRequest request, int timeoutMilliseconds = 5000);

    // --- Called from the MAIN thread (EditorHost::Run()) only -----------

    bool IsCommandPending() const;
    std::optional<LoadProjectAssemblyCommandRequest> TryPeekPendingCommandRequest() const;
    void FulfillCommand(LoadProjectAssemblyCommandResult result);

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_conditionVariable;
    bool m_requested = false;
    bool m_fulfilled = false;
    LoadProjectAssemblyCommandRequest m_request;
    LoadProjectAssemblyCommandResult m_result;
};

} // namespace gte
