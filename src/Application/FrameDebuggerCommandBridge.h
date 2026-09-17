#pragma once

// task_manager/frame-debugger-3 campaign, PHASE7
// (PHASE7_NETWORK_HTTP_AUTOMATION_AND_MAIN_VIEWPORT_PINNING.md) - the ONE
// reviewed, thread-safe bridge a Network route handler (background thread)
// is allowed to touch to make a FRAME-DEBUGGER-mutating (or -reading)
// request happen on the main thread. Structurally mirrors
// EditorUiCommandBridge.h field-for-field (mutex + condition_variable +
// SubmitAndWait()/TryPeekPendingCommandRequest()/FulfillCommand()) but is
// deliberately its OWN, separate, narrow bridge type - per AGENTS.md's own
// explicit rule: a genuinely new KIND of request gets its own bridge, never
// a new enum value bolted onto an existing bridge built for an unrelated
// purpose (see PHASE0_MASTER_STRATEGY.md's Locked Design Decision #9, and
// EditorUiCommandBridge.h's own header comment for the exact rule this
// follows - Frame Debugger control is unambiguously a NEW kind of request,
// completely unrelated to "activate this already-existing docked tab",
// EditorUiCommandBridge's only existing job).
//
// Deliberately free of ImGui/Editor #includes - this header only ever moves
// plain scalars (bool/int/float/string) between "the network thread wants
// the Frame Debugger to do X" and "the main thread did X (or couldn't), and
// here is its resulting state". The actual ImGui/Panel work happens inside
// IEditorLayer's own FrameDebuggerOpenWindow()/FrameDebuggerSetEnabled()/
// FrameDebuggerCaptureNow()/FrameDebuggerSelectEvent()/
// FrameDebuggerSetChannel()/
// FrameDebuggerSetLevels()/FrameDebuggerGetState() methods (EditorLayer.h) -
// Application::Run() is what bridges the two, exactly like
// EditorUiCommandBridge's own ActivateTab() plumbing already does for its
// one command kind.
//
// GET /frame_debugger/state ALSO goes through this SAME bridge (a GetState
// command kind) rather than a lighter-weight direct/lock-free read of
// Editor-owned mutable state from the network thread - see this campaign's
// own PHASE7_COMPLETION_REPORT.md "Deviations" section for why: unlike
// GET /list_tabs (a pure function of compile-time-fixed data, needing no
// bridge at all), the Frame Debugger's state (m_enabled/m_history/
// m_selectedEventIndex/m_channel/m_levelsBlack/m_levelsWhite - see
// Panels/FrameDebuggerPanel.h) is genuinely mutable, main-thread-owned data
// with no atomics of its own - reading it directly from the network thread
// would be a real (if narrow) data race, so this bridge's existing
// mutex-protected round-trip is reused for reads too, exactly the same
// "never touch engine state directly" discipline every other read endpoint
// in this codebase (GET /get_texture, GET /list_textures) already follows.
//
// Owned by Application (the composition root), constructed alongside
// m_captureBridge/m_commandBridge/m_uiCommandBridge, BEFORE NetworkServer
// (so its address can be handed into NetworkServer's constructor) - see
// Application.h.

#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>

