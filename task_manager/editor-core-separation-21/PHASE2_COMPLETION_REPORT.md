# PHASE2 COMPLETION REPORT — Fix the `AtmosphereAerialPerspectiveCompositePass` Toggle Lie

Campaign: `editor-core-separation-21` ("The Engine Is Lying" — Render Pass / Frame Debugger Honesty Campaign)
Phase: PHASE2 (`PHASE2_FIX_AERIAL_PERSPECTIVE_COMPOSITE_TOGGLE_LIE.md`)
Branch: `feature/editor-core-separation` (unchanged, as required)

---

## Which branch applied

**Branch B** (`PHASE2_FIX_AERIAL_PERSPECTIVE_COMPOSITE_TOGGLE_LIE.md` Step 3.2) —
PHASE1's confirmed root cause was a genuinely stale Frame Debugger capture, not
a toggle-registry correctness bug: the mechanism that decides whether
`AtmosphereAerialPerspectiveCompositePass` gets declared into the Render Graph
was already 100% correct every frame; the bug was that **nothing ever told
the Frame Debugger's own frozen, one-shot capture to refresh itself** the
instant a built-in pass's enabled state changed, so a caller's very first
`GET /frame_debugger/capture` after a toggle would always show whatever the
PREVIOUS capture already contained (one full capture cycle behind).

No `ask_questions` call was needed — PHASE1's finding mapped cleanly onto
Branch B's own prescribed fix shape (add a fourth, automatic capture trigger),
and no new design ambiguity was discovered while implementing it.

---

## The fix — exact diff summary

### 1. A new, pure, Tier-1-tested comparison — `RenderPassToggleChangeDetectionLogic.h`

New file `src/Renderer/RenderGraph/RenderPassToggleChangeDetectionLogic.h`
(mirrors `AtmospherePassToggleLogic.h`'s own precedent of extracting a
decision into its own dependency-free header):

```cpp
inline bool DidRenderPassToggleEnabledStatesChange(
    const std::vector<RenderPassToggleState>& before, const std::vector<RenderPassToggleState>& after) noexcept
{
    if (before.size() != after.size()) {
        return true;
    }
    for (std::size_t i = 0; i < before.size(); ++i) {
        if (before[i].name != after[i].name || before[i].enabled != after[i].enabled) {
            return true;
        }
    }
    return false;
}
```

Deliberately compares only `name`/`enabled` (never `everDeclaredThisSession`,
since a pass simply running for the first time this session with its enabled
state unchanged is not itself a reason to force a recapture). `ListAll()`
already returns entries sorted by name, so a plain index-aligned comparison
is correct with no re-sort needed.

New Tier-1 test file:
`tests/Renderer/RenderGraph/RenderPassToggleChangeDetectionLogicTests.cpp` (5
tests: identical-empty, nothing-changed, one-pass-disabled, a-newly-registered
pass, and confirming `everDeclaredThisSession` alone never counts as a
change), registered in `tests/CMakeLists.txt`. All 5 pass:

```
[----------] 5 tests from RenderPassToggleChangeDetectionLogicTest
[ RUN      ] RenderPassToggleChangeDetectionLogicTest.ReturnsFalseForTwoIdenticalEmptySnapshots
[       OK ] RenderPassToggleChangeDetectionLogicTest.ReturnsFalseForTwoIdenticalEmptySnapshots (0 ms)
[ RUN      ] RenderPassToggleChangeDetectionLogicTest.ReturnsFalseWhenNothingChanged
[       OK ] RenderPassToggleChangeDetectionLogicTest.ReturnsFalseWhenNothingChanged (0 ms)
[ RUN      ] RenderPassToggleChangeDetectionLogicTest.ReturnsTrueWhenOnePassWasDisabled
[       OK ] RenderPassToggleChangeDetectionLogicTest.ReturnsTrueWhenOnePassWasDisabled (0 ms)
[ RUN      ] RenderPassToggleChangeDetectionLogicTest.ReturnsTrueWhenAPassWasNewlyRegistered
[       OK ] RenderPassToggleChangeDetectionLogicTest.ReturnsTrueWhenAPassWasNewlyRegistered (0 ms)
[ RUN      ] RenderPassToggleChangeDetectionLogicTest.IgnoresEverDeclaredThisSessionOnItsOwn
[       OK ] RenderPassToggleChangeDetectionLogicTest.IgnoresEverDeclaredThisSessionOnItsOwn (0 ms)
[----------] 5 tests from RenderPassToggleChangeDetectionLogicTest (1 ms total)
[  PASSED  ] 5 tests.
```

