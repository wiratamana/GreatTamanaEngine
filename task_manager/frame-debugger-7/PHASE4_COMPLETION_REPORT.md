# PHASE4 — Completion Report

_Parent: `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE4_PREVIEW_WIRING_AND_DATA_MODEL.md`. Builds on Phase 1
(`FrameDebuggerCurrentCapture`), Phase 2 (deferred capture trigger), and
Phase 3 (`AddFrameDebuggerReplayPasses()` /
`FrameDebuggerCaptureContext::ReplayStepPreviews()`)._

## Summary

Selecting ANY leaf in the event tree — a per-object draw or a compute pass
alike — now shows the real, correct "accumulated Game View as of this exact
step" image, fixing Bug 2. The frame-debugger-5 campaign's old per-compute-
pass-distinct-texture preview mechanism (`FrameDebuggerComputePassPreview`,
`FrameDebuggerHistoryEntry::computePassPreviews`,
`CollectComputePassTextureWrites()`/`CollectComputePassVolumeTextureWrites()`,
the whole HDR/volume-texture ray-march capture code in
`FrameDebuggerCurrentCapture::CaptureFrame()`, and the owned
`VolumeTexturePreviewRenderer m_volumePreviewRenderer` member) is removed
entirely, per `PHASE0`'s Locked Design Decision #3 — an explicit,
user-approved breaking change. Every leaf now picks its preview from exactly
three real, retained images: `FrameDebuggerHistoryEntry::preview`,
`::compositedPreview` (both pre-existing), and the new
`::perObjectStepPreviews` (moved here from Phase 3's transient
`FrameDebuggerCaptureContext::ReplayStepPreviews()`).

## New / renamed identifiers future phases need to know about

### `FrameDebuggerStepPreviewKind` (new enum, `src/Editor/FrameDebuggerData.h`)

```cpp
enum class FrameDebuggerStepPreviewKind {
    NotYetDrawn,   // A Pre-GameView compute-dispatch leaf - honestly "nothing drawn yet".
    PerObjectStep, // One of "GameView"'s own per-entity draw children - stepPreviewIndex selects which.
    PreComposite,  // "GameView" itself, or a Post-GameView leaf before the composite pass.
    PostComposite, // The composite pass itself, anything after it, or nothing selected.
};
```

Computed ONCE per event node, inside `BuildRealFrameDebuggerSnapshot()`
(`FrameDebuggerData.cpp`), reusing the SAME execution-order comparison that
function already derives for the Pre/Post-GameView group split (never
re-derived a second way).

### `FrameDebuggerEventDetails` new fields (`FrameDebuggerData.h`)

