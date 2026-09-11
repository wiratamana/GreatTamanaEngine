# PHASE3 — Application Wiring + Frame-Loop Integration

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it first.
**Also read:** `PHASE1_COMPLETION_REPORT.md` and `PHASE2_COMPLETION_REPORT.md`
before starting.

## Step 1: The Goal

Make `Application` own the new `EditorUiCommandBridge` (Phase 1), hand its
address to `NetworkServer`'s constructor (so Phase 4's route handler can
reach it), and drain at most one pending `ActivateTab` command per frame, at
the correct point in `Application::Run()`, calling into
`IEditorLayer::ActivateTab()` (Phase 2) and feeding the result back through
the bridge — with ZERO network/httplib code touched yet (that is Phase 4).

## Step 2: The Situation / The Problem

- `Application` already owns two bridges this way —
  `m_captureBridge`/`m_commandBridge` — both declared in `Application.h`
  BEFORE `m_networkServer` (so their addresses stay valid for
  `m_networkServer`'s own constructor call), and both drained once per frame
  inside `Application::Run()` at a specific, deliberate point (see
  `Application.cpp`). This phase adds a THIRD bridge, following the exact
  same ownership/construction-order pattern.
- **The critical new question this phase must answer carefully: WHERE, in
  `Application::Run()`'s existing frame body, is it actually correct to call
  `IEditorLayer::ActivateTab()`?**
  - Dear ImGui's window/dock state (`ImGui::FindWindowByName()`/
    `SetWindowFocus()`) is only valid to touch AFTER `ImGui::NewFrame()` has
    been called for the current frame, and BEFORE that same frame's
    `BuildUI()` (which is what actually calls each panel's own
    `ImGui::Begin(...)`) runs — this is because `SetWindowFocus()`'s effect
    (which tab is selected) needs to be in place BEFORE the tabs
    themselves are drawn this frame, or the visible result lags one frame
    behind the request.
  - `Application::Run()`'s existing order is: poll SDL events -> drain
    `EngineCommandBridge` (BEFORE `Game::Update()`) -> compute `deltaSeconds`
    -> `m_editorLayer->NewFrame()` -> `m_renderer.BeginFrame()` ->
    `m_game.Update(...)` -> resolve `gameTarget`/`sceneTarget` -> the
    offscreen `RenderGraph::Execute()` block -> GPU-timing bookkeeping ->
    `m_editorLayer->BuildUI(...)` -> ... (see `Application.cpp`, already
    read in full during this campaign's initial research — re-read it now
    if you have not already).
  - Therefore the new bridge must be drained **immediately after
    `m_editorLayer->NewFrame();` and before `m_editorLayer->BuildUI(...)`
    is called** — anywhere in that window is technically correct (nothing
    between those two calls that this feature cares about touches ImGui
    window/dock state), but the EARLIEST point in that window
    (immediately after `NewFrame()`) is the right choice, mirroring
    `EngineCommandBridge`'s own "drain as early as possible" precedent
    (see `Application.cpp`'s own comment on why `EngineCommandBridge` is
    drained right after input polling — the same "make this frame fully
    consistent with the request" reasoning, just relative to `NewFrame()`/
    `BuildUI()` instead of relative to `Game::Update()`).
  - Draining it BEFORE `NewFrame()` would be wrong: `ImGuiEditorLayer`'s
    ImGui context is not in a valid "currently building a frame" state
    until `NewFrame()` runs, and `ImGui::FindWindowByName()`/
    `SetWindowFocus()` calls made before that point are operating on STALE
    state left over from the previous frame's `EndFrame()`/`Render()` —
    this is a real correctness requirement, not just a style preference.
  - Draining it AFTER `BuildUI()` would make the tab-selection change only
    visible starting the FOLLOWING frame (since `BuildUI()` this frame
    already drew every tab with the OLD selection state) — a real, visible
    one-frame lag a caller could observe as "the response says success but
    the screenshot I take right after doesn't show it yet."

## Step 3: The Plan

### 3.1 — `src/Application/Application.h`: new member + constructor include

