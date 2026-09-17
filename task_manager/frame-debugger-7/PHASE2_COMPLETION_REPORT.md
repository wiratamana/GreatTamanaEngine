# PHASE2 — Completion Report

_Parent: `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE2_DEFERRED_CAPTURE_TRIGGER.md`. Builds on Phase 1's renamed
`FrameDebuggerCurrentCapture`/`m_currentCapture`._

## Summary

Bug 1 ("the very first captured frame after pressing 'Enable' is missing
objects") is fixed by deferring the Enable checkbox's false→true edge's
capture trigger to the very next `Build()` call, instead of calling
`TriggerCapture()` synchronously inside the click handler. A new
`bool m_pendingCaptureAfterEnable` member is set to `true` by
`ApplyEnabledEdge()`'s false→true branch and consumed (read-and-cleared, then
immediately followed by a real `TriggerCapture()` call) at the very top of
`BuildToolbarRow()`, before the "Enable" checkbox itself is even drawn. No
other call site, HTTP route, or UI control needed to change.

## Root cause recap (for future readers who land here directly)

`Application::Run()` calls `m_editorLayer->PrepareFrameDebuggerCaptureContext()`
**before** `Game::Render()` runs, every frame. That call only arms
(`Reset()`s and returns non-null) the real capture context when
`ctx.frameDebuggerWindowOpen && m_enabled` are **both already true at that
point in the frame**. The Enable checkbox itself is only actually clicked
later, inside `Build()`/`BuildToolbarRow()`/`ApplyEnabledEdge()` — by which
point this same frame's `Game::Render()` (and therefore every
`RenderSystem::Draw()` call) has already run with a `nullptr` capture
pointer, since `m_enabled` was still `false` when the frame started. The old
code called `TriggerCapture()` immediately, right there in
`ApplyEnabledEdge()`'s false→true branch, building a snapshot from this
same, already-stale frame — real render-graph/pass data, but **zero**
`FrameDebuggerDrawRecord`s, hence no per-entity children under `"GameView"`
(no terrain/smoke-cube rows). Pressing Enable a second time always worked
because `m_enabled` had already been `true` for the *entire* previous frame
before that capture ran.

## What changed

### `src/Editor/Panels/FrameDebuggerPanel.h`

- **New private member**: `bool m_pendingCaptureAfterEnable = false;` (added
  right after `m_stepCaptureRequested`). Doc comment explains its exact
  contract: true for exactly one `Build()` call after the Enable checkbox's
  false→true edge fires, consumed at the START of the next `Build()` call
  (via `BuildToolbarRow()`, before the checkbox is drawn). Also documents,
  as instructed by the phase doc, that **Phase 3 of this same campaign
  renames this bool to `m_pendingCaptureTrigger`** and generalizes the
  mechanism to all three capture triggers (Enable-edge, Step, Capture
  button) — this is expected future work, not something Phase 2 attempted.
- **Updated doc comments** (no signature/behavior changes to the
  declarations themselves) on:
  - The `ApplyEnabledEdge()` declaration's own doc comment (previously said
    "...triggers the very first real capture..." — now explains the
    false→true edge only *arms* `m_pendingCaptureAfterEnable`, with the real
    capture happening on the next `Build()` call).
  - The `TriggerCapture()` declaration's own doc comment (previously said
    "Called from exactly three places... see that method's own body for the
    one-frame-lag caveat the Enable-edge/Capture-button paths carry" — now
    explains that, as of Phase 2, **every** call site is already guaranteed
    to run on a frame whose capture context was correctly armed, so there is
    no lag/caveat left to carry).

### `src/Editor/Panels/FrameDebuggerPanel.cpp`