Appended at the END of the struct (matching its own existing "never insert
in the middle" precedent):

```cpp
FrameDebuggerStepPreviewKind stepPreviewKind = FrameDebuggerStepPreviewKind::PostComposite;
int stepPreviewIndex = -1; // only meaningful when stepPreviewKind == PerObjectStep.
```

- `BuildComputeDispatchLeaf()` gained a new, LAST parameter,
  `FrameDebuggerStepPreviewKind stepPreviewKind`, and now sets
  `details.stepPreviewKind` from it (`stepPreviewIndex` stays at its default,
  `-1`, for every compute-dispatch leaf).
- `BuildGameViewLeaf()` now unconditionally sets `details.stepPreviewKind =
  PreComposite`.
- `BuildGameViewDrawRecordLeaf()` gained a new, LAST parameter,
  `int stepPreviewIndex` (the record's own 0-based position among
  `capture.DrawRecords()`), and sets `details.stepPreviewKind =
  PerObjectStep` / `details.stepPreviewIndex = stepPreviewIndex`.
- `BuildRealFrameDebuggerSnapshot()`'s own three loops were updated:
  - The Pre-GameView loop passes `NotYetDrawn` unconditionally.
  - The `GameView` draw-record loop now tracks a local `int
    perObjectStepIndex` counter (0-based, incremented once per record, in
    the SAME order as `capture.DrawRecords()`) and passes it through.
  - The Post-GameView loop computes a new
    `FindPostGameViewCompositePassExecutionIndex()` value ONCE before the
    loop, then picks `PreComposite` for any surviving leaf strictly BEFORE
    that index, `PostComposite` for the composite pass's own leaf, anything
    strictly after it, or when no composite pass survived this capture at
    all (index `-1`).

### `FindPostGameViewCompositePassExecutionIndex()` (new, anonymous-namespace, `FrameDebuggerData.cpp`)

Finds the real execution-order index, among the surviving Post-GameView
compute passes, of the pass whose own `writeNames` contains the literal
string `"GameViewComposited"` — **this is the structural approach the
phase document asked for**: it is a check against each pass's own real
render-graph write-RESOURCE-NAME fact (`pass.writeNames`), not a check
against the composite pass's own NAME
(`"AtmosphereAerialPerspectiveCompositePass"`). I did **not** find a way to
derive this purely from `RenderGraphPassSnapshot`'s own execution-order
index metadata alone (e.g. there is no field on
`FrameDebuggerCurrentCapture::CaptureFrame()`'s own `compositedGameViewSource`
parameter that carries a texture NAME string back to
`BuildRealFrameDebuggerSnapshot()` — that function only ever sees the
`rg::RenderGraphSnapshot`, never a live `RenderTexture`), so the literal
string `"GameViewComposited"` IS present in this new function — but as a
match target for `pass.writeNames`, not for `pass.name`. This is the exact
"prefer a structural check... over a literal pass-name string" middle
ground the phase document's own Step 3.1 explicitly pre-approved, and it is
the SAME literal string already threaded through this whole feature's
prose (`FrameDebuggerHistory.h`'s own pre-existing doc comments already say
`"GameViewComposited"` is what `compositedGameViewSource` is created under
in `Application.cpp`) — cross-checked directly against
`AtmosphereLutRenderer::AddAerialPerspectiveCompositePass()`'s own
`outputTextureName` call-site argument at implementation time. Returns `-1`
if no surviving Post-GameView pass writes that texture this capture (e.g. a
capture taken before the composite pass has ever run this session) — every
Post-GameView leaf then defaults to `PostComposite` (Step 2's own "or
nothing selected" catch-all bucket).

### `FrameDebuggerPreviewSourceChoice` (rewritten, `FrameDebuggerData.h`/`.cpp`)

```cpp
enum class FrameDebuggerPreviewSourceChoice {
    None,
    NotYetDrawn,           // NEW - honest "nothing drawn yet" placeholder.
    Preview,
    CompositedPreview,
    PerObjectStepPreview,  // NEW - REPLACES the old ComputePassPreview.
};
```

`ChooseFrameDebuggerPreviewSource()`'s signature is now:

```cpp
FrameDebuggerPreviewSourceChoice ChooseFrameDebuggerPreviewSource(bool hasEntry,
    FrameDebuggerStepPreviewKind stepPreviewKind, bool hasPreview, bool hasCompositedPreview,
    bool hasPerObjectStepPreviewAtIndex);
```

The old `isViewingGameViewLeaf`/`hasSelectedComputePassPreview` booleans are
gone. Rule: `NotYetDrawn` always wins outright (never fabricates an image,
even if `hasPreview`/`hasCompositedPreview` happen to be true);
`PerObjectStep` returns `PerObjectStepPreview` if
`hasPerObjectStepPreviewAtIndex`, else `None` (deliberately NEVER falls back
to `compositedPreview`/`preview` — this is what makes Bug 2's fix honest:
no atmosphere fog, no later objects, ever, for this bucket); `PreComposite`
always shows `Preview` (never `CompositedPreview`, even if present —
UNCHANGED semantics from before this phase, Locked Design Decision #5, just
reached via the new enum instead of a separate `isViewingGameViewLeaf`
bool); `PostComposite` prefers `CompositedPreview`, falling back to
`Preview` only when `CompositedPreview` is absent (UNCHANGED).

### `FrameDebuggerHistoryEntry::perObjectStepPreviews` (new field, `FrameDebuggerHistory.h`)

```cpp
std::vector<RenderTexture> perObjectStepPreviews;
```

REPLACES the deleted `computePassPreviews` field. One real, retained GPU
`RenderTexture` per real object drawn this capture's `"GameView"` pass, in
the SAME order as `FrameDebuggerCaptureContext::DrawRecords()`. Entirely
REBUILT from scratch on every real capture (a plain `std::move()`
assignment overwrites whatever it held before).

### Widened `FrameDebuggerCurrentCapture::CaptureFrame()` signature (Step 3.2 corrected guidance, followed exactly)

Per this phase document's own corrected Step 3.2 (which explicitly says the
earlier draft's assumption — that `CaptureFrame()` already took/read a
`FrameDebuggerCaptureContext&` — was wrong, and spells out exactly how to
widen it), the real, current signature was changed from:

```cpp
void CaptureFrame(Renderer& renderer, const rg::RenderGraph& renderGraph, const FrameDebuggerSnapshot& snapshot,
    RenderTexture& gameViewSource, RenderTexture* compositedGameViewSource);
```

to:

```cpp
void CaptureFrame(Renderer& renderer, const rg::RenderGraph& renderGraph, const FrameDebuggerSnapshot& snapshot,
    RenderTexture& gameViewSource, RenderTexture* compositedGameViewSource, FrameDebuggerCaptureContext& capture);
```

— a new, LAST, non-const `capture` parameter, exactly as instructed. The one
real call site, `FrameDebuggerPanel::TriggerCapture()`
(`Panels/FrameDebuggerPanel.cpp`), was updated to pass `m_captureContext` as
that new last argument. Inside `CaptureFrame()`'s body:
`entry.perObjectStepPreviews = std::move(capture.ReplayStepPreviews());` —
placed BEFORE the `ImmediateSubmit()` call (order doesn't matter for
correctness here since nothing else touches `perObjectStepPreviews` in this
method, but it's placed alongside the other `entry.*` field population for
readability).

**Note on `renderGraph`**: as of this phase, `CaptureFrame()`'s own body no
longer reads `renderGraph` at all (the old compute-pass-texture-write
discovery that used to consume it, via `renderGraph.LastSnapshot()`/
`DebugTextureSnapshotFor()`/`DebugVolumeTextureSnapshotFor()`, was removed
outright along with `computePassPreviews`). The phase document's own
corrected Step 3.2 only authorized ADDING the new `capture` parameter, not
removing any existing one — so `renderGraph` was kept (now genuinely
unused, its parameter name commented out in the `.cpp` definition,
`const rg::RenderGraph& /*renderGraph*/`, to silence any unused-parameter
concern without touching the still-`renderGraph`-named header declaration).
A future phase may choose to drop it for real if nothing else ever needs it
again.

### `FrameDebuggerCaptureContext::ReplayStepPreviews()` constness fixup (Phase 3 gap, resolved this phase per the phase document's own instruction)

Phase 3 left this accessor's constness unspecified ("just an accessor").
Per this phase document's own explicit instruction, it is now a NON-const
accessor:

```cpp
std::vector<RenderTexture>& ReplayStepPreviews() noexcept { return m_replayStepPreviews; }
```

(previously `const std::vector<RenderTexture>& ... const`) — required so
`CaptureFrame()` can `std::move()` out of it. No second, parallel
`TakeReplayStepPreviews()` method was added, per the phase document's own
explicit guidance ("nothing else in this codebase needs a second, read-only
view of this particular vector").

## What was removed entirely (Locked Design Decision #3)

Confirmed via `search_in_dir` across `src/` and `tests/` before deleting
anything — every remaining hit after this phase's edits is a comment-only
prose reference in an unrelated file (e.g. `AtmosphereAerialPerspectiveLutInspection.h`'s
own unrelated `VolumeTexturePreviewRenderer` mentions), never live code:

- `FrameDebuggerComputePassPreview` (struct, `FrameDebuggerHistory.h`).
- `FrameDebuggerHistoryEntry::computePassPreviews` (field).
- `FrameDebuggerComputePassTextureWrite`/`CollectComputePassTextureWrites()`
  and `FrameDebuggerComputePassVolumeTextureWrite`/
  `CollectComputePassVolumeTextureWrites()` (`FrameDebuggerData.h`/`.cpp`).
- The entire HDR-round-trip (`HdrComputePassCopySource`) and
  volume-texture-ray-march capture code inside
  `FrameDebuggerCurrentCapture::CaptureFrame()`
  (`FrameDebuggerHistory.cpp`), including the second/third
  `ImmediateSubmit()`-adjacent loops and their `VkBufferImageCopy`/staging-
  buffer uploads.
- `FrameDebuggerCurrentCapture::m_volumePreviewRenderer` (member) and the
  `#include "../Renderer/VolumeTexturePreviewRenderer.h"` line
  (`FrameDebuggerHistory.h`).
- `Panels/FrameDebuggerPanel.cpp`'s old `selectedComputePassPreview`
  lookup/linear-scan code in `EnsurePreviewDescriptor()`.
- The dead `selectedEventIsGpuSkinning` special case in
  `BuildInspectorPane()` — this was already permanently-false dead code
  left over from `frame-debugger-5`'s own migration (its own comment said
  so explicitly); it is now fully superseded by `m_lastPreviewChoice ==
  NotYetDrawn`, which correctly suppresses the image for EVERY Pre-GameView
  leaf (not just one hardcoded name), so it was removed as part of this
  phase's own cleanup rather than left to rot further.

**Explicitly preserved, per the phase document's own instruction**:
`RenderGraphDebugTextureRegistry`/`DebugTextureSnapshotFor()` and everything
the Render Graph panel / `GET /get_texture` use — these were never touched.

## `Panels/FrameDebuggerPanel.cpp`/`.h` wiring (Step 3.4)

- `EnsurePreviewDescriptor()` rewritten: resolves
  `stepPreviewKind`/`stepPreviewIndex` directly from
  `FindEventDetailsByIndex()`'s result (defaulting to `PostComposite`/`-1`
  when nothing is selected or no capture exists — the same "nothing
  selected" catch-all bucket an explicit Post-GameView-at/after-composite
  selection also falls into), computes `hasPerObjectStepPreviewAtIndex` by
  bounds-checking against `entry->perObjectStepPreviews.size()`, and caches
  the resulting `FrameDebuggerPreviewSourceChoice` into a new member,
  `m_lastPreviewChoice` (declared in `FrameDebuggerPanel.h`, default
  `None`).