Add `#include "EditorUiCommandBridge.h"` to the includes list (alongside
the existing `#include "EngineCommandBridge.h"` / `#include
"FrameCaptureBridge.h"`).

Add the new member, declared immediately after `m_commandBridge` (BEFORE
`m_networkServer`, for the exact same "constructed first, destroyed last
relative to it, so its address stays valid for the constructor call below"
reasoning already documented for `m_captureBridge`/`m_commandBridge` —
copy/adapt that existing comment block):

```cpp
    // network-impl-7 campaign - the THIRD sanctioned cross-thread bridge a
    // Network route handler is allowed to touch, this one for EDITOR-UI
    // commands (activate_tab - see AGENTS.md, "Networking", and
    // EditorUiCommandBridge.h's own header comment). Declared right after
    // m_commandBridge, for the exact same reason: BEFORE m_networkServer
    // (constructed first, destroyed last relative to it) so its address
    // can be handed into m_networkServer's own constructor below.
    EditorUiCommandBridge m_uiCommandBridge;
```

### 3.2 — `src/Network/NetworkServer.h`/`.cpp`: extend the constructor

`NetworkServer`'s constructor currently takes two defaulted, non-owning
pointers (`FrameCaptureBridge*`, `EngineCommandBridge*`). Add a THIRD,
appended AFTER the existing two (preserving every existing call site,
including every `tests/Network/NetworkServerTests.cpp` no-argument
construction and `Application`'s own single/two-argument-worth-of-real-
pointers call), following `commandBridge`'s own precedent exactly:

```cpp
// Forward-declared for the same cheap-header reason as FrameCaptureBridge/
// EngineCommandBridge above - network-impl-7 campaign.
namespace gte { class EditorUiCommandBridge; }
```

```cpp
    // `uiCommandBridge` (network-impl-7 campaign) is a THIRD defaulted,
    // non-owning pointer, appended AFTER `commandBridge` so every existing
    // call site keeps compiling unchanged. Non-null in production
    // (Application owns the real EditorUiCommandBridge and passes its
    // address) - `nullptr` means "GET /activate_tab responds 503 rather
    // than crashing" - the exact same "nullptr degrades gracefully" contract
    // `captureBridge`/`commandBridge` already document above.
    explicit NetworkServer(FrameCaptureBridge* captureBridge = nullptr,
        EngineCommandBridge* commandBridge = nullptr,
        EditorUiCommandBridge* uiCommandBridge = nullptr);
```

Add the matching private member (`EditorUiCommandBridge* m_uiCommandBridge
= nullptr;`, same doc-comment style as `m_commandBridge`), and thread it
through the constructor's member-initializer list and into
`RegisterRoutes(...)`'s own parameter list in `NetworkServer.cpp` (Phase 4
is what actually uses it inside `RegisterRoutes()` to register the new
routes — this phase only needs to make sure the pointer arrives there
correctly; Phase 4 adds the `GET /activate_tab` registration call itself).

**Do not register any route in this phase.** `RegisterRoutes()`'s
signature gains the new parameter and forwards it, but its BODY gains no
new `server.Get(...)` call until Phase 4 — keep this phase's diff scoped to
plumbing only, so a compile failure here is trivially attributable to
"the plumbing is wrong," not "the new route is wrong."

### 3.3 — `src/Application/Application.cpp`: construct + wire the bridge

Update `Application`'s constructor member-initializer list: pass
`&m_uiCommandBridge` as the third argument to `m_networkServer`'s own
constructor call (currently `m_networkServer(&m_captureBridge,
&m_commandBridge)` — becomes `m_networkServer(&m_captureBridge,
&m_commandBridge, &m_uiCommandBridge)`), and update that line's own
existing comment to mention the third bridge, mirroring the comment already
there for the first two.

### 3.4 — `src/Application/Application.cpp`: drain the bridge in `Run()`

Immediately after the existing line:

```cpp
        m_editorLayer->NewFrame();
```

insert the new drain step:

