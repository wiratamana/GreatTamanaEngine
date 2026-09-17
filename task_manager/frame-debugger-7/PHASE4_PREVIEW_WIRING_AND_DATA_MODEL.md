# PHASE4 — Wire the new per-step images into the preview/data model

_Parent: `PHASE0_MASTER_STRATEGY.md` — read it first. Builds directly on
Phase 3's `FrameDebuggerCaptureContext::ReplayStepPreviews()`._

## Step 1: The Goal

Make selecting ANY leaf in the event tree — a per-object draw OR a compute
pass — show the real, correct "accumulated Game View as of this exact step"
image, replacing both: (a) the old "always show the whole final frame"
behavior (Bug 2), and (b) the old `frame-debugger-5` per-compute-pass
distinct-texture preview (superseded per `PHASE0`'s Locked Design Decision
#3 — the user explicitly chose this over keeping the old behavior).

## Step 2: The Situation

Three real "bucket" images exist (or can be trivially derived) for a
compute-pass leaf's position in the timeline, PLUS Phase 3's N per-object
images for `"GameView"`'s own draw-record children:

1. **Before `"GameView"` has drawn anything** (a `"Compute Dispatches
   (Pre-GameView)"` leaf) — honestly "nothing on screen yet". Do not
   fabricate an image (Locked Design Decision #6, `PHASE0`) — represent this
   as a distinct `None`-like choice the panel renders as a clear placeholder
   string, e.g. `"Nothing drawn yet at this point in the frame."`.
2. **After every object has been drawn, before the atmosphere composite
   pass runs** (`"GameView"` leaf itself, OR a `"Compute Dispatches
   (Post-GameView)"` leaf that runs BEFORE the composite pass) — this is
   already exactly `entry.preview` (unchanged field/meaning) — and is also
   now pixel-identical to `ReplayStepPreviews().back()` (Phase 3's last
   step) by construction (see Phase 3's own sky-background ordering note) —
   pick whichever the implementer finds cleaner to keep as the single
   source of truth (recommendation: keep using `entry.preview` for this
   bucket, since it is cheaper — one plain post-hoc copy — and leave
   `ReplayStepPreviews().back()` as "just another per-object leaf", do not
   force artificial de-duplication logic between the two).
3. **At/after the atmosphere composite pass** (a `"Compute Dispatches
   (Post-GameView)"` leaf at/after the composite pass, or nothing selected)
   — this is already exactly `entry.compositedPreview` (unchanged).

## Step 3: The Plan

### 3.1 — Give every event node its own "which bucket" fact at snapshot-build time

`src/Editor/FrameDebuggerData.h/.cpp`: add a new enum, computed ONCE inside
`BuildRealFrameDebuggerSnapshot()` (which already walks
`graphSnapshot.passesInExecutionOrder` to build the Pre/Post-GameView groups
— reuse that same execution-order comparison, do not re-derive it a second
way):

```cpp
// frame-debugger-7 campaign, PHASE4 - which real, retained image should
// represent "the Game View as of this exact step" for one event node.
enum class FrameDebuggerStepPreviewKind {
    NotYetDrawn,   // A step before "GameView" has drawn anything at all.
    PerObjectStep, // One of "GameView"'s own per-entity draw children -
                   // `stepPreviewIndex` is which ReplayStepPreviews() entry.
    PreComposite,  // "GameView" itself, or any Post-GameView compute leaf
                   // that runs before the composite pass.
    PostComposite, // The composite pass itself, any leaf after it, or
                   // nothing selected at all.
};
```

Add `FrameDebuggerStepPreviewKind stepPreviewKind` and
`int stepPreviewIndex` (only meaningful for `PerObjectStep`) to
`FrameDebuggerEventDetails` (append at the END of the struct, per this
file's own existing "never insert a field in the middle" precedent — see
`FrameDebuggerEventDetails::eventLabel`'s own comment for why). Populate
both wherever `BuildRealFrameDebuggerSnapshot()` currently assembles each
leaf's `FrameDebuggerEventDetails` (the Pre-GameView loop, the `"GameView"`
leaf itself, each `BuildGameViewDrawRecordLeaf()`-built child, and the
Post-GameView loop — you need the composite pass's own real execution-order
index to split the Post-GameView loop into "before" vs "at/after" — find it
generically, e.g. the first surviving Post-GameView compute pass whose write
touches the SAME texture name `entry.compositedPreview`'s own source pass
does today, OR simpler: this campaign already knows exactly one pass
produces `compositedPreview`
(`"AtmosphereAerialPerspectiveCompositePass"`-shaped) — confirm by reading
`FrameDebuggerHistory::CaptureFrame()`'s own `compositedGameViewSource`
parameter's real call-site origin in `ImGuiEditorLayer.cpp`/`Application.cpp`
before hardcoding anything; prefer a structural check (e.g. "the pass whose
own write feeds the SAME texture handle as `m_gameViewComposited`") over a
literal name string if at all findable without excessive churn — if not
cleanly findable structurally within this phase's budget, a single,
clearly-commented literal pass-name check is an acceptable, honest fallback
(this campaign already tolerates one such literal-name special case
elsewhere historically, e.g. `frame-debugger-4`'s original PHASE2, later
generalized by `frame-debugger-5` — note in your completion report if you
had to take this shortcut, so a future campaign knows to generalize it the
same way).

### 3.2 — Rewrite `ChooseFrameDebuggerPreviewSource()`

Replace the old boolean-soup signature
(`hasEntry/hasPreview/hasCompositedPreview/isViewingGameViewLeaf/hasSelectedComputePassPreview`)
with one that takes the new `FrameDebuggerStepPreviewKind` (plus whatever
plain booleans are still needed for "does the relevant retained image
actually exist", mirroring this function's own existing "always take
already-resolved plain values, never a live RenderTexture/history object"
philosophy — `AGENTS.md`, "Testability & Regression Safety"). Update the
`FrameDebuggerPreviewSourceChoice` enum: replace `ComputePassPreview` with
`PerObjectStepPreview` (carrying the index the caller must use to index into
the CAPTURED entry's own `perObjectStepPreviews` — see below for exactly
where that field lives and how it gets there).

**IMPORTANT, re-confirmed against the REAL current source this revision (do
not re-derive, this is fully resolved) — `FrameDebuggerHistory::CaptureFrame()`
does NOT take a `FrameDebuggerCaptureContext&`/`capture` parameter AT ALL
today**, and therefore does NOT "already read capture's other fields" the way
an earlier draft of this document assumed. Its real, current signature
(`src/Editor/FrameDebuggerHistory.h`) is:

```cpp
void CaptureFrame(Renderer& renderer, const rg::RenderGraph& renderGraph, const FrameDebuggerSnapshot& snapshot,
    RenderTexture& gameViewSource, RenderTexture* compositedGameViewSource);
```

— it only ever receives the ALREADY-BUILT `snapshot` (which
`FrameDebuggerPanel::TriggerCapture()` builds moments earlier by calling
`BuildRealFrameDebuggerSnapshot(graphSnapshot, m_captureContext, renderTargetInfo)`
— a SEPARATE, pure function that itself takes `capture` as a parameter, but
whose OWN return value, `snapshot`, carries no `RenderTexture`s at all, only
plain display strings/enums). So Phase 3's own `capture.ReplayStepPreviews()`
vector is never reachable from inside `CaptureFrame()`'s current body — this
phase MUST widen that method's signature, concretely, as follows:

1. Add a new, LAST parameter to `FrameDebuggerHistory::CaptureFrame()`:
   `FrameDebuggerCaptureContext& capture` (non-const — see point 3 below for
   why). Update the doc comment accordingly.
2. Update the one real call site, `FrameDebuggerPanel::TriggerCapture()`
   (`Panels/FrameDebuggerPanel.cpp`), from:
   ```cpp
   m_history.CaptureFrame(*m_frameRenderer, *m_frameRenderGraph, snapshot, *m_frameGameView, m_frameGameViewComposited);
   ```
   to:
   ```cpp
   m_history.CaptureFrame(*m_frameRenderer, *m_frameRenderGraph, snapshot, *m_frameGameView, m_frameGameViewComposited, m_captureContext);
   ```
3. Inside `CaptureFrame()`'s body, at the point the entry's other fields are
   populated, add: `entry.perObjectStepPreviews = std::move(capture.ReplayStepPreviews());`
   — this requires Phase 3's own `FrameDebuggerCaptureContext::ReplayStepPreviews()`
   accessor (which Phase 3 left underspecified as just "an accessor", with no
   stated constness) to be a NON-const accessor returning
   `std::vector<RenderTexture>&` (mirroring `DrawRecords()`'s sibling
   `SetReplayStepPreviews()` mutator precedent) — nothing else in this
   codebase needs a const/read-only view of this particular vector, so widen
   it to non-const in this phase rather than adding a second, parallel
   `TakeReplayStepPreviews()` method. Note this small Phase-3-accessor
   constness fixup explicitly in this phase's own completion report, since
   Phase 3's own document did not pin it down.
4. `FrameDebuggerCaptureContext::Reset()` already `.clear()`s
   `m_replayStepPreviews` every armed frame (Phase 3) — after the move above,
   the source vector is left in a valid-but-unspecified (likely empty) moved-
   from state regardless, so no additional cleanup is needed here.

Add `None`/`NotYetDrawn` as its own explicit choice (mapped to
`FrameDebuggerStepPreviewKind::NotYetDrawn`) so the panel can render the
"nothing drawn yet" placeholder honestly, distinctly from the true empty
"no capture at all" state.

### 3.3 — Remove the now-obsolete per-compute-pass-texture mechanism

Per `PHASE0`'s Locked Design Decision #3 (explicit, user-approved
breaking change): delete
`FrameDebuggerComputePassPreview`/`FrameDebuggerHistoryEntry::computePassPreviews`/
`CollectComputePassTextureWrites()`/`CollectComputePassVolumeTextureWrites()`/
`FrameDebuggerComputePassTextureWrite`/`FrameDebuggerComputePassVolumeTextureWrite`,
and every bit of `FrameDebuggerHistory::CaptureFrame()`'s HDR/volume-texture
ray-march capture code that only existed to fill that vector (the
`HdrComputePassCopySource`/`ComputePassCopySource` structs, the whole
compute-write discovery + copy loop, the `m_volumePreviewRenderer` member
IF nothing else in this class still needs it — check whether
`VolumeTexturePreviewRenderer` is used anywhere else in this file before
removing the member itself).

**Before deleting anything, `search_in_dir` across `src/` and `tests/` for
every one of these identifiers** to confirm no other consumer depends on
them (the Render Graph panel / `GET /get_texture` have their OWN, separate,
still-needed way to inspect a raw named texture — `RenderGraphDebugTextureRegistry`/
`DebugTextureSnapshotFor()` — do NOT remove those; only remove the
Frame-Debugger-specific consumption of them added by `frame-debugger-5`).

### 3.4 — `Panels/FrameDebuggerPanel.cpp` wiring

Update `EnsurePreviewDescriptor()` to build the new, smaller set of inputs
`ChooseFrameDebuggerPreviewSource()` now needs (reading
`FindEventDetailsByIndex()`'s result's new `stepPreviewKind`/
`stepPreviewIndex` fields directly, no more per-name linear search through
a `computePassPreviews` vector) and to render the `NotYetDrawn` choice as a
placeholder string (mirrors the existing "No Texture" placeholder branch —
add a second, distinct message right next to it).

### 3.5 — Tests

- `tests/Editor/FrameDebuggerDataTests.cpp` — rewrite every
  `ChooseFrameDebuggerPreviewSource()` test case for the new signature/enum;
  cover all four `FrameDebuggerStepPreviewKind` values.
- `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` — add cases proving
  `BuildRealFrameDebuggerSnapshot()` assigns the right `stepPreviewKind`/
  `stepPreviewIndex` to a Pre-GameView leaf, a `"GameView"` leaf, each
  per-object child (in order), a Post-GameView-before-composite leaf, and
  a Post-GameView-at/after-composite leaf.
- Delete every test that only existed to cover the removed
  `CollectComputePassTextureWrites()`/`CollectComputePassVolumeTextureWrites()`
  functions.

## Step 4: Definition of Done

- Clicking any object draw leaf shows a real image with exactly that object
  (and everything drawn before it) visible, no atmosphere fog, no later
  objects.
- Clicking a Pre-GameView compute leaf shows the honest "nothing drawn yet"
  placeholder.
- Clicking a Post-GameView leaf at/after the composite pass (or nothing
  selected) shows the true final, fog-inclusive image.
- The old per-compute-pass distinct-texture preview code no longer exists
  anywhere in this codebase.
- Quick compile check only. `PHASE4_COMPLETION_REPORT.md` written, code
  committed.

Use `ask_questions` for any genuine ambiguity (especially the "how do I
structurally find the composite pass's own execution-order index" question
in Step 3.1) — and require the same from any further delegation.
