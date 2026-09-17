# PHASE0 — Master Strategy: `frame-debugger-7` campaign

_Orchestrator document. Every child phase file below MUST be read on its own,
but this file is the one place that explains WHY the whole campaign exists,
what is LOCKED (not up for re-litigation), and in what order things happen.
Mirrors the shape of `task_manager/frame-debugger-6/PHASE0_MASTER_STRATEGY.md`._

## Step 1: The Goal (where are we going?)

Fix two real, user-confirmed bugs in the Editor's "Frame Debugger" window
(`src/Editor/Panels/FrameDebuggerPanel.h/.cpp`, `src/Editor/FrameDebuggerData.h/.cpp`,
`src/Editor/FrameDebuggerCapture.h/.cpp`, `src/Editor/FrameDebuggerHistory.h/.cpp`):

1. **Bug 1 — wrong first capture.** Load a scene → open Frame Debugger → press
   "Enable" → the very first captured frame is missing objects (no terrain
   row at all). Pressing Enable a second time captures correctly.
2. **Bug 2 — wrong preview image per selected item.** Clicking an object in
   the event tree (e.g. "SmokeTestCube") does not show the screen as it
   looked right after THAT object was drawn — it always shows the same
   whole-frame image (with everything, including terrain and the atmosphere
   sky, already drawn), exactly like Unity's Frame Debugger does NOT behave.
   The user wants genuine Unity-style behavior: select object/step **K** →
   see the real screen exactly as it looked with only steps `1..K` applied.

Additionally, three explicit product decisions the user made while this
strategy was being designed (see `Q1..Q6` answers, now LOCKED — do not
re-ask, do not revert):

- **No multi-frame history.** Unity's Frame Debugger does not remember past
  frames either. Remove the existing 8-slot `FrameDebuggerHistory` ring
  buffer, its Prev/Next "Frame History" toolbar, and the `step_history` HTTP
  command entirely. Exactly ONE captured frame is ever held in memory at a
  time. Turning "Enable" off, or resuming playback while Enabled, must free
  that captured frame's data immediately.
- **No artificial per-object safety cap.** The replay/capture work described
  below only ever runs once, on an explicit user trigger (Enable/Step/
  Capture), never every real frame — so there is no need to limit it to
  scenes with few objects. Build the accumulated preview for every object,
  unconditionally.
- **Every leaf, compute pass or object draw alike, shows the accumulated
  Game View image as of that exact step** — not a pass-specific texture like
  before. This REPLACES the `frame-debugger-5` campaign's "show this compute
  pass's own distinct output texture" feature outright (an explicit,
  user-approved breaking change — the raw texture pixels themselves remain
  inspectable elsewhere in the Editor, e.g. the Render Graph panel /
  `GET /get_texture`, so no diagnostic capability is actually lost, it just
  moves out of the Frame Debugger's own big preview box).

## Step 2: The Situation (where are we now? — confirmed by reading the code)

### Bug 1's real root cause

`FrameDebuggerPanel::ApplyEnabledEdge()` (`Panels/FrameDebuggerPanel.cpp`)
calls `TriggerCapture()` **immediately** on the Enable checkbox's
false→true edge. But `Application::Run()` (`Application/Application.cpp`,
around line 527) already called
`m_editorLayer->PrepareFrameDebuggerCaptureContext()` **earlier this same
frame, before `Game::Render()` ran** — at that point `m_enabled` was still
`false` (the checkbox click is only processed later, inside
`ImGuiEditorLayer::BuildUI()`), so `PrepareCaptureContextForThisFrame()`
returned `nullptr` and this frame's `RenderSystem::Draw()` calls never
recorded any `FrameDebuggerDrawRecord`s at all. The capture built
moments later from that same frame's data is therefore real (the render
graph and its pass list are fine) but has **zero** per-entity draw records —
so `BuildRealFrameDebuggerSnapshot()`'s `"GameView"` node gets **no
children at all** (no terrain, no smoke cube). The SECOND Enable press
works because `m_enabled` was already `true` for the *entirety* of that
next frame before `Build()` ran. This is an existing, DOCUMENTED "one-frame
lag" (see `Panels/FrameDebuggerPanel.cpp`'s own comment on
`ApplyEnabledEdge()`), previously considered a minor caveat — now, because
`frame-debugger-6` made per-entity leaves a first-class tree feature, this
lag actually hides real content and must be fixed.

### Bug 2's real root cause

The real `"GameView"` render-graph pass (`Application/RenderPasses.cpp`'s
`AddGameViewPass()`) opens ONE dynamic-rendering bracket
(`renderer.BeginGraphPassRecording()` → `game.Render()` → loop of
`renderer.Submit()` calls for every entity → `renderer.EndGraphPassRecording()`)
and draws every object into the SAME shared color target, back to back, with
no opportunity today to copy an intermediate result. `FrameDebuggerHistory::
CaptureFrame()` only ever runs **after** the whole frame has finished
rendering, and only ever takes a snapshot of whatever a NAMED texture
currently holds — which works for compute passes (each writes its own,
never-later-overwritten LUT texture) but is structurally incapable of
recovering "what did the shared Game View texture look like after only the
first 2 of 5 objects were drawn" — that information is already gone,
overwritten by object 3/4/5, by the time `CaptureFrame()` ever runs.
**There is no bug in the retained-texture-copy code; the missing piece is a
way to actually produce N real intermediate images while the frame is
rendering**, then retain all of them.