```cpp
        // network-impl-7 campaign - drains at most ONE pending
        // GET /activate_tab request per frame, IMMEDIATELY after
        // NewFrame() and BEFORE BuildUI() - this is the one window in the
        // frame where Dear ImGui's window/dock state is valid to touch
        // (NewFrame() already ran) AND where a change here is still
        // visible in THIS SAME frame's own tab rendering (BuildUI() has
        // not run yet - see PHASE3_APPLICATION_WIRING_AND_FRAME_LOOP_INTEGRATION.md's
        // own "why this exact frame position" reasoning for the full
        // justification). Mirrors EngineCommandBridge's own "drain as
        // early as possible" precedent above, just relative to a different
        // pair of per-frame calls.
        if (const std::optional<EditorUiCommandRequest> uiRequest = m_uiCommandBridge.TryPeekPendingCommandRequest()) {
            GTE_PROFILE_SCOPE("Application::ExecuteEditorUiCommand");
            EditorUiCommandResult uiResult;
            uiResult.kind = uiRequest->kind;
            // Only one EditorUiCommandKind exists today (ActivateTab) - a
            // future addition to this bridge (see EditorUiCommandBridge.h's
            // own doc comment on why it's an enum, not a single hardcoded
            // shape) would branch on uiRequest->kind here, the exact same
            // shape ExecuteEngineCommand() (EngineCommandDispatch.cpp)
            // already uses for ITS bridge's own multiple kinds.
            const TabActivationResult activation = m_editorLayer->ActivateTab(uiRequest->activateTab.tabName);
            uiResult.activateTab.tabExists = activation.tabExists;
            uiResult.activateTab.success = activation.tabExists;
            m_uiCommandBridge.FulfillCommand(uiResult);
        }
```

**Placement note:** this new block goes AFTER `m_editorLayer->NewFrame();`
and BEFORE `m_renderer.BeginFrame();` — i.e. as the very next statement
after `NewFrame()`, ahead of everything else already there. Do not place it
after `m_game.Update(...)` or anywhere near the `RenderGraph::Execute()`
blocks — none of that ordering matters for THIS feature (it does not touch
`Game`/ECS/Renderer at all), and placing it earlier keeps the "as early as
possible" principle intact without needing to reason about interactions
with the rest of the frame body at all.

`Application.cpp` already `#include`s `<optional>` transitively (used
elsewhere in this same function for `EngineCommandRequest`) — no new
include should be needed for `std::optional`, but verify this explicitly
rather than assume.

### 3.5 — Do not add any test in this phase

This phase is pure wiring inside `Application`/`NetworkServer`'s
constructors and `Run()`'s frame body — none of it is independently
Tier-1-testable in isolation from a live `Application`/window/Vulkan device
(the same "Tier 2, no automated coverage yet" bucket `Application::Run()`
itself already falls into — see `AGENTS.md`, "Testability & Regression
Safety"). Verification for this phase is compile-success plus the manual
smoke test below; Phase 4's own end-to-end tests (Phase 5) are what
actually exercise this wiring automatically, once a real HTTP route exists
to drive it through.

## Step 4: Verification for this phase

- `cmake --build build` succeeds.
- Manual smoke test, replacing Phase 2's now-removed temporary call site:
  temporarily have a quick script/manual step submit a fake request
  directly against `m_uiCommandBridge` is NOT possible without a route yet
  — instead, confirm correctness by CODE INSPECTION against Section 2's own
  "why this exact frame position" reasoning, since there is no way to
  exercise this bridge end-to-end until Phase 4 exists. If you want a
  concrete manual check anyway: temporarily add a one-line diagnostic
  (`std::fprintf(stderr, ...)`) inside the new drain block, confirm it
  never fires spuriously during normal operation (nothing should ever be
  "pending" in this bridge yet, since nothing can submit to it until
  Phase 4 exists), then remove the diagnostic before committing.
- No behavior change to any EXISTING endpoint, panel, or frame-loop step —
  this phase is strictly additive.
- Write `PHASE3_COMPLETION_REPORT.md` in this same folder, `git add`/
  `git commit` the changes + report together.
