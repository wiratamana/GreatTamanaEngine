#pragma once

// The ONE reviewed, thread-safe bridge a Network route handler (background
// thread) is allowed to touch to make an EDITOR-UI-mutating request happen
// on the main thread - network-impl-7 campaign. Structurally mirrors
// EngineCommandBridge.h (ECS-mutating requests) and FrameCaptureBridge.h
// (read-only pixel requests) - see either file's own header comment for the
// shared cross-thread design rationale - but is deliberately its OWN,
// separate, narrow bridge type, per AGENTS.md's own explicit rule: a
// genuinely new KIND of request gets its own bridge, never a new enum value
// bolted onto an existing bridge built for an unrelated purpose (see
// AGENTS.md, "Networking", FrameCaptureBridge bullet).
//
// Deliberately free of ImGui/Editor #includes - this header only ever moves
// a plain tab-name string and a plain bool/string outcome between "the
// network thread wants this tab focused" and "the main thread focused it (or
// couldn't)". The actual ImGui work happens inside IEditorLayer::ActivateTab()
// (Phase 2) - Application::Run() (Phase 3) is what bridges the two.
//
// Owned by Application (the composition root), constructed alongside
// m_captureBridge/m_commandBridge, BEFORE NetworkServer (so its address can
// be handed into NetworkServer's constructor) - see Application.h (Phase 3).

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace gte {

// Only one kind exists today, but this follows EngineCommandKind's own
// precedent of being an enum (not a single hardcoded request shape) so a
// FUTURE Editor-UI command (e.g. "close tab X", "set panel Y visible") can
// be added to this SAME bridge later without inventing a third one - see
// EngineCommandKind's own history (started with 2 values in network-impl-3,
// grew to 4 in network-impl-5, reusing the SAME bridge both times).
enum class EditorUiCommandKind {
    ActivateTab,
    // GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
    // PHASE6 (task_manager/render-pass-5/PHASE6_EDITOR_TOOLING_AND_LIVE_VALIDATION.md)
    // - spawns a real, repeatable GPU-driven-eligible validation batch (see
    // src/Editor/GpuDrivenBatchTestSpawner.h) - reuses this SAME
    // single-global-slot bridge (mirrors EngineCommandKind's own growth
    // history above), since it is likewise an editor-only, ECS/GPU-mutating
    // request.
    SpawnGpuDrivenTestBatch,
};

// Plain request payload for one ActivateTab command.
struct ActivateTabCommand {
    std::string tabName;
};

// GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
// PHASE6 - plain request payload for one SpawnGpuDrivenTestBatch command.
struct SpawnGpuDrivenTestBatchCommand {
    // Must be >= 1 for a real batch to actually be spawned - see
    // GpuDrivenBatchTestSpawner::Spawn()'s own doc comment. Defaults to 6,
    // matching PHASE5's own already-verified manual harness (see
    // PHASE5_COMPLETION_REPORT.md's "Live verification" section) - the
    // project-owner-confirmed default (via ask_questions) for this new,
    // caller-configurable spawn helper.
    std::uint32_t instanceCount = 6;
};

// One pending Editor UI command, tagged by `kind` - only `activateTab` is
// meaningful today (EXACTLY one field meaningful, selected by `kind` - same
// tagged-struct convention as EngineCommandRequest, not std::variant).
struct EditorUiCommandRequest {
    EditorUiCommandKind kind = EditorUiCommandKind::ActivateTab;
    ActivateTabCommand activateTab;
    SpawnGpuDrivenTestBatchCommand spawnGpuDrivenTestBatch;
};

// Outcome of one ActivateTab command. `success` is the single field a
// caller needs for the happy path; `tabExists` distinguishes WHY a failure
// happened (see NetworkRoutes.cpp's own 404-vs-409 status mapping, Phase 4):
// - success == true                         -> tab found and focused (200)
// - success == false && tabExists == false   -> no live ImGui window with
//                                                that exact name existed
//                                                this frame (409 - the name
//                                                itself is fine, nothing to
//                                                focus RIGHT NOW)
// A name that isn't even in Core/EditorPanelCatalog.h's known list is rejected
// BEFORE ever reaching this bridge at all (see NetworkRoutes.cpp's own
// pre-validation, Phase 4) - this outcome type has no field for that case
// because it is structurally unreachable here.
struct ActivateTabOutcome {
    bool success = false;
    bool tabExists = false;
};

// GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
// PHASE6 - outcome of one SpawnGpuDrivenTestBatch command. `success ==
// false` covers BOTH "instanceCount was 0" and "the Editor module is not
// available in this build" (NullEditorLayer) - `errorMessage` always
// explains which. `editorAvailable` (editor-core-separation-1 campaign,
// PHASE8) is a real, dedicated bool field - mirroring
// SaveSceneOutcome/ImportExternalFileOutcome's own precedent - so callers
// never need to sniff `errorMessage`'s own TEXT for the literal substring
// "GTE_ENABLE_EDITOR" to distinguish "structurally unavailable" from "a
// caller mistake" (NetworkServer.cpp used to do exactly that before PHASE8).
struct SpawnGpuDrivenTestBatchOutcome {
    bool success = false;
    bool editorAvailable = true;
    std::string errorMessage;
    std::uint32_t instanceCount = 0;
};

// The completed result of one EditorUiCommandRequest - `kind` mirrors the
// request's own `kind`.
struct EditorUiCommandResult {
    EditorUiCommandKind kind = EditorUiCommandKind::ActivateTab;
    ActivateTabOutcome activateTab;
    SpawnGpuDrivenTestBatchOutcome spawnGpuDrivenTestBatch;
};

class EditorUiCommandBridge {
public:
    EditorUiCommandBridge() = default;
    ~EditorUiCommandBridge() = default;

    EditorUiCommandBridge(const EditorUiCommandBridge&) = delete;
    EditorUiCommandBridge& operator=(const EditorUiCommandBridge&) = delete;

    // --- Called from the NETWORK thread (a route handler) only ----------

    struct SubmitResult {
        std::optional<EditorUiCommandResult> result;
        bool alreadyPending = false;
        bool timedOut = false;
    };
    SubmitResult SubmitAndWait(EditorUiCommandRequest request, int timeoutMilliseconds = 3000);

    // --- Called from the MAIN thread (Application::Run()) only ----------

    bool IsCommandPending() const;

    // Same "collapse check-then-use into one locked operation" fix
    // EngineCommandBridge::TryPeekPendingCommandRequest() already documents
    // in full - copy that exact reasoning/behavior here, do not regress to
    // a separate IsCommandPending() + PeekPendingCommandRequest() pair.
    std::optional<EditorUiCommandRequest> TryPeekPendingCommandRequest() const;

    void FulfillCommand(EditorUiCommandResult result);

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_conditionVariable;
    bool m_requested = false;
    bool m_fulfilled = false;
    EditorUiCommandRequest m_request;
    EditorUiCommandResult m_result;
};

} // namespace gte
