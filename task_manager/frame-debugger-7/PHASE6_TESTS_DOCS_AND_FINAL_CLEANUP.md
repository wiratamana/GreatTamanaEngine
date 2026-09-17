# PHASE6 — Tests and documentation catch-up

_Parent: `PHASE0_MASTER_STRATEGY.md` — read it first. Builds on Phases 1-5._

## Step 1: The Goal

Bring automated test coverage and documentation fully up to date with
everything this campaign changed, per `AGENTS.md`'s own mandatory rule:
"Every change to Tier 1 code must come with a matching test change."

## Step 2: The Situation

Phases 1-5 already added/updated tests alongside their own code changes (per
the same rule, applied incrementally). This phase is the CAMPAIGN-WIDE
audit + the documentation rewrite, which is easiest to do once, holistically,
after the dust settles, rather than piecemeal per phase.

## Step 3: The Plan

### 3.1 — Test audit

Re-open every test file touched or implied by Phases 1-5 and confirm:

- `tests/Editor/FrameDebuggerDataTests.cpp` — covers the new
  `FrameDebuggerStepPreviewKind`-based `ChooseFrameDebuggerPreviewSource()`
  fully (all four enum values, plus the "entry doesn't exist at all" case).
- `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` — covers
  `stepPreviewKind`/`stepPreviewIndex` assignment for every node shape
  (Pre-GameView leaf, `"GameView"` leaf, each per-object child, Post-GameView
  before/at-or-after the composite pass).
- `tests/Editor/FrameDebuggerHistoryTests.cpp` (or its renamed counterpart,
  matching Phase 1's `FrameDebuggerCurrentCapture` rename) — covers the
  single-slot `HasCapture()`/`Clear()` state machine.
- `tests/Editor/FrameDebuggerCaptureTests.cpp` — covers any new pure logic
  Phase 3 added to `FrameDebuggerCaptureContext` (e.g. if `maxDrawCount`
  clipping logic in `RenderSystem::Draw()` was extracted into a small pure
  helper — check whether Phase 3 did this; if the clipping stayed as a
  simple inline loop `break`, note plainly in this phase's report why no
  new Tier-1 test was needed for it, per `AGENTS.md`'s own "if it can be
  extracted as a small pure function... do that" guidance vs. "sometimes it
  genuinely can't be cleanly extracted").
- `tests/Application/FrameDebuggerCommandBridgeTests.cpp` /
  `tests/Network/NetworkRoutesTests.cpp` — no leftover `step_history`
  assertions; the `/state` response shape matches Phase 1/5's changes.
- Confirm `tests/CMakeLists.txt` still lists every surviving test file (and
  no longer lists any deleted one).

### 3.2 — `docs/conventions/frame-debugger.md` rewrite

This is the single most important documentation file for this feature. Add
a new, clearly-headed section, **"What's new (`frame-debugger-7`
campaign)"**, immediately after the existing `"What's new (frame-debugger-6
campaign)"` section, following the exact same writing style/level of detail
already used by every prior campaign's own section in this file. It must
cover, explicitly and plainly:

1. **The Bug 1 fix** — the deferred capture-trigger mechanism (Phase 2/3.0),
   explaining the root cause (arm-before-render vs. click-detected-after-
   render timing) and the fix (defer to the next properly-armed frame).
2. **The removal of the multi-frame history ring buffer** (Phase 1) — an
   EXPLICIT, LOCKED, user-approved BREAKING CHANGE relative to
   `frame-debugger-3`/`4` (which introduced and grew it) — exactly ONE
   captured frame is now ever retained, cleared on Disable/Resume.
3. **The removal of the per-compute-pass distinct-texture preview** (Phase
   4) — an EXPLICIT, LOCKED, user-approved BREAKING CHANGE relative to
   `frame-debugger-5` (which introduced it) — replaced by the new unified
   "accumulated Game View as of this exact step" preview for every leaf.
   Note plainly that raw per-pass texture pixel inspection is still possible
   elsewhere (Render Graph panel / `GET /get_texture`) — no diagnostic
   capability was actually lost, it only moved out of this window's own big
   preview box.
4. **The new per-object replay-rendering mechanism** (Phase 3) — describe
   the "N self-contained, debug-only Render Graph passes, each redrawing
   objects `[0..i]` from scratch" design, why it was chosen over a
   shared-target-plus-mid-pass-copy scheme, and its accepted O(N²) draw-call
   cost (only ever paid once per explicit capture trigger).
5. Update the tree-shape diagram (the one in the existing "What is real
   today" section) to show that EVERY leaf — compute or per-object — now has
   a real, always-available preview image (removing any language that still
   implies compute leaves show "their own special texture").
6. Update "Still-deferred future work" if anything there is now resolved or
   newly relevant (e.g. the shared-target O(N) optimization mentioned in
   Phase 3 as a possible future follow-up).

### 3.3 — `AGENTS.md` summary paragraph

Update the "Frame Debugger" section's short summary paragraph (the one
linking out to `docs/conventions/frame-debugger.md`) to reflect the new
reality in 2-3 sentences, matching this file's own existing terse style for
every other subsystem summary.

### 3.4 — `TODO.md`

Grep `TODO.md`'s "Frame Debugger" section — if it references the now-removed
history ring buffer or per-compute-pass-texture mechanism as an assumption
for future work, correct it.

## Step 4: Definition of Done

- Every touched Tier-1 file has matching, passing tests (verified via a
  fast, targeted test-binary compile+run — full `ctest` regression is still
  Phase 7's job, not this phase's).
- `docs/conventions/frame-debugger.md`, `AGENTS.md`, and `TODO.md` (if
  applicable) accurately describe the system exactly as it exists after
  Phase 5.
- `PHASE6_COMPLETION_REPORT.md` written, code + docs committed together.

Use `ask_questions` for any genuine ambiguity — and require the same from
any further delegation.