namespace gte {

// One kind per distinct Frame Debugger action - mirrors
// EditorUiCommandKind's own "an enum, not a single hardcoded request shape"
// precedent (EditorUiCommandBridge.h), since this bridge already starts
// with more than one meaningful kind.
enum class FrameDebuggerCommandKind {
    OpenWindow,
    SetEnabled,
    CaptureNow,
    SelectEvent,
    SetChannel,
    SetLevels,
    GetState,
};

struct FrameDebuggerSetEnabledCommand {
    bool enabled = false;
};

struct FrameDebuggerSelectEventCommand {
    int index = -1;
};

struct FrameDebuggerSetChannelCommand {
    // "all"|"r"|"g"|"b"|"a" - already validated by NetworkRoutes.cpp's own
    // ParseFrameDebuggerSetChannelQuery() before this ever gets submitted.
    std::string channel;
};

struct FrameDebuggerSetLevelsCommand {
    float black = 0.0f;
    float white = 1.0f;
};

// One pending Frame Debugger command, tagged by `kind` - only the ONE field
// matching `kind` is meaningful (same tagged-struct convention as
// EditorUiCommandRequest/EngineCommandRequest, never std::variant).
struct FrameDebuggerCommandRequest {
    FrameDebuggerCommandKind kind = FrameDebuggerCommandKind::GetState;
    FrameDebuggerSetEnabledCommand setEnabled;
    FrameDebuggerSelectEventCommand selectEvent;
    FrameDebuggerSetChannelCommand setChannel;
    FrameDebuggerSetLevelsCommand setLevels;
};

// The Frame Debugger's own read-only state snapshot - same SHAPE as
// IEditorLayer::FrameDebuggerStateSnapshotView (EditorLayer.h), but a
// completely independent, Application-owned type: this header must never
// depend on anything under src/Editor/ (mirrors EditorUiCommandBridge.h's
// own "ActivateTabOutcome is its own tiny type, never EditorLayer.h's
// TabActivationResult reused directly" precedent). Application.cpp is the
// ONE place that copies one into the other, one field at a time, exactly
// like it already does for ActivateTabOutcome/TabActivationResult.
struct FrameDebuggerStateOutcome {
    bool enabled = false;
    bool windowOpen = false;
    // task_manager/frame-debugger-7 campaign, PHASE1 - REPLACES
    // `historyCount`/`historyCursor` (the old 8-slot ring buffer's own
    // fields) - there is only ever ONE captured frame now.
    bool hasCapturedFrame = false;
    int totalEventCount = 0;
    int selectedEventIndex = -1;
    std::string channel = "all";
    float levelsBlack = 0.0f;
    float levelsWhite = 1.0f;
};

// Outcome of one FrameDebuggerCommandRequest. `success` is meaningful for
// every kind (false for CaptureNow when the Frame Debugger isn't currently
// enabled - mirroring the "Capture" button's own disabled-while-not-
// enabled guard - or false for SetChannel when given an unrecognized
// string, defense-in-depth only since NetworkRoutes.cpp's own query
// parsing already rejects an invalid channel with its own 400 before ever
// reaching this bridge; every other kind always succeeds). `state` is
// ALWAYS populated with the Frame Debugger's resulting state after the
// command ran (not just for GetState) - a deliberate, small ergonomic
// choice (see this campaign's own PHASE7_COMPLETION_REPORT.md "Deviations"
// section) so an HTTP caller/AI verifier can see the resulting state
// immediately after e.g. GET /frame_debugger/enable, without a SEPARATE
// GET /frame_debugger/state round-trip after every single step.
struct FrameDebuggerCommandResult {
    FrameDebuggerCommandKind kind = FrameDebuggerCommandKind::GetState;
    bool success = true;
    FrameDebuggerStateOutcome state;
};

class FrameDebuggerCommandBridge {
public:
    FrameDebuggerCommandBridge() = default;
    ~FrameDebuggerCommandBridge() = default;

    FrameDebuggerCommandBridge(const FrameDebuggerCommandBridge&) = delete;
    FrameDebuggerCommandBridge& operator=(const FrameDebuggerCommandBridge&) = delete;

    // --- Called from the NETWORK thread (a route handler) only ----------

    struct SubmitResult {
        std::optional<FrameDebuggerCommandResult> result;
        bool alreadyPending = false;
        bool timedOut = false;
    };
    SubmitResult SubmitAndWait(FrameDebuggerCommandRequest request, int timeoutMilliseconds = 3000);

    // --- Called from the MAIN thread (Application::Run()) only ----------

    bool IsCommandPending() const;

    // Same "collapse check-then-use into one locked operation" fix
    // EngineCommandBridge::TryPeekPendingCommandRequest()/
    // EditorUiCommandBridge::TryPeekPendingCommandRequest() already document
    // in full - copy that exact reasoning/behavior here.
    std::optional<FrameDebuggerCommandRequest> TryPeekPendingCommandRequest() const;

    void FulfillCommand(FrameDebuggerCommandResult result);

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_conditionVariable;
    bool m_requested = false;
    bool m_fulfilled = false;
    FrameDebuggerCommandRequest m_request;
    FrameDebuggerCommandResult m_result;
};

} // namespace gte
