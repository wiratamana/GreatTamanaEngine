# PHASE5 — Cross-Thread Bridge + HTTP Endpoints

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it FIRST — especially the
"final, locked HTTP endpoint contract" table, Locked Product Decisions
#3/#4/#9, and Locked Architecture Decision #16). Read
`PHASE1_COMPLETION_REPORT.md` through `PHASE4_COMPLETION_REPORT.md` first —
this phase is the FIRST one to actually call `RenderPassToggleRegistry::SetEnabled()`/
`RenderFeatureCompositor::SetFeatureEnabled()`/`SetFeaturePriority()`/
`IEditorLayer::SetShowBlurredSceneOutput()`/`SetShowGBufferValidationOutput()`
from OUTSIDE a test — get every one of those 5 call sites exactly right. Use
`ask_questions` for any genuine ambiguity, especially around exact
status-code mapping if a real case arises that this document does not
already resolve.

## Step 1: The Goal

Ship a new `RenderGraphControlCommandBridge` (mirroring
`FrameDebuggerCommandBridge` field-for-field), pump it once per frame inside
`EditorHost.cpp`'s real `Run()` loop, and register the 6 HTTP routes from
`PHASE0_MASTER_STRATEGY.md`'s own locked contract table. By the end of this
phase, `gte_send_request` against a live, running `GreatTamanaEditor.exe`
can: disable/enable any built-in pass by name, list every known built-in
pass's toggle state, enable/disable and re-prioritize any loaded plugin
render feature by name, and flip both debug-pass checkboxes — all over
plain `GET .../*?param=value` requests, all reflected immediately in the
NEXT `GET /render_graph`/`GET /render_graph/passes` response and in the
"Render Graph" panel's own live display.

## Step 2: The Situation

Read these exact files IN FULL before writing any code:

- `src/Application/FrameDebuggerCommandBridge.h` AND `.cpp` — THE template
  this phase's new bridge mirrors byte-for-byte in shape (mutex +
  `std::condition_variable` + one pending-request slot;
  `SubmitAndWait(request, timeoutMilliseconds = 3000)` returning a
  `SubmitResult{ std::optional<Result> result; bool alreadyPending; bool
  timedOut; }`; `IsCommandPending()`/`TryPeekPendingCommandRequest()`/
  `FulfillCommand()` for the main-thread side). Copy the `.cpp`'s actual
  locking/condvar logic near-verbatim, substituting only the request/result
  payload types.
- `src/Network/NetworkServer.cpp` — read the FULL block from
  `RespondWithFrameDebuggerCommandResult()` through every
  `server.Get("/frame_debugger/...")` registration (roughly lines 231-549 as
  of this campaign's own planning session — CONFIRM the real current line
  numbers, they drift). This is the EXACT template for:
  - `RegisterRoutes(...)`'s own parameter list (this phase adds ONE new
    `RenderGraphControlCommandBridge* renderGraphControlCommandBridge`
    parameter, mirroring how `frameDebuggerCommandBridge` itself is already
    one of several bridge pointers threaded through this exact function).
  - A shared `RespondWithRenderGraphControlCommandResult(...)` helper
    (mirroring `RespondWithFrameDebuggerCommandResult()`'s own exact
    alreadyPending/timedOut/success->503/504/200-or-409 mapping).
  - Each individual route's own parse-query -> bridge-null-check ->
    build-request -> `SubmitAndWait()` -> respond shape.
- `src/Network/NetworkRoutes.h`/`.cpp` — find the existing
  `ParsedFrameDebuggerEnableQuery`/`ParseFrameDebuggerEnableQuery()` and
  `ParsedFrameDebuggerSelectEventQuery`/`ParseFrameDebuggerSelectEventQuery()`
  pairs (a bool-valued query param, and an int-valued query param,
  respectively) — these are the EXACT 2 shapes every one of this phase's own
  6 new endpoints needs (a `name` string + `enabled` bool; a `name` string +
  `priority` int; a bare `enabled` bool). Also find
  `BuildFrameDebuggerCommandResponseJson()`/`BuildFrameDebuggerStateResponseJson()`
  for the exact JSON-building style to mirror.