- `BuildInspectorPane()`: `showPreviewTexture` no longer references the
  removed `selectedEventIsGpuSkinning`; the placeholder-text branch now
  renders `"Nothing drawn yet at this point in the frame."` when
  `m_lastPreviewChoice == FrameDebuggerPreviewSourceChoice::NotYetDrawn`,
  else the pre-existing `"No Texture"`.
- `TriggerCapture()`'s `m_currentCapture.CaptureFrame(...)` call site
  updated with the new, LAST `m_captureContext` argument.

## Tests updated (Step 3.5)

- `tests/Editor/FrameDebuggerDataTests.cpp` — `ChooseFrameDebuggerPreviewSourceTest`
  fully rewritten for the new signature; covers all four
  `FrameDebuggerStepPreviewKind` values (`NotYetDrawn` always wins;
  `PreComposite` shows `Preview`/`None`; `PostComposite` prefers
  `CompositedPreview`, falls back to `Preview`, else `None`; `PerObjectStep`
  shows `PerObjectStepPreview` when resolvable, else `None` — never falls
  back to the whole-frame images).
- `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` — deleted every test
  that only existed to cover the removed `CollectComputePassTextureWrites()`/
  `CollectComputePassVolumeTextureWrites()` functions (10 tests); added 6 new
  tests proving `BuildRealFrameDebuggerSnapshot()` assigns the right
  `stepPreviewKind`/`stepPreviewIndex`:
  `PreGameViewLeafGetsNotYetDrawnStepPreviewKind`,
  `GameViewLeafGetsPreCompositeStepPreviewKind`,
  `PerObjectDrawRecordChildrenGetPerObjectStepKindAndIncreasingIndex` (proves
  0/1 ordering matches `capture.DrawRecords()`),
  `PostGameViewLeafBeforeCompositePassGetsPreCompositeStepPreviewKind`,
  `CompositePassLeafItselfGetsPostCompositeStepPreviewKind` (also proves a
  leaf strictly AFTER the composite pass is likewise `PostComposite`), and
  `PostGameViewLeavesDefaultToPostCompositeWhenNoCompositePassSurvives`
  (the `FindPostGameViewCompositePassExecutionIndex()` returns `-1` case).
