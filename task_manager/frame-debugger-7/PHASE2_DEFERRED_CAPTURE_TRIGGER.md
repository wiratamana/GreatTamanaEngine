# PHASE2 — Fix Bug 1: the wrong first capture (deferred capture trigger)

_Parent: `PHASE0_MASTER_STRATEGY.md` — read it first. Builds on Phase 1's
renamed `FrameDebuggerCurrentCapture`/`m_currentCapture`._

## Step 1: The Goal

The FIRST captured frame after pressing "Enable" must always contain real,
complete per-object data (no missing terrain/smoke-cube rows). Between the
click and that first real capture being ready, the tree should honestly show
"No frame captured yet." — never a wrong/incomplete one. This matches the
user's own words: "start with nothing like unity frame debugger, empty UI;
when user enable, this will mark as first frame."

## Step 2: The Situation (confirmed root cause — see `PHASE0` Step 2)

`Application::Run()` (`src/Application/Application.cpp`, ~line 527) calls
`m_editorLayer->PrepareFrameDebuggerCaptureContext()` **before**
`Game::Render()` runs, every frame. `FrameDebuggerPanel::
PrepareCaptureContextForThisFrame()` arms (`Reset()`s and returns) the real
capture context only when `ctx.frameDebuggerWindowOpen && m_enabled` are
BOTH already true **at that point in the frame**. The Enable checkbox is
only actually clicked later, inside `ImGuiEditorLayer::BuildUI()` →
`FrameDebuggerPanel::Build()` → `BuildToolbarRow()` → `ApplyEnabledEdge()` —
by which point this frame's `Game::Render()` (and therefore every
`RenderSystem::Draw()` call) has ALREADY happened, using a `nullptr`
capture pointer. `ApplyEnabledEdge()` then calls `TriggerCapture()`
immediately anyway, building a snapshot from this same (already-stale)
frame's data — hence zero recorded `FrameDebuggerDrawRecord`s, hence no
per-entity children under `"GameView"`.

## Step 3: The Plan

### 3.1 — Add a pending-capture flag

In `Panels/FrameDebuggerPanel.h`, add:

```cpp
// PHASE2 (frame-debugger-7 campaign) - true for exactly one Build() call
// after the Enable checkbox's false->true edge fires, consumed at the START
// of the NEXT Build() call whose own frame was actually rendered with the
// capture context armed (see ApplyEnabledEdge()'s own updated doc comment).
bool m_pendingCaptureAfterEnable = false;
```

### 3.2 — `ApplyEnabledEdge()` no longer captures immediately

In `Panels/FrameDebuggerPanel.cpp`, on the `false -> true` branch:

- Keep `ctx.playbackPaused = true;`.
- **Remove** the direct `TriggerCapture();` call.
- Set `m_pendingCaptureAfterEnable = true;` instead.
- Update this method's own doc comment to explain the new, CORRECT
  semantics precisely (replace the old "may be empty, that's an accepted
  one-frame lag" comment — that lag is exactly what this phase removes):
  arming (`PrepareCaptureContextForThisFrame()`) happens next frame, before
  that frame's `Game::Render()` runs, with `m_enabled` already `true` for
  the whole frame — so by the time `Build()` runs later that SAME frame and
  consumes `m_pendingCaptureAfterEnable`, the just-finished render already
  recorded real per-object facts.

### 3.3 — Consume the pending flag at the right point

At the very top of `BuildToolbarRow()` (before drawing the checkbox, so the
UI this frame already reflects the freshly-captured tree) OR at the top of
`Build()` itself (implementer's choice — either works since both run once
per frame, after this frame's render already happened): if
`m_pendingCaptureAfterEnable` is true, clear it and call `TriggerCapture()`
right there, BEFORE the rest of the panel reads `m_currentCapture.CurrentEntry()`
for display this same frame. This guarantees the tree the user sees on the
very frame the capture "arrives" is already correct — no extra visible
delay beyond the one, real, imperceptible frame needed for the recorder to
actually have run once while armed.

### 3.4 — Step / Capture button: verify, do not needlessly re-plumb

Read `TriggerCapture()`'s existing callers: the "Capture" button and the
Step-consumption branch (`m_stepCaptureRequested`) inside
`BuildToolbarRow()`. Both of these ALREADY only ever fire on a frame where
`m_enabled` has already been `true` for the entire frame (Step only enables
while already Enabled; the Capture button is disabled unless already
Enabled) — so `Application::Run()`'s earlier
`PrepareFrameDebuggerCaptureContext()` call this same frame already armed
correctly before `Game::Render()` ran. **Do not change these two paths** —
write a short paragraph in `PHASE2_COMPLETION_REPORT.md` explaining exactly
why they were already correct (cite the code), so nobody "fixes" something
that was never broken.

### 3.5 — HTTP path gets the fix for free

`FrameDebuggerPanel::SetEnabledFromCommand()` calls the same
`ApplyEnabledEdge()` — no separate change needed; note this explicitly in
the completion report as a confirmed side-effect, and double check there is
no OTHER HTTP entry point that duplicates the old immediate-capture
behavior.

### 3.6 — UI empty state while pending

Confirm (by reading `BuildEventTreePane()`) that while
`m_pendingCaptureAfterEnable` is true and no capture has landed yet, the
tree correctly falls back to `"No frame captured yet."` — this should
already just work once `TriggerCapture()` is no longer called synchronously
inside the click handler, since `m_currentCapture.CurrentEntry()` stays
`nullptr` until the deferred call actually runs. Add a one-line comment at
the relevant fallback branch pointing at this phase for future readers.

## Step 4: Testing/verification (no full build yet)

- Quick compile check only (this phase's code is small).
- If feasible, extract the pure "should this Build() call consume a pending
  trigger" decision into a tiny testable helper (mirrors this codebase's own
  "extract a pure function" convention, `AGENTS.md`) — e.g. a free function
  taking `(bool pending, bool enabledThisFrame) -> bool shouldCaptureNow`.
  If the logic is trivial enough that extracting it would be pure ceremony,
  it's fine to leave it inline — use judgement, and say which you chose (and
  why) in the completion report.
- Full, live, HTTP-driven proof (open → enable → confirm empty →
  confirm next state has terrain/smoke cube) is Phase 7's job, not this
  phase's — do not attempt a live app run here unless you want a *quick*
  sanity spot-check (optional, allowed by `AGENTS.md`'s Tier 2 guidance, but
  not required to close this phase).

## Step 5: Definition of Done

- Enabling the Frame Debugger no longer ever produces a capture with zero
  per-object rows for a scene that has objects.
- `PHASE2_COMPLETION_REPORT.md` written, code committed.

Use `ask_questions` for any genuine ambiguity you hit that this document
doesn't already resolve — and require the same of any further delegation
you create.