- `src/Editor/EditorHost.cpp` — the FULL bridge-pump block (the
  `EditorUiCommandBridge`/`FrameDebuggerCommandBridge`/
  `AssetImportCommandBridge` pumps, one `if (const std::optional<...> request
  = m_xxxBridge.TryPeekPendingCommandRequest())` block each, all sitting
  immediately after `m_editorLayer->NewFrame();` and before
  `m_renderer.BeginFrame();`). This phase's new pump block goes in the SAME
  group, in the SAME style, reading `m_core.GetRenderPassToggleRegistryMutable()`/
  `m_core.GetRenderFeatureCompositor()`/`m_editorLayer->SetShowBlurredSceneOutput()`/
  `SetShowGBufferValidationOutput()` directly (see
  `PHASE0_MASTER_STRATEGY.md`'s Step 2.6 for exactly which of these 2
  categories each command kind belongs to — built-in pass/plugin-feature
  mutations go straight to `m_core`, Blur/GBuffer mutations go through
  `m_editorLayer`).
- `src/Network/NetworkServer.h` — confirm its constructor's EXACT current
  parameter list (6 bridge/capability pointers today) and how
  `FrameDebuggerCommandBridge*` is forward-declared there — this phase adds
  a 7th parameter, `RenderGraphControlCommandBridge* renderGraphControlCommandBridge = nullptr`,
  forward-declared the exact same way.
- Wherever `NetworkServer` itself is CONSTRUCTED (search for
  `NetworkServer(` outside `NetworkServer.h/.cpp` — likely
  `EditorHost.cpp`/`Application.cpp`) — this phase's new bridge instance
  needs to be OWNED alongside the other bridges (same composition root) and
  its address passed into the new constructor parameter.

## Step 3: The Plan

### Step 3.1 — New file: `src/Application/RenderGraphControlCommandBridge.h`

```cpp
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
    bool enabled = false; // Used by both SetBlurEnabled and SetGBufferEnabled.
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
```

### Step 3.2 — New file: `src/Application/RenderGraphControlCommandBridge.cpp`

Copy `FrameDebuggerCommandBridge.cpp`'s ENTIRE `SubmitAndWait()`/
`IsCommandPending()`/`TryPeekPendingCommandRequest()`/`FulfillCommand()`
implementations verbatim, substituting only the type names
(`FrameDebuggerCommandRequest`/`Result` -> `RenderGraphControlCommandRequest`/
`Result`). Do not "improve" or restructure the locking logic — this exact
shape is already reviewed/battle-tested; any deviation here is unjustified
risk for zero benefit.

### Step 3.3 — `EditorHost.cpp` wiring

1. Own a new member, `RenderGraphControlCommandBridge m_renderGraphControlCommandBridge;`,
   declared alongside `m_frameDebuggerCommandBridge`/`m_assetImportCommandBridge`.
2. New pump block, placed in the SAME group as the existing
   `EditorUiCommandBridge`/`FrameDebuggerCommandBridge`/`AssetImportCommandBridge`
   pumps (immediately after `m_editorLayer->NewFrame();`, before
   `m_renderer.BeginFrame();`):

```cpp
if (const std::optional<RenderGraphControlCommandRequest> rgcRequest =
        m_renderGraphControlCommandBridge.TryPeekPendingCommandRequest()) {
    GTE_PROFILE_SCOPE("EditorHost::ExecuteRenderGraphControlCommand");
    RenderGraphControlCommandResult rgcResult;
    rgcResult.kind = rgcRequest->kind;
    switch (rgcRequest->kind) {
    case RenderGraphControlCommandKind::SetBuiltInPassEnabled: {
        const bool applied = m_core.GetRenderPassToggleRegistryMutable().SetEnabled(
            rgcRequest->setPassEnabled.name, rgcRequest->setPassEnabled.enabled);
        rgcResult.success = applied;
        if (!applied) {
            rgcResult.errorMessage = "\"" + rgcRequest->setPassEnabled.name + "\" cannot be disabled (deny-listed).";
        }
        break;
    }
    case RenderGraphControlCommandKind::ListPassStates: {
        for (const rg::RenderPassToggleState& state : m_core.GetRenderPassToggleRegistryMutable().ListAll()) {
            RenderGraphControlPassStateOutcome outcome;
            outcome.name = state.name;
            outcome.enabled = state.enabled;
            outcome.everDeclaredThisSession = state.everDeclaredThisSession;
            rgcResult.passStates.push_back(std::move(outcome));
        }
        rgcResult.success = true;
        break;
    }
    case RenderGraphControlCommandKind::SetFeatureEnabled: {
        RenderFeatureCompositor* compositor = m_core.GetRenderFeatureCompositor();
        if (compositor == nullptr) {
            rgcResult.success = false;
            rgcResult.errorMessage = "no render feature compositor available this session";
        } else {
            rgcResult.success =
                compositor->SetFeatureEnabled(rgcRequest->setFeatureEnabled.name, rgcRequest->setFeatureEnabled.enabled);
            if (!rgcResult.success) {
                rgcResult.errorMessage = "\"" + rgcRequest->setFeatureEnabled.name + "\" matches no loaded plugin render feature.";
            }
        }
        break;
    }
    case RenderGraphControlCommandKind::SetFeaturePriority: {
        RenderFeatureCompositor* compositor = m_core.GetRenderFeatureCompositor();
        if (compositor == nullptr) {
            rgcResult.success = false;
            rgcResult.errorMessage = "no render feature compositor available this session";
        } else {
            rgcResult.success = compositor->SetFeaturePriority(
                rgcRequest->setFeaturePriority.name, rgcRequest->setFeaturePriority.priority);
            if (!rgcResult.success) {
                rgcResult.errorMessage = "\"" + rgcRequest->setFeaturePriority.name + "\" matches no loaded plugin render feature.";
            }
        }
        break;
    }
    case RenderGraphControlCommandKind::SetBlurEnabled:
        m_editorLayer->SetShowBlurredSceneOutput(rgcRequest->setBlurEnabled.enabled);
        rgcResult.success = true;
        break;
    case RenderGraphControlCommandKind::SetGBufferEnabled:
        m_editorLayer->SetShowGBufferValidationOutput(rgcRequest->setGBufferEnabled.enabled);
        rgcResult.success = true;
        break;
    }
    m_renderGraphControlCommandBridge.FulfillCommand(rgcResult);
}
```