- `tests/Editor/FrameDebuggerHistoryTests.cpp`/`FrameDebuggerCaptureTests.cpp` —
  no changes needed (neither references any identifier this phase
  touched/removed; `CaptureFrame()`/`ReplayStepPreviews()` themselves stay
  Tier 2/untested here, same as before this phase).

## Verification performed (compile check only, per this phase's own Step 4/workflow rules)

- `cmake --build build --target gte_core -j 4` — succeeded, rebuilding
  exactly the 6 affected translation units (`FrameDebuggerCapture.cpp`,
  `FrameDebuggerData.cpp`, `FrameDebuggerHistory.cpp`,
  `Panels/FrameDebuggerPanel.cpp`, `ImGuiEditorLayer.cpp` — transitively —
  plus `Game/RenderSystem.cpp`/`Application/RenderPasses.cpp`, rebuilt only
  because they transitively include a touched header) plus the library link
  step, with zero errors/warnings introduced.
- `cmake --build build --target GreatTamanaEngineTests -j 4` — succeeded.
- Ran `tests\GreatTamanaEngineTests.exe --gtest_filter=*FrameDebugger*` — all
  **80 tests passed**, 0 failed (Phase 3's own last verification reported 85
  passing under this same filter; this phase removed 10 tests that only
  existed to cover the now-deleted `CollectComputePassTextureWrites()`/
  `CollectComputePassVolumeTextureWrites()` functions and added 6 new
  `stepPreviewKind`/`stepPreviewIndex` tests to
  `FrameDebuggerSnapshotBuilderTests.cpp` - the observed, final, actually-run
  count of 80 reflects that net change; every test that ran passed).