- **`ApplyEnabledEdge()`**: the false→true branch still sets
  `ctx.playbackPaused = true;` (unchanged), but the direct `TriggerCapture();`
  call was **removed** and replaced with `m_pendingCaptureAfterEnable = true;`.
  The true→false branch (Phase 1's "clear on Disable") is completely
  untouched. The function's own preceding doc comment was rewritten to
  explain the new, correct semantics in detail (arming happens next frame,
  before that frame's `Game::Render()` runs, with `m_enabled` already `true`
  for the whole frame — so by the time `Build()`/`BuildToolbarRow()` runs
  later that SAME frame and consumes `m_pendingCaptureAfterEnable`, the
  just-finished render already recorded real per-object facts).
- **`BuildToolbarRow()`**: at the very top of the function — before drawing
  the "Enable" checkbox itself — added:
  ```cpp
  if (m_pendingCaptureAfterEnable) {
      m_pendingCaptureAfterEnable = false;
      TriggerCapture();
  }
  ```
  This guarantees the tree the user sees on the very frame the capture
  "arrives" is already correct (built from a real, freshly-armed frame),
  with no extra visible delay beyond the one real, imperceptible frame the
  recorder needed to actually run once while armed.
- **`BuildEventTreePane()`**: added a one-line note (Step 3.6 of the phase
  doc) at the existing "No frame captured yet." fallback branch, explaining
  that this is also the exact state shown during the one real frame between
  the Enable checkbox's false→true edge and its deferred capture landing.
  No logic change was needed here — this already worked correctly the
  moment `TriggerCapture()` stopped being called synchronously inside the
  click handler, since `m_currentCapture.CurrentEntry()` stays `nullptr`
  until the deferred call actually runs.

### `src/Editor/EditorLayer.h`

- Updated `IEditorLayer::FrameDebuggerSetEnabled(bool enabled)`'s doc comment
  (a comment-only change — the virtual's signature/contract is unchanged) to
  stop saying the false→true edge "triggers the very first real capture" and
  instead explain that, as of this phase, it only arms a deferred one-shot
  flag consumed at the start of the next `Build()` call. No other
  `IEditorLayer` change was needed — `ImGuiEditorLayer`/`NullEditorLayer`'s
  own `FrameDebuggerSetEnabled()` overrides just forward straight into
  `FrameDebuggerPanel::SetEnabledFromCommand()` → `ApplyEnabledEdge()`, so
  they automatically inherit the fix with no code change on their part.

## Why the Step/Capture-button paths were NOT touched (Step 3.4 of the phase doc)

Read both existing `TriggerCapture()` callers other than the deferred
Enable-edge consumption:

- The explicit **"Capture" button** (`BuildToolbarRow()`,
  `ImGui::BeginDisabled(!m_enabled)` guarding it) — only clickable while
  `m_enabled` is already `true`, which means `m_enabled` was *already* `true`
  for the entirety of **this** frame, including when
  `Application::Run()` called `PrepareFrameDebuggerCaptureContext()` earlier
  this same frame, before `Game::Render()` ran. The capture context was
  therefore already correctly armed before this frame's rendering happened —
  calling `TriggerCapture()` synchronously, right when the button is
  clicked (later in the same frame, inside `BuildToolbarRow()`), is exactly
  correct and needs no deferral.
- The **Step-triggered capture** (`m_stepCaptureRequested`, serviced later in
  `BuildToolbarRow()`) — `NotifyStepConsumed()` is only ever called from
  `Application::Run()`'s Step-handling branch, which itself only runs when
  `IsPlaybackPaused()`/`TryConsumeStepRequest()` succeed — a Step can only be
  requested while Enable was already on going into that frame (Step is a
  feature of an already-Enabled, already-paused Frame Debugger session), so
  `m_enabled` was again already `true` before this frame's
  `PrepareFrameDebuggerCaptureContext()` call ran. Same conclusion: already
  correct, no change needed.

Both paths were left completely untouched (not even a comment tweak beyond
what already existed), confirming by code reading rather than by inspection
alone that neither path shares Bug 1's root cause.

## HTTP path (Step 3.5 of the phase doc)

`FrameDebuggerPanel::SetEnabledFromCommand(EditorContext&, bool)` calls
`ApplyEnabledEdge()` directly and unconditionally — no separate code path,
no duplicated logic. It automatically inherits this phase's fix with zero
additional changes: `GET /frame_debugger/enable?enable=true` now also just
arms `m_pendingCaptureAfterEnable`, consumed by the very next `Build()`
call exactly like the hand-driven checkbox. Searched the rest of
`src/Network/`, `src/Application/FrameDebuggerCommandBridge.*`, and
`ImGuiEditorLayer.cpp`/`NullEditorLayer.cpp` for any other call site that
might duplicate the old immediate-capture behavior (`grep`-style search for
`TriggerCapture`) — confirmed there is exactly ONE place `TriggerCapture()`
is called for the Enable-edge case (the new deferred consumption inside
`BuildToolbarRow()`), plus the two already-covered Capture-button/Step call
sites, plus `CaptureNowFromCommand()` (the HTTP mirror of the "Capture"
button, which — like the button itself — is guarded by `!m_enabled` and
therefore already correct for the same reason as the hand-driven button).

## Testable pure-helper decision (Step 4 of the phase doc)

The phase document offered the option to extract a tiny pure helper (e.g.
`bool shouldCaptureNow(bool pending, bool enabledThisFrame)`), but explicitly
allowed leaving the logic inline if extracting it would be pure ceremony.
This phase's actual logic is:

```cpp
if (m_pendingCaptureAfterEnable) {
    m_pendingCaptureAfterEnable = false;
    TriggerCapture();
}
```

There is no second boolean input to combine (`m_enabled` doesn't factor into
this particular decision — by the time this code runs, on the very next
frame after the false→true edge, `m_enabled` is unconditionally already
`true`, since nothing else can have flipped it back to `false` in between
without going through `ApplyEnabledEdge()`'s own true→false branch, which
would have left `m_pendingCaptureAfterEnable` false in the first place). The
"decision" is therefore just "consume the flag if set" — a single boolean
read-and-clear with no branching logic worth its own unit test beyond what
the existing `FrameDebuggerCommandBridgeTest`/`FrameDebuggerCurrentCaptureTest`
suites already exercise indirectly. Extracting a one-line `if` into its own
named free function would be pure ceremony here, so — per the phase
document's own explicit permission — it was left inline, with the reasoning
above written down (this section) instead of silently skipped.

## Verification performed (compile check only, per this phase's own Step 4)

- `cmake --build build --target gte_core -j 4` — succeeded, rebuilding
  exactly the 3 affected translation units
  (`FrameDebuggerPanel.cpp`, `ImGuiEditorLayer.cpp`, `Application.cpp` — the
  latter two rebuilt only because they transitively include
  `FrameDebuggerPanel.h`/`EditorLayer.h`) plus the library link step, with
  zero errors/warnings introduced.
- `cmake --build build --target GreatTamanaEngineTests -j 4` — succeeded.
- Ran `tests\GreatTamanaEngineTests.exe --gtest_filter=*FrameDebugger*` — all
  **85 tests passed**, 0 failed — identical count/result to Phase 1's own
  verification, confirming this phase's comment-only/deferred-trigger
  changes introduced zero regressions in any existing Frame-Debugger-related
  Tier 1 test.
- `cmake --build build --target GreatTamanaEngine -j 4` — succeeded (full
  executable link), as an extra confidence check that `Application.cpp`'s
  transitive include of the touched header still links cleanly end-to-end.
- No full build, no full `ctest` regression run, and no live
  `run_app_background`/`gte_send_request` smoke test were performed this
  phase — per the workflow rules and the phase document's own Step 4, those
  are optional here and Phase 7's job to do for real.

## Definition of Done — status

- [x] Enabling the Frame Debugger no longer ever produces a capture with
      zero per-object rows for a scene that has objects — the capture is
      now deferred to the next `Build()` call, which always runs after a
      frame whose rendering had the capture context correctly armed for its
      entire duration (confirmed by code review; live proof is Phase 7's
      job).
- [x] `PHASE2_COMPLETION_REPORT.md` written (this file), code committed to
      git.

## Identifiers future phases (especially Phase 3) need to know about

- `bool FrameDebuggerPanel::m_pendingCaptureAfterEnable` (private member,
  `FrameDebuggerPanel.h`) — **Phase 3 renames this to
  `m_pendingCaptureTrigger`** and generalizes it into the two-bool
  pending/serviced handshake described in
  `PHASE3_UNIFIED_STEP_TIMELINE_AND_PER_DRAW_REPLAY_RENDERING.md`'s own
  Step 3.0. This phase's version is deliberately the simpler, single-bool,
  single-trigger-site precursor to that mechanism — exactly as the parent
  phase document anticipated.
- The consumption site — the very top of `BuildToolbarRow()` — is exactly
  where Phase 3's own widened mechanism expects to find (and replace) this
  logic; no other file/function was touched that Phase 3 would need to
  re-discover.

No genuine design ambiguity was hit during this phase — the phase document's
own concrete file/line/identifier references matched the real current source
exactly (post-Phase-1), so `ask_questions` was not needed.