### The technical opportunity that makes the fix low-risk

`RenderGraphBuilder::PassBuilder::WriteColorAttachment(handle, clearColor)`
(`src/Renderer/RenderGraph/RenderGraphBuilder.h`) already supports
`clearColor == std::nullopt`, meaning "LOAD, don't clear" — i.e. the render
graph ALREADY has first-class support for "keep drawing into the same
target across several separate passes". Combined with `AddPass()`'s
already-generic "declare a pass, give it setup+execute lambdas" shape, this
means the fix for Bug 2 does **not** require inventing any new rendering
primitive, breaking the real `"GameView"` pass's own bracket, or touching
`Renderer::Submit()`'s hot path at all — it only requires **adding N new,
debug-only, Frame-Debugger-exclusive Render Graph passes**, each one:
LOAD the shared scratch target → draw exactly one more object → copy the
current result into its own dedicated retained texture. This is entirely
additive, only ever added to the graph on an explicit capture-trigger frame,
and can never corrupt the real, always-on `"GameView"` pass no matter what
bugs exist in this new code.

## Step 3: The Plan (phase list)

| Phase | File | One-line summary |
|---|---|---|
| 1 | `PHASE1_REMOVE_HISTORY_AND_SINGLE_CAPTURE_LIFECYCLE.md` | Rip out the 8-slot ring buffer; exactly one captured frame, cleared on Disable/Resume. |
| 2 | `PHASE2_DEFERRED_CAPTURE_TRIGGER.md` | Fix Bug 1 — defer the very first auto-capture to the next properly-armed frame. |
| 3 | `PHASE3_UNIFIED_STEP_TIMELINE_AND_PER_DRAW_REPLAY_RENDERING.md` | **Heaviest/riskiest phase.** New debug-only replay Render Graph passes that produce one real accumulated image per object draw. |
| 4 | `PHASE4_PREVIEW_WIRING_AND_DATA_MODEL.md` | Wire the new images into `ChooseFrameDebuggerPreviewSource()`/the data model; retire the old per-compute-pass-texture mechanism. |
| 5 | `PHASE5_PANEL_UI_AND_HTTP_CLEANUP.md` | Finish removing dead UI/HTTP surface; polish empty/placeholder states. |
| 6 | `PHASE6_TESTS_DOCS_AND_FINAL_CLEANUP.md` | Bring Tier-1 tests and documentation (`docs/conventions/frame-debugger.md`, `AGENTS.md`) up to date. |
| 7 | `PHASE7_LIVE_VERIFICATION_FULL_BUILD_AND_CAMPAIGN_COMPLETION.md` | Full build + live HTTP-driven proof both bugs are fixed + campaign report. |

Phases 1→2 are small and low-risk and unlock correct testing ground for
Phase 3. Phase 3 is the one genuine rendering-pipeline change and is
explicitly called out for an EXTRA double-check pass before the wider
campaign double-check happens (see the orchestration notes below). Phases
4-6 wire everything together and clean up. Phase 7 is the only phase allowed
to do a full build/regression run.

## Step 4: Locked Design Decisions (do not re-litigate)

1. **No multi-frame history** — one current capture only (Step 1 above).
2. **No per-object safety cap** — build every accumulated image unconditionally.
3. **Unified accumulated-preview semantics for every leaf** (compute pass OR
   object draw) — replaces `frame-debugger-5`'s per-compute-pass distinct
   texture preview outright.
4. **The real, always-on `"GameView"` pass is never modified.** All new
   replay work happens in brand-new, additive, debug-only Render Graph
   passes, added to the graph ONLY on an explicit capture-trigger frame.
5. **The very first capture after "Enable" must contain real per-object
   data** — achieved by deferring that first capture to the next frame
   whose rendering already had the recorder armed beforehand, never by
   trying to salvage the already-stale frame.
6. **A pre-`"GameView"` compute step's own accumulated image is honestly
   "nothing drawn to the screen yet"** — a real, correct state, not a bug;
   render it as a clear placeholder rather than fabricating an image.
7. **This campaign's rendering changes only ever run while the Frame
   Debugger window is open AND "Enable" is checked, and only on the exact
   frame a capture trigger fires** — zero cost to ordinary gameplay/edit-mode
   rendering, matching this codebase's existing "zero overhead when
   disarmed" rule (`AGENTS.md`, "Frame Debugger" section).

## Step 5: Workflow rules for every phase (repeat of the standing project rules)

- Read `README.md` and `AGENTS.md` first, every phase.
- Stay on branch `feature/frame-debugger-impl`.
- No full build/regression test until Phase 7 (incremental compile checks
  and `run_app_background` + `gte_send_request` visual spot-checks are
  encouraged in the meantime, per `AGENTS.md`'s own Tier 2 guidance).
- Every phase ends with a `PHASEn_COMPLETION_REPORT.md` in this same folder
  and a git commit of the phase's code + its own report.
- **Every implementation delegate_task prompt (and any further task IT
  delegates) must be told to use `ask_questions` whenever it hits a genuine
  design ambiguity** — never guess silently on something only a human (or
  this master strategy) can decide.