- `cmake --build build-editor-off --target gte_core -j 4` — reported "ninja:
  no work to do" (this phase touched ONLY files that already compile
  exclusively under `GTE_ENABLE_EDITOR=ON` — `FrameDebuggerCapture.h`,
  `FrameDebuggerData.h/.cpp`, `FrameDebuggerHistory.h/.cpp`,
  `Panels/FrameDebuggerPanel.h/.cpp` — none of which are part of the
  `GTE_ENABLE_EDITOR=OFF` build's own translation-unit set at all, so this
  confirms zero risk of a link/compile break in that configuration without
  needing a fresh rebuild).
- No full build, no full `ctest` regression run, and no live
  `run_app_background`/`gte_send_request` smoke test were performed this
  phase — per the workflow rules, those are Phase 7's job only. This phase's
  own Definition of Done explicitly calls for "Quick compile check only."

## Definition of Done — status

- [x] Clicking any object draw leaf shows a real image with exactly that
      object (and everything drawn before it) visible, no atmosphere fog, no
      later objects — enforced structurally by `ChooseFrameDebuggerPreviewSource()`'s
      `PerObjectStep` branch never falling back to `CompositedPreview`/`Preview`
      (confirmed by the new `ChooseFrameDebuggerPreviewSourceTest` cases); the
      real image itself was already proven correct/accumulating in Phase 3's
      own live spot-check (this phase only wires the ALREADY-correct images
      into the picking logic).
- [x] Clicking a Pre-GameView compute leaf shows the honest "nothing drawn
      yet" placeholder — `NotYetDrawn` kind always wins in
      `ChooseFrameDebuggerPreviewSource()`, and `BuildInspectorPane()` renders
      the new, distinct placeholder text for it.
- [x] Clicking a Post-GameView leaf at/after the composite pass (or nothing
      selected) shows the true final, fog-inclusive image — `PostComposite`
      kind prefers `CompositedPreview`.
- [x] The old per-compute-pass distinct-texture preview code no longer
      exists anywhere in this codebase — confirmed via `search_in_dir` (every
      remaining hit is a comment-only prose reference in a file this phase
      did not need to touch).
- [x] Quick compile check performed and passed (`gte_core`,
      `GreatTamanaEngineTests`, both built clean; 80/80 `*FrameDebugger*`
      tests passed; `build-editor-off`'s `gte_core` target confirmed
      unaffected).
- [x] `PHASE4_COMPLETION_REPORT.md` written (this file), code + report to be
      committed together via git_add + git_commit.

## Genuine ambiguities encountered

None required `ask_questions`. The one open technical question the phase
document itself flagged as needing a judgment call — "did you find the
composite pass's own execution-order index structurally, or fall back to a
literal pass-name check" — is answered explicitly above: **structurally**,
via a check against each surviving Post-GameView pass's own real
`writeNames` for the literal texture name `"GameViewComposited"`, never
against the pass's own NAME
(`"AtmosphereAerialPerspectiveCompositePass"`) — the phase document's own
Step 3.1 pre-approved this exact middle ground ("prefer a structural check
... over a literal pass-name string if at all findable... a single,
clearly-commented literal pass-name check is an acceptable, honest
fallback" — this implementation achieves the structural version, not the
fallback). Every other technical claim in the phase document (the exact,
corrected `CaptureFrame()` widening steps, the accessor-constness fixup, the
enum/field shapes) matched what the real current source needed exactly, so
no further ambiguity arose.

## Next phase (Phase 5) — what to build on top of this

- `FrameDebuggerPreviewSourceChoice`/`FrameDebuggerStepPreviewKind` are now
  the permanent picking mechanism — Phase 5's own UI/HTTP cleanup should not
  need to touch `ChooseFrameDebuggerPreviewSource()`'s own signature again.
- `m_lastPreviewChoice` (new `FrameDebuggerPanel` member) is available for
  Phase 5 if any HTTP state-reporting route wants to surface which bucket is
  currently active (not required by this phase, not yet exposed over HTTP).
- The removed `computePassPreviews`-era vocabulary
  (`FrameDebuggerComputePassPreview`, `CollectComputePass*TextureWrites()`)
  is gone for good — any lingering doc-comment mentions elsewhere in this
  codebase (none found this phase beyond this campaign's own files) are
  fair game for a future documentation pass (Phase 6).