Pre-existing `RenderPassToggleRegistryTest`/`AtmospherePassToggleLogicTest`
suites (12 tests) still pass unchanged, confirming reverting PHASE1's
temporary instrumentation in those files caused no regression.

### 2. The FOURTH Frame Debugger capture trigger — two mutation paths, two call sites

There are exactly two places in the engine a built-in pass's enabled state
can actually flip:

- **The Render Graph panel's own checkboxes** (`Panels/RenderGraphPanel.cpp`,
  `BuildPassRow()`/`BuildDisabledBuiltInPassesSection()`) — runs on the main
  thread, DURING `ImGuiEditorLayer::BuildUI()`, BEFORE `FrameDebuggerPanel::Build()`
  runs later that same call.
- **`GET /render_graph/set_pass_enabled`** (`EditorHost.cpp`'s
  `RenderGraphControlCommandBridge` pump, `SetBuiltInPassEnabled` case) — runs
  on the main thread too, but EARLIER, BEFORE `BuildUI()` even starts.

Both needed a fix, with two different mechanisms because of that timing
difference:

**(a) `RenderGraphPanel::Build()`** now snapshots
`renderPassToggleRegistry.ListAll()` before its own checkbox-drawing sections
run, and again right after both `BuildRegimeSection()` calls (the last place
a checkbox in this panel can mutate the registry). If
`DidRenderPassToggleEnabledStatesChange()` says anything changed, it sets a
new shared flag:

```cpp
if (rg::DidRenderPassToggleEnabledStatesChange(toggleStatesBeforeThisPanelsOwnUi, renderPassToggleRegistry.ListAll())) {
    ctx.renderPassToggleRegistryChangedThisFrame = true;
}
```

**(b) A new `EditorContext::renderPassToggleRegistryChangedThisFrame` bool**
(`src/Editor/EditorContext.h`) is the shared channel between the two panels
(both are free functions/classes called from the same `ImGuiEditorLayer::BuildUI()`,
in the same fixed order, every frame — mirroring the existing
`showBlurredSceneOutput`/`showGBufferValidationOutput` "plain EditorContext
bool as the cross-panel channel" convention already established in this
codebase).

**(c) `FrameDebuggerPanel::Build()`** consumes (read-and-clears) that flag
unconditionally, every call, right after its existing
`m_wasPlaybackPaused = ctx.playbackPaused;` line:

```cpp
if (ctx.renderPassToggleRegistryChangedThisFrame) {
    ctx.renderPassToggleRegistryChangedThisFrame = false;
    if (m_enabled) {
        m_pendingCaptureTrigger = true;
        GTE_LOG_DEBUG("FrameDebugger",
            "Build() - RenderPassToggleRegistry changed this frame while Enabled - arming an automatic "
            "re-capture for the next frame (editor-core-separation-21 campaign, PHASE2).");
    }
}
```

This exactly mirrors `ApplyEnabledEdge()`'s own false->true edge: it only ARMS
`m_pendingCaptureTrigger` (never calls `TriggerCapture()` synchronously), and
only while `m_enabled` is true — the same "Capture button" gating
`CaptureNowFromCommand()` already uses.

**(d) `EditorHost.cpp`'s `SetBuiltInPassEnabled` case** (the HTTP/bridge path,
which runs before `BuildUI()` even starts this frame and therefore cannot use
the same-frame `ctx` flag) instead calls the Frame Debugger directly, reusing
the EXISTING `IEditorLayer::FrameDebuggerCaptureNow()` method (the same one
`FrameDebuggerCommandKind::CaptureNow` already calls) the moment `SetEnabled()`
reports `applied == true`:

```cpp
} else {
    // ... this mutation runs BEFORE BuildUI() even starts this frame ...
    m_editorLayer->FrameDebuggerCaptureNow();
}
```

`FrameDebuggerCaptureNow()`/`CaptureNowFromCommand()` is already a safe no-op
(returns `false`, ignored here) whenever the Frame Debugger isn't currently
`Enabled` — no new guard needed.