(Confirm the EXACT includes this needs: `RenderFeatureCompositor` — the
real header, since this `.cpp` now calls real methods on it; check whether
`EditorHost.cpp` already transitively includes it via `Core.h` before adding
a duplicate `#include`.)

3. Pass `&m_renderGraphControlCommandBridge` into `NetworkServer`'s
   constructor call, alongside the other 6 existing bridge pointers.

### Step 3.4 — `NetworkServer.h` changes

Add a 7th constructor parameter,
`RenderGraphControlCommandBridge* renderGraphControlCommandBridge = nullptr`,
and its matching forward declaration
(`class RenderGraphControlCommandBridge;`), mirroring
`FrameDebuggerCommandBridge*`'s own exact existing shape.

### Step 3.5 — `NetworkRoutes.h`/`.cpp` — parse + build functions

New parse functions (mirror `ParseFrameDebuggerEnableQuery()`/
`ParseFrameDebuggerSelectEventQuery()`'s exact validation/error-message
style):

```cpp
struct ParsedRenderGraphSetPassEnabledQuery {
    bool valid = false;
    std::string errorMessage;
    std::string name;
    bool enabled = false;
};
ParsedRenderGraphSetPassEnabledQuery ParseRenderGraphSetPassEnabledQuery(
    const std::string& nameParam, const std::string& enabledParam);

// (an IDENTICAL shape, ParsedRenderGraphSetFeatureEnabledQuery /
// ParseRenderGraphSetFeatureEnabledQuery, reusing/duplicating the same
// name+bool validation logic - a few lines of duplication here is
// acceptable, exactly like this file's own existing precedent of NOT
// sharing parse logic across genuinely different endpoints even when the
// shape happens to match.)

struct ParsedRenderGraphSetFeaturePriorityQuery {
    bool valid = false;
    std::string errorMessage;
    std::string name;
    std::int32_t priority = 0;
};
ParsedRenderGraphSetFeaturePriorityQuery ParseRenderGraphSetFeaturePriorityQuery(
    const std::string& nameParam, const std::string& priorityParam);

struct ParsedRenderGraphSetBoolQuery {
    bool valid = false;
    std::string errorMessage;
    bool enabled = false;
};
ParsedRenderGraphSetBoolQuery ParseRenderGraphSetBoolQuery(const std::string& enabledParam);
```

Validation rules (mirror `ParseFrameDebuggerEnableQuery()`'s own exact
`"true"`/`"false"` string handling for every `enabled` parameter): `name`
must be non-empty (400 if empty/missing); `enabled` must be exactly
`"true"` or `"false"` (400 otherwise — never accept `"1"`/`"0"`, matching
the existing convention exactly); `priority` must parse as a valid base-10
integer (400 on parse failure — use the SAME parsing helper
`ParseFrameDebuggerSelectEventQuery()`'s own int-parsing already uses, do
not invent a second one).

New response-JSON builders (mirror `BuildFrameDebuggerCommandResponseJson()`'s
snake_case style):

