#pragma once

// editor-core-separation-8 campaign, PHASE5 - the ONE reviewed, thread-safe
// bridge a Network route handler (background thread) is allowed to touch to
// make a render-graph-CONTROL-mutating (or -reading) request happen on the
// main thread. Structurally mirrors FrameDebuggerCommandBridge.h field-for-
// field (mutex + condition_variable + SubmitAndWait()/
// TryPeekPendingCommandRequest()/FulfillCommand()) but is deliberately its
// OWN, separate bridge type - per this codebase's own explicit rule: a
// genuinely new KIND of request gets its own bridge, never a new enum value
// bolted onto an existing bridge built for an unrelated purpose (see
// PHASE0_MASTER_STRATEGY.md's Locked Architecture Decision #16). "Control
// this session's render-graph runtime composition" (built-in pass on/off,
// plugin render feature on/off + priority, Blur/GBuffer debug-pass on/off)
// is unambiguously ONE new, cohesive kind of request, unrelated to Frame
// Debugger control or tab activation.
//
// Deliberately free of ImGui/Core/RenderGraph #includes - this header only
// ever moves plain scalars (bool/int/string) between "the network thread
// wants render-graph control state X changed (or read)" and "the main
// thread did X (or couldn't), and here is the resulting state". The actual
// mutation happens inside EditorHost.cpp's own pump block, calling directly
// into Core's new PHASE1/PHASE2 methods (built-in pass/plugin feature) or
// IEditorLayer's new PHASE3 methods (Blur/GBuffer) - see
// PHASE0_MASTER_STRATEGY.md's Step 2.6 for exactly why these two categories
// are routed differently even though they share this one bridge.

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace gte {

enum class RenderGraphControlCommandKind {
    SetBuiltInPassEnabled,
    ListPassStates,
    SetFeatureEnabled,
    SetFeaturePriority,
    SetBlurEnabled,
    SetGBufferEnabled,
    // task_manager/better-render-pass-7 campaign (better-render-pass-3
    // campaign, BLOCK5 - Array/Cubemap Texture Resources), PHASE5 - mirrors
    // SetGBufferEnabled's own exact shape, for
    // IEditorLayer::SetShowTextureArrayValidationOutput() (see
    // src/Editor/TextureArrayValidation.h).
    SetTextureArrayValidationEnabled,
};

struct RenderGraphControlSetPassEnabledCommand {
    std::string name;
    bool enabled = false;
};

struct RenderGraphControlSetFeatureEnabledCommand {
    std::string name;
    bool enabled = false;
};

struct RenderGraphControlSetFeaturePriorityCommand {
    std::string name;
    std::int32_t priority = 0;
};

struct RenderGraphControlSetBoolCommand {
    bool enabled = false; // Used by SetBlurEnabled/SetGBufferEnabled/SetTextureArrayValidationEnabled.
};

// One pending render-graph-control command, tagged by `kind` - only the ONE
// field matching `kind` is meaningful (same tagged-struct convention as
// FrameDebuggerCommandRequest/EditorUiCommandRequest, never std::variant).
struct RenderGraphControlCommandRequest {
    RenderGraphControlCommandKind kind = RenderGraphControlCommandKind::ListPassStates;
    RenderGraphControlSetPassEnabledCommand setPassEnabled;
    RenderGraphControlSetFeatureEnabledCommand setFeatureEnabled;
    RenderGraphControlSetFeaturePriorityCommand setFeaturePriority;
    RenderGraphControlSetBoolCommand setBlurEnabled;
    RenderGraphControlSetBoolCommand setGBufferEnabled;
    RenderGraphControlSetBoolCommand setTextureArrayValidationEnabled;
};

// One reported pass toggle state - a completely independent, Application-
// owned type, mirroring FrameDebuggerStateOutcome's own "never reuse a
// gte_core/Editor-tier struct directly across this boundary" precedent
// (this header must never depend on rg::RenderPassToggleState directly).
// EditorHost.cpp is the ONE place that copies one into the other, one field
// at a time.
struct RenderGraphControlPassStateOutcome {
    std::string name;
    bool enabled = false;
    bool everDeclaredThisSession = false;
};

// Outcome of one RenderGraphControlCommandRequest. `success` is false for:
// SetBuiltInPassEnabled on a deny-listed name (e.g. "Present"),
// SetFeatureEnabled/SetFeaturePriority on an unrecognized plugin feature
// name - true for every other kind, including ListPassStates (which has no
// failure mode of its own). `errorMessage` is populated only when `success`
// is false. `passStates` is populated ONLY for ListPassStates - empty
// otherwise (never populated speculatively for a mutation kind - a caller
// wanting fresh state after a mutation makes a SEPARATE GET
// /render_graph/passes call, exactly mirroring GET /render_graph's own
// "separate read-only endpoint" convention rather than FrameDebugger's
// "state always echoed back" one - the two subsystems are free to differ
// here since this campaign's mutation calls are simple, single-field
// bools/ints with no rich "resulting state" worth echoing beyond a plain
// success/failure).
struct RenderGraphControlCommandResult {
    RenderGraphControlCommandKind kind = RenderGraphControlCommandKind::ListPassStates;
    bool success = true;
    std::string errorMessage;
    std::vector<RenderGraphControlPassStateOutcome> passStates;
};

class RenderGraphControlCommandBridge {
public:
    RenderGraphControlCommandBridge() = default;
    ~RenderGraphControlCommandBridge() = default;

    RenderGraphControlCommandBridge(const RenderGraphControlCommandBridge&) = delete;
    RenderGraphControlCommandBridge& operator=(const RenderGraphControlCommandBridge&) = delete;

    // --- Called from the NETWORK thread (a route handler) only ----------

    struct SubmitResult {
        std::optional<RenderGraphControlCommandResult> result;
        bool alreadyPending = false;
        bool timedOut = false;
    };
    SubmitResult SubmitAndWait(RenderGraphControlCommandRequest request, int timeoutMilliseconds = 3000);

    // --- Called from the MAIN thread (EditorHost::Run()) only ----------

    bool IsCommandPending() const;
    std::optional<RenderGraphControlCommandRequest> TryPeekPendingCommandRequest() const;
    void FulfillCommand(RenderGraphControlCommandResult result);

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_conditionVariable;
    bool m_requested = false;
    bool m_fulfilled = false;
    RenderGraphControlCommandRequest m_request;
    RenderGraphControlCommandResult m_result;
};

} // namespace gte