**No new "kind of mechanism" was invented anywhere** — both paths funnel into
the exact same pre-existing `m_pendingCaptureTrigger` deferred-capture
handshake (`frame-debugger-7` campaign's own PHASE2/PHASE3 design), joining
the Enable-edge / Step / explicit "Capture" button as this campaign's
documented fourth trigger.

### 3. PHASE1's temporary `"RenderPassHonestyDiag"` instrumentation — removed

Per Step 3.6 point 3, every temporary, unconditional-every-frame diagnostic
`GTE_LOG_DEBUG` call PHASE1 added was reverted to clean code (confirmed no
regression via the pre-existing `RenderPassToggleRegistryTest` suite, above):

- `src/Renderer/RenderGraph/RenderPassToggleRegistry.cpp` — both
  `NoteDeclaredAndCheckEnabled()`/`SetEnabled()` diagnostic blocks removed
  (and the now-unused `#include "../../Core/Logging.h"` removed too).
- `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp` — the
  `AddAerialPerspectiveCompositePass()` declare-time diagnostic block removed
  (and its now-unused `Logging.h` include removed).
- `src/Core/Core.cpp` — both `"AtmosphereComposite"` provider diagnostic
  blocks removed (and its now-unused `Logging.h` include removed — confirmed
  via `search_in_dir` that no other `GTE_LOG_*` call existed anywhere else in
  this large file).

One diagnostic call site was judged to have **permanent conceptual value for
PHASE5** (per Step 3.6 point 3's own explicit exception) and was NOT deleted
outright: `FrameDebuggerPanel::TriggerCapture()`'s block that checked whether
`"AtmosphereAerialPerspectiveCompositePass"` appeared in the just-captured
`graphSnapshot`. Its single-pass-name-hardcoded `GTE_LOG_DEBUG` call itself
was removed (it added noisy per-capture logging with no permanent, generic
value on its own), but a `// TODO(editor-core-separation-21 PHASE5)` comment
was left in its exact place, spelling out that this is precisely where
PHASE5's planned generic mismatch detector (comparing
`graphSnapshot.passesInExecutionOrder` against `RenderPassToggleRegistry`'s
own recorded state for every pass name) belongs.

---

## Mandatory shared verification (Step 3.6) — full live transcript

Environment: `build\GreatTamanaEditor.exe`, launched via `run_app_background`,
driven purely over `gte_send_request` (`http://127.0.0.1:8080`), closed via
`stop_app_background` before finishing — no stray instance left running.

### Incremental compile check

`cmake --build build` succeeded cleanly (Ninja, incremental — one real
mistake was made and self-corrected mid-phase: an `edit_line` insertion
accidentally replaced-away the pre-existing `#include
"../../Renderer/RenderGraph/RenderGraph.h"` line in `RenderGraphPanel.cpp`
instead of purely adding a new include next to it, causing an
"incomplete type `RenderGraph`"/"`ExecuteTimingMode` has not been declared"
compile failure — caught immediately by this exact build, fixed by
re-adding the missing include, and reconfirmed with a clean subsequent build).

### Live HTTP repro — the EXACT script from PHASE1, start to finish

```
GET /frame_debugger/open
  -> {"state":{...,"enabled":false,"hasCapturedFrame":false,...},"success":true}
GET /frame_debugger/enable?value=true
  -> {"state":{...,"enabled":true,"hasCapturedFrame":false,...},"success":true}
GET /render_graph/passes
  -> {"passes":[{"enabled":true,...,"name":"AtmosphereAerialPerspectiveCompositePass"}, ...]}
GET /frame_debugger/capture
  -> {"state":{...,"hasCapturedFrame":true,...,"totalEventCount":63,...},"success":true}
```
Baseline confirmed: pass enabled, tree has 63 total events (identical to
PHASE1's own baseline).

```
GET /render_graph/set_pass_enabled?name=AtmosphereAerialPerspectiveCompositePass&enabled=false
  -> {"success":true}
GET /render_graph/passes
  -> {"passes":[{"enabled":false, ...,"name":"AtmosphereAerialPerspectiveCompositePass"}, ...]}   <- mutation applied instantly
GET /frame_debugger/state
  -> {"channel":"all","enabled":true,"hasCapturedFrame":true,"levelsBlack":0.0,"levelsWhite":1.0,
      "selectedEventIndex":-1,"totalEventCount":61,"windowOpen":true}
```

**This is the fix, proven**: unlike PHASE1's own reproduction (which needed a
SECOND `/frame_debugger/capture` call before `totalEventCount` ever dropped to
61), a plain `GET /frame_debugger/state` — with **no explicit capture request
at all** — already shows the corrected `totalEventCount:61` (2 fewer events:
the pass's own leaf plus its "Compute Dispatch" child), because
`EditorHost.cpp`'s `SetBuiltInPassEnabled` case called
`IEditorLayer::FrameDebuggerCaptureNow()` immediately upon the toggle,
arming, and by the time of the request above, completing, a fresh capture on
its own.

```
GET /frame_debugger/capture
  -> {"state":{...,"totalEventCount":61,...},"success":true}   <- still correct, no stale re-appearance
```

### Re-enable — bidirectional confirmation

```
GET /render_graph/set_pass_enabled?name=AtmosphereAerialPerspectiveCompositePass&enabled=true
  -> {"success":true}
GET /frame_debugger/state
  -> {"channel":"all","enabled":true,"hasCapturedFrame":true,...,"totalEventCount":63,...}   <- already fresh again
GET /frame_debugger/capture
  -> {"state":{...,"totalEventCount":63,...},"success":true}   <- confirmed, settled
```

### No crash, no new warning/error, sane rendered image

```
GET /get_logs?min_level=Warning&limit=20   -> 20 entries, all pre-existing, unrelated startup warnings
                                               (plugin render-feature priority-tie/GPU-timing-slot-budget
                                               notices from frame 0/1) - nothing new caused by this fix.
GET /get_logs?min_level=Error&limit=20     -> {"count":0,"entries":[],...}   <- zero errors
GET /render_graph/passes                    -> AtmosphereAerialPerspectiveCompositePass: enabled=true
                                               (left in the default, working state)
GET /get_game_view                          -> HTTP 200, image/png, 158923 bytes - a sane, solid blue
                                               atmosphere-tinted sky image (no magenta, no corruption)
```

`stop_app_background` closed the engine cleanly afterward.

---

## Tier-1 test obligation (AGENTS.md / PHASE0 Step 3.1 point 3)

`DidRenderPassToggleEnabledStatesChange()` is the one genuinely new piece of
`gte_core`-tier pure logic this phase introduced, and it now has its own
5-test file (`RenderPassToggleChangeDetectionLogicTests.cpp`, all passing —
see above). The Frame Debugger trigger-consumption code itself
(`FrameDebuggerPanel::Build()`) and the `RenderGraphPanel::Build()` call site
remain Tier-2 (they need a live `EditorContext`/ImGui frame), exactly like
every other capture-trigger site in this same file already is — this matches
`AtmospherePassToggleLogic.h`'s own established precedent of extracting only
the PURE decision, not the stateful call site around it.

---

## Files changed

- `src/Renderer/RenderGraph/RenderPassToggleChangeDetectionLogic.h` (new)
- `tests/Renderer/RenderGraph/RenderPassToggleChangeDetectionLogicTests.cpp` (new)
- `tests/CMakeLists.txt` (registered the new test file, plus its own
  documentation-comment entry)
- `src/Editor/EditorContext.h` (new `renderPassToggleRegistryChangedThisFrame` field)
- `src/Editor/Panels/RenderGraphPanel.cpp` (before/after toggle-state snapshot
  + `ctx` flag set)
- `src/Editor/Panels/FrameDebuggerPanel.cpp` (fourth capture trigger consuming
  that flag; PHASE1's `TriggerCapture()` diagnostic trimmed to a `TODO(PHASE5)`
  comment)
- `src/Editor/EditorHost.cpp` (`SetBuiltInPassEnabled` case now calls
  `FrameDebuggerCaptureNow()` on success)
- `src/Renderer/RenderGraph/RenderPassToggleRegistry.cpp` (PHASE1 diagnostic
  instrumentation reverted)
- `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp` (PHASE1 diagnostic
  instrumentation reverted)
- `src/Core/Core.cpp` (PHASE1 diagnostic instrumentation reverted)

## What remains open (explicitly, for PHASE3/PHASE4/PHASE5 — not this phase's job)

- The already-known `DemoRenderFeaturePlugin_Clear`/
  `DemoRenderFeatureSecondPlugin_Clear` inert toggles (PHASE0 Step 2.2) are
  untouched by this phase — that is PHASE3 (audit)/PHASE4 (fix)'s job, not
  PHASE2's (this phase's own scope was strictly the one reported
  `AtmosphereAerialPerspectiveCompositePass` lie).
- `src/Application/RenderPasses.cpp`'s `AddGpuSkinningPasses()` direct-render-
  to-swapchain-fallback path (PHASE0 Step 2.2b) is untouched by this phase —
  also PHASE3/PHASE4's job.
- PHASE5's permanent, automatic "Render Pass Honesty" mismatch detector does
  not exist yet — this phase only left a `TODO` comment marking its natural
  home; no `GTE_LOG_ERROR`-firing detector has been built.

## No delegation, no unresolved ambiguity

No `delegate_task` call was made (none permitted for this phase). No
`ask_questions` call was needed — PHASE1's finding mapped directly onto one of
PHASE2's own pre-written branches with no genuine design ambiguity
encountered during implementation.