```cpp
std::string BuildRenderGraphControlCommandResponseJson(bool success, const std::string& errorMessage);
std::string BuildRenderGraphControlPassStatesResponseJson(
    const std::vector<RenderGraphControlPassStateOutcome>& passStates);
```

(`BuildRenderGraphControlPassStatesResponseJson()`'s shape:
`{"passes":[{"name":"RenderOpaque","enabled":true,"ever_declared_this_session":true}, ...]}`
— confirm this matches `PHASE0_MASTER_STRATEGY.md`'s own locked contract
table exactly before finalizing.)

### Step 3.6 — `NetworkServer.cpp` — the 6 route registrations

A shared response-mapping helper, mirroring
`RespondWithFrameDebuggerCommandResult()` exactly:

```cpp
void RespondWithRenderGraphControlCommandResult(
    httplib::Response& res, const RenderGraphControlCommandBridge::SubmitResult& submit)
{
    if (submit.alreadyPending) {
        res.status = 503;
        res.set_content(BuildGenericErrorResponseJson("another render graph control command is already in progress"), "application/json");
        return;
    }
    if (submit.timedOut) {
        res.status = 504;
        res.set_content(BuildGenericErrorResponseJson("render graph control command timed out"), "application/json");
        return;
    }
    const RenderGraphControlCommandResult& result = *submit.result;
    res.status = result.success ? 200 : 409;
    res.set_content(BuildRenderGraphControlCommandResponseJson(result.success, result.errorMessage), "application/json");
}
```

The 6 routes (`RegisterRoutes()` gains a new
`RenderGraphControlCommandBridge* renderGraphControlCommandBridge` parameter,
mirroring `frameDebuggerCommandBridge`'s own):

```
GET /render_graph/set_pass_enabled?name=<string>&enabled=true|false
GET /render_graph/passes
GET /render_graph/set_feature_enabled?name=<string>&enabled=true|false
GET /render_graph/set_feature_priority?name=<string>&priority=<int>
GET /render_graph/set_blur_enabled?enabled=true|false
GET /render_graph/set_gbuffer_enabled?enabled=true|false
```

Each mirrors `/frame_debugger/enable`'s exact shape: parse ->
`renderGraphControlCommandBridge == nullptr` check (503) -> build request ->
`SubmitAndWait()` -> `RespondWithRenderGraphControlCommandResult()` (for the
5 mutation routes) — `GET /render_graph/passes` is the one exception (no
query params to parse, and its OWN response body is
`BuildRenderGraphControlPassStatesResponseJson(submit.result->passStates)`
on success, not the generic command-result JSON — mirror
`/frame_debugger/state`'s own special-cased handling of its own read-only
response shape, immediately below the shared helper in that file).

### Step 3.7 — Build system + Tier-1 tests

- Root `CMakeLists.txt`: add `RenderGraphControlCommandBridge.h`/`.cpp` to
  the **`gte_core`** source list (CONFIRMED, not a guess — `search_in_dir`
  for `FrameDebuggerCommandBridge.cpp` in root `CMakeLists.txt` shows it is
  listed inside the same `add_library(gte_core STATIC ...)` block every
  other `src/Application/*.cpp` file already belongs to; there is no
  separate `gte_application` target in this codebase).
- `tests/CMakeLists.txt` + new file
  `tests/Application/RenderGraphControlCommandBridgeTests.cpp` — mirror
  `tests/Application/FrameDebuggerCommandBridgeTests.cpp`'s existing test
  cases 1:1 (timeout, fulfilled result, already-pending, fulfill-when-idle
  no-op, late-fulfillment-after-timeout, pending-state-observable) —
  substituting only the type names, since the bridge's OWN mechanics are
  identical.
- `tests/Network/NetworkRoutesTests.cpp` — add Tier-1 coverage for every
  new `Parse*`/`Build*` function pair (mirror whatever existing test already
  covers `ParseFrameDebuggerEnableQuery()`/`BuildFrameDebuggerCommandResponseJson()`).

### Verification

1. Incremental build: `cmake --build build`.
2. Run every new/extended test (bridge tests + `NetworkRoutesTests.cpp`
   additions).
3. **Live, end-to-end HTTP smoke test — the actual proof this whole
   campaign works:**
   - `run_app_background` the real `GreatTamanaEditor.exe`.
   - `gte_send_request("/render_graph/passes")` — confirm `200` and a JSON
     body listing several real built-in pass names (e.g. `"RenderOpaque"`,
     `"DrawSkyBackground"`) all `enabled: true` (assuming the Editor has
     rendered at least one frame by the time this request lands).
   - `gte_send_request("/render_graph/set_pass_enabled?name=RenderTransparent&enabled=false")` —
     confirm `200`.
   - `gte_send_request("/render_graph/passes")` AGAIN — confirm
     `"RenderTransparent"` now reports `enabled: false`.
   - `gte_send_request("/render_graph")` — confirm `"RenderTransparent"` no
     longer appears anywhere in `offscreen_regime.passes`/
     `present_regime.passes` (the real, end-to-end proof the disable
     genuinely took effect in the live render graph, not just in the
     registry's own bookkeeping).
   - `gte_send_request("/render_graph/set_pass_enabled?name=RenderTransparent&enabled=true")` —
     re-enable it (leave the session in a clean state), confirm it
     reappears in a subsequent `GET /render_graph`.
   - `gte_send_request("/render_graph/set_pass_enabled?name=Present&enabled=false")` —
     confirm `409` (the deny-list actually refuses).
   - **THE single most important check in this whole campaign's live
     verification — proves PHASE1's own Step 3.4b fix genuinely works, not
     just compiles**: `gte_send_request("/activate_tab?name=Game")` then
     `gte_send_request("/get_swapchain")` + `load_image` (baseline
     screenshot, atmosphere haze visible as normal), then
     `gte_send_request("/render_graph/set_pass_enabled?name=AtmosphereComposite&enabled=false")` —
     confirm `200` — then `gte_send_request("/get_swapchain")` + `load_image`
     AGAIN and visually compare: the Editor must still be running (no crash/
     hang), and the image should look visibly DIFFERENT from the baseline
     (no atmosphere/aerial-perspective compositing applied — PHASE0's own
     "Known risk" note says this may look stale/wrong for a frame or two,
     which is an ACCEPTED, honestly-disclosed outcome, not a failure,
     PROVIDED the Editor keeps running with no crash). Then
     `gte_send_request("/render_graph/set_pass_enabled?name=AtmosphereComposite&enabled=true")` —
     re-enable it, confirm `200`, and confirm a THIRD `/get_swapchain`
     screenshot looks like the original baseline again. Record all three
     screenshots' observed outcome honestly in `PHASE5_COMPLETION_REPORT.md` —
     if this check reveals a crash/hang/genuinely broken recovery (not just a
     visibly-different frame), STOP and `ask_questions` rather than silently
     shipping a broken toggle.
   - If any `_v2` plugin is loaded this session:
     `gte_send_request("/render_graph/set_feature_enabled?name=<realName>&enabled=false")`
     then `gte_send_request("/render_graph")` — confirm that entry's
     `render_features[].enabled` is now `false` and it no longer visibly
     composites (a screenshot diff via `/get_swapchain` + `load_image` is a
     bonus, not required, if no loaded plugin has an obviously visible
     effect). Re-enable it afterward.
   - `gte_send_request("/render_graph/set_blur_enabled?enabled=true")` then
     `gte_send_request("/activate_tab?name=Scene")` +
     `gte_send_request("/get_swapchain")` + `load_image` — confirm the
     Scene panel's own "Show Compute Blur (debug)" checkbox is now visibly
     CHECKED (proving the HTTP path and the Scene panel's own checkbox
     share the exact same underlying state, per this campaign's own
     design). Set it back to `false` afterward.
   - `gte_send_request("/get_logs?limit=80")` — confirm no unexpected
     warning/error text was logged by any of the above.
   - `stop_app_background` afterward.
4. `git_status` — confirm the diff touches EXACTLY: the 2 new bridge files,
   `EditorHost.cpp`, `NetworkServer.h/.cpp`, `NetworkRoutes.h/.cpp`, root
   `CMakeLists.txt`, `tests/CMakeLists.txt`, the new bridge test file, and
   the `NetworkRoutesTests.cpp` additions.

### What this phase does NOT do

- Does not add any query-parameter filtering beyond what each route's own
  contract states.
- Does not persist any state to disk (Locked Product Decision #7).
- Does not write the final `docs/conventions/networking.md` bullets (PHASE6
  owns the official, polished documentation update, after this phase's own
  live smoke test proves the real shape).
- Does not run a full build or full `ctest` pass (Workflow Rule 1 — Phase 6
  only).

### Completion

Write `PHASE5_COMPLETION_REPORT.md` (every real HTTP request/response pair
captured verbatim during the live smoke test above — this is the single
most important piece of evidence in this whole campaign, since it proves
every requirement from the original story actually works end-to-end), then
`git_add` + `git_commit`.
