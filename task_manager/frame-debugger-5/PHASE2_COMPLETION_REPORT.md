# PHASE2 — COMPLETION REPORT: Generic Compute-Dispatch Event-Tree Discovery

## Parent -> `PHASE0_MASTER_STRATEGY.md`

Status: **DONE**. Branch: `feature/frame-debugger-impl` (unchanged, no new branch created).

## Summary

`src/Editor/FrameDebuggerData.cpp`'s `BuildRealFrameDebuggerSnapshot()` no longer discovers compute passes
by hardcoded name (the old `"GPU Skinning"` name-list special case and the old hardcoded
`FindPassByName(..., "AtmosphereAerialPerspectiveCompositePass")` special case are both **deleted**). It now
discovers **every real, surviving compute pass** in the current frame's `RenderGraphSnapshot` generically, via
PHASE1's `RenderGraphPassSnapshot::isComputePass` flag, and builds one uniform `BuildComputeDispatchLeaf()`
leaf per pass. Per Locked Design Decision #8 (`PHASE0_MASTER_STRATEGY.md`'s v2 review finding), these leaves
are SPLIT into two groups — `"Compute Dispatches (Pre-GameView)"` and `"Compute Dispatches (Post-GameView)"`
— positioned strictly before/after the real `"GameView"` leaf according to each pass's own real index in
`graphSnapshot.passesInExecutionOrder` relative to `"GameView"`'s own index. Each group is only added when it
actually has at least one real child this frame (never an empty, misleading group).

The now-unneeded `gpuSkinningPassNamesThisFrame` plumbing was removed end-to-end:
`BuildRealFrameDebuggerSnapshot()`'s own parameter, `FrameDebuggerPanel::Build()`'s parameter and
`m_frameGpuSkinningPassNames` member, and `ImGuiEditorLayer.cpp`'s local-vector-building call site (which
also removed the now-otherwise-unused `game.CollectGpuSkinningDispatchRequests()` call from that function).

Every read/write row on a compute-dispatch leaf is now labeled by its own real `ResourceKind`
(Texture/Buffer/VolumeTexture, via PHASE1's `readKinds`/`writeKinds`) using two new small exhaustive-switch
helpers, `ReadRowLabelForKind()`/`WriteRowLabelForKind()` (mirroring `RenderGraphTypes.cpp`'s
`IsWriteAccess()`/`ToString()` "no `default:` case" convention).

**A genuine implementation bug was found and fixed in the phase document's own Step 3.3 code sample** during
this phase (see "Deviation from the phase document" below) — this was caught by this phase's own new tests,
not blindly trusted.

## Exact diff shape at each touch point

### 1. `src/Editor/FrameDebuggerData.h`

- `BuildRealFrameDebuggerSnapshot()`'s `gpuSkinningPassNamesThisFrame` parameter removed from its signature;
  its doc comment rewritten to describe the new split-group tree shape and the generic `isComputePass`-driven
  discovery mechanism, per PHASE0's Locked Design Decision #2/#6/#8.

### 2. `src/Editor/FrameDebuggerData.cpp`

- Deleted `BuildGpuSkinningLeaf()` and `BuildAerialPerspectiveCompositeLeaf()` outright.
- Deleted the now-unused `Contains()` helper (confirmed via grep: nothing else in this file used it after the
  name-list mechanism it served was removed).
- Added `ReadRowLabelForKind()`/`WriteRowLabelForKind()` — two small, exhaustive `switch (rg::ResourceKind)`
  helpers with no `default:` case, returning `"Read Texture"`/`"Read Buffer"`/`"Read Volume Texture"` (and the
  `"Write ..."` counterparts).
- Added `BuildComputeDispatchLeaf()` — the one new, fully generic leaf builder for ANY real compute dispatch,
  whatever its name: `leaf.name`/`details.passName`/`details.shaderName` are all the raw, real pass name
  (never fabricated/prettified — a deliberate change from the old special cases, which invented friendly
  labels like `"GPU Skinning"`/`"Aerial Perspective Composite"`/a hardcoded `.comp` filename);
  `details.eventLabel = "Compute Dispatch"` uniformly (replacing the old, pass-specific
  `"Compute Dispatch"`/`"Compute Composite"` split); every read/write row uses the new kind-label helpers;
  blend/Z/stencil rows stay `"n/a (compute pass)"`; GPU timing is reported exactly like the old functions did.
- Rewrote `BuildRealFrameDebuggerSnapshot()`'s discovery logic: computes `gameViewIndex` via pointer
  arithmetic against `gameViewPass` (already obtained via the existing `FindPassByName()`), then builds
  `preGameViewGroup`/`postGameViewGroup` via two RANGE-RESTRICTED loops (see "Deviation" section below for why
  two loops instead of the phase document's one merged loop), only pushing each group onto `root.children` if
  non-empty, with `"GameView"`'s own leaf built in between.

### 3. `src/Editor/Panels/FrameDebuggerPanel.h`/`.cpp`

- `Build()`'s `gpuSkinningPassNamesThisFrame` parameter removed from its signature; doc comment updated.
- `m_frameGpuSkinningPassNames` member removed; replaced with a comment explaining the removal.
- `TriggerCapture()`'s call into `BuildRealFrameDebuggerSnapshot()` updated to the new 3-argument signature
  (`graphSnapshot, m_captureContext, renderTargetInfo`).
- `BuildInspectorPane()`'s `selectedEventIsGpuSkinning` check (`details->passName == "GPU Skinning"`) was
  **left in place, not deleted** — it is out of PHASE2's documented scope (PHASE0's Step 3.7 explicitly says
  "do not add any preview-picking logic here" this phase) and is now permanently `false` (since no leaf's
  `passName` is ever literally `"GPU Skinning"` anymore), which is the CORRECT, EXPECTED interim behavior per
  PHASE0's Step 3.7 ("selecting any compute-dispatch leaf still falls back to showing the whole-frame image
  until PHASE3"). A comment was added explaining exactly why this check is now permanently inert and that
  PHASE3 is expected to replace it with a real per-pass-preview lookup, so a future reader is not confused by
  dead-looking code.

### 4. `src/Editor/ImGuiEditorLayer.cpp`

- The `BuildUI()` block that built a local `gpuSkinningPassNames` vector from
  `game.CollectGpuSkinningDispatchRequests()` purely to feed `FrameDebuggerPanel::Build()` was removed
  entirely; the call site now reads `m_frameDebuggerPanel.Build(m_ctx, renderer, renderGraph, m_gameView,
  m_gameViewComposited);`.

## `gpuSkinningPassNamesThisFrame` plumbing removal — blast-radius check (what was confirmed before deleting)

- **`m_frameGpuSkinningPassNames` (`FrameDebuggerPanel.h`/`.cpp`)**: grepped `Panels/FrameDebuggerPanel.cpp`
  for the member name — found exactly 2 usages before this change (the assignment in `Build()` and the read
  in `TriggerCapture()`'s call into `BuildRealFrameDebuggerSnapshot()`), both removed/updated; nothing else in
  that file read it for a different purpose.
- **`game.CollectGpuSkinningDispatchRequests()` (`ImGuiEditorLayer.cpp`)**: grepped the whole file for
  `GpuSkinningDispatchRequest` — found exactly 1 call site (the block being deleted), confirming it existed
  purely to resolve the now-removed name list and was not reused for any other display/logic purpose in that
  function or file.
- **`Contains()` helper (`FrameDebuggerData.cpp`)**: grepped the file after removing its one call site inside
  the old GPU-Skinning-group block — confirmed no other call site existed, so it was deleted rather than left
  dead.
- **Test-side blast radius**: grepped `tests/` for `BuildRealFrameDebuggerSnapshot` and `gpuSkinning`
  (case-insensitive) — confirmed only `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` calls that function
  or references the removed name-list parameter; `tests/Editor/FrameDebuggerDataTests.cpp` and
  `tests/Editor/FrameDebuggerCaptureTests.cpp`-style files do not call it at all (one comment in
  `FrameDebuggerDataTests.cpp` mentions `"GPU Skinning"` as an illustrative example inside
  `ChooseFrameDebuggerPreviewSourceTest`'s own comment, unrelated to the removed parameter — left untouched,
  since it does not exercise the removed code path).

## Deviation from the phase document — a genuine bug in Step 3.3's own code sample, found and fixed

The phase document's Step 3.3 shows ONE single loop iterating the WHOLE `passesInExecutionOrder` range,
routing each surviving compute pass into whichever group its index falls into, with `"GameView"`'s own leaf
only built AFTER that entire loop finishes. Implementing this literally, then writing this phase's own new
`EventIndexIsMonotonicAcrossPreGameViewGameViewAndPostGameView` test (from Step 3.6 item (e), which the same
document explicitly asks for), **that test — and two others — failed**: a single merged loop assigns
`eventIndex` values to POST-GameView passes BEFORE `"GameView"` itself ever gets one (since the whole loop
completes before `BuildGameViewLeaf()` is ever called), which silently violates the SAME document's own
explicitly-stated invariant a few paragraphs later ("IMPORTANT (`nextEventIndex` ordering caveat)": *"the
pre-GameView loop above assigns `eventIndex` values to pre-GameView leaves BEFORE `"GameView"` itself gets
its own `eventIndex`, and the post-GameView loop assigns its leaves' `eventIndex` values AFTER... Do not
reorder these three blocks relative to each other"*) — which only holds if the pre-loop and post-loop are
genuinely two SEPARATE blocks with `"GameView"`'s own leaf built strictly between them, not one merged loop.

Since the desired invariant (`eventIndex` strictly increasing in true chronological order: every pre-GameView
leaf's index < `"GameView"`'s own index < every post-GameView leaf's index) is stated explicitly and
unambiguously elsewhere in the very same section — this was judged to be an implementation bug in the sample
code, not a genuine design ambiguity requiring `ask_questions` — the standing instruction to "re-verify
anything against the live source before trusting it blindly" was applied here. Fixed by splitting the single
loop into two RANGE-RESTRICTED loops: one over `[0, gameViewIndex)` (building `preGameViewGroup`, run and
pushed onto `root.children` BEFORE `"GameView"`'s own leaf is built), and one over `(gameViewIndex, size)`
(building `postGameViewGroup`, run AFTER). This produces the exact same final tree SHAPE the phase document
specifies (same two groups, same membership rule, same "only add if non-empty" behavior) — only the
`eventIndex` assignment ORDER changed, to genuinely satisfy the document's own stated invariant. All three
initially-failing tests pass with this fix; no other test's expectations needed to change because of it.

## Tests — REWRITTEN vs. genuinely NEW

All tests live in `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`.

### REWRITTEN (old assertion vs. new assertion, in brief)

1. **`GameViewWithNoGpuSkinningProducesExactlyOneLeaf` -> `GameViewWithNoComputePassesProducesExactlyOneLeaf`**
   — old: called `BuildRealFrameDebuggerSnapshot(graphSnapshot, capture, {})` (3rd arg = empty name list); new:
   call site simplified to drop the removed parameter. All assertions on the single `"GameView"` leaf are
   unchanged; only the comment ("No GPU Skinning group at all" -> "No compute-dispatch groups at all") and
   test name changed.
2. **`TwoGpuSkinningPassesProduceGroupPlusGameViewLeaf` -> `TwoPreGameViewComputePassesProduceOnePreGameViewGroup`**
   — old: fed a `gpuSkinningPassNamesThisFrame` name list of 2 pass names, asserted a `"GPU Skinning"` group
   whose leaves' `passName` was the fabricated `"GPU Skinning"` string; new: the same two passes are marked
   `isComputePass = true` directly (one with a `Buffer` write, one with a `Texture` write — also covers
   PHASE2's own Step 3.6 item (a)), asserting the new `"Compute Dispatches (Pre-GameView)"` group name, each
   leaf's `passName` now the RAW pass name (`"SkinPass_A"`, not `"GPU Skinning"`), and each read/write row now
   labeled `"Write Buffer"`/`"Write Texture"` per its real kind.
3. **`GpuSkinningNameWithNoMatchingRealPassAddsNoGroup` -> `NonComputePassIsNeverTreatedAsComputeDispatch`** —
   the OLD scenario ("a caller-supplied name with no matching real pass") cannot occur anymore now that the
   name-list parameter is gone entirely; replaced with the analogous NEW regression covering the same spirit:
   an ordinary pass declared via plain `AddPass()` (`isComputePass` defaults to `false`) must never be
   mistaken for a compute dispatch no matter its name.
4. **`AerialPerspectiveCompositePassProducesThirdLeafAfterGameView` -> `PostGameViewComputePassProducesLeafUnderPostGameViewGroup`**
   — old: asserted the hardcoded friendly `passName == "Aerial Perspective Composite"`, `eventLabel ==
   "Compute Composite"`, `shaderName == "AtmosphereAerialPerspectiveComposite.comp"`, and the pass appended as
   a bare third top-level leaf; new: the same pass is now discovered generically (`isComputePass = true`),
   asserting the raw pass name for `passName`/`shaderName`, the generic `"Compute Dispatch"` `eventLabel`, and
   that it now lands as a child of the NEW `"Compute Dispatches (Post-GameView)"` group (a sibling of
   `"GameView"`, not a bare third leaf).
5. **`NoAerialPerspectiveCompositePassAddsNoThirdLeaf` -> `NoComputePassesProduceNeitherGroup`** — old:
   asserted no third leaf appears; new: asserted NEITHER `"Compute Dispatches (...)"` group appears (covers
   PHASE2's own Step 3.6 item (b)) — same call-site simplification, comment/name updated to match the new
   group-based shape.
6. **`AllThreeGroupsAppearTogetherInRealExecutionOrder` -> `PreAndPostGameViewGroupsAppearTogetherAsSiblingsInRealExecutionOrder`**
   — old: fed a `gpuSkinningPassNamesThisFrame` name list, asserted a `"GPU Skinning"` group + `"GameView"` +
   a bare `"AtmosphereAerialPerspectiveCompositePass"` leaf, three top-level siblings; new: both passes marked
   `isComputePass` directly, asserting the SPLIT `"Compute Dispatches (Pre-GameView)"` -> `"GameView"` ->
   `"Compute Dispatches (Post-GameView)"` three-top-level-sibling shape (Locked Design Decision #8 — covers
   Step 3.6 item (a3)).

Three other pre-existing tests (`NoGameViewPassProducesEmptyResult`,
`DistinctPipelineAndTextureNamesProduceDistinctEntriesNotDuplicates`,
`ViewProjectionMatrixRoundTripsWithoutTransposing`, `RenderTargetInfoIsRealAndNamedGameView`) needed only a
mechanical call-site fix (dropping the removed `gpuSkinningPassNamesThisFrame` argument, or passing
`renderTargetInfo` as the new 3rd positional argument) — no assertion changed at all; these are NOT counted as
"rewritten" above since their expected behavior is identical, only the removed-parameter call-site shape
changed.

### Genuinely NEW tests

- **`CulledComputePassIsExcludedFromEitherComputeDispatchGroup`** — Step 3.6 item (c): a culled compute pass
  on BOTH the pre- and the post-GameView side is correctly excluded from its respective group, while a
  surviving pass on each side still appears.
- **`ComputeDispatchLeafLabelsReadWriteRowsByRealResourceKind`** — Step 3.6 item (d): one pass with all three
  `ResourceKind`s (Texture/Buffer/VolumeTexture) on BOTH its reads and its writes, asserting every one of the
  6 resulting rows is labeled correctly (`"Read Texture"`/`"Read Buffer"`/`"Read Volume Texture"`/`"Write
  Texture"`/`"Write Buffer"`/`"Write Volume Texture"`).
- **`EventIndexIsMonotonicAcrossPreGameViewGameViewAndPostGameView`** — Step 3.6 item (e): 2 pre-GameView
  compute passes + `"GameView"` + 2 post-GameView compute passes, asserting `eventIndex` is strictly
  increasing left-to-right across the whole tree (0, 1, 2, 3, 4) — this is the test that caught the Step 3.3
  code-sample bug described above.

**Total: 6 rewritten tests, 3 genuinely new tests, 4 tests with a mechanical call-site-only fix (no assertion
change), 12 tests total in the file (was 10 before this phase).**

## Fast compile check (as run this phase)

```
cmake --build build --target GreatTamanaEngineTests
```
→ succeeded, zero warnings/errors introduced.

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure -R FrameDebugger
```
→ 100% tests passed, 69/69 (every `FrameDebuggerSnapshotBuilderTest`/`FrameDebuggerDataTest`/
`FrameDebuggerHistoryTest`/`FrameDebuggerCaptureContextTest`/`FrameDebuggerCommandBridgeTest`/
`ParseFrameDebugger*QueryTests`/`BuildFrameDebugger*ResponseJsonTests`/`NetworkRoutesTests` variant in this
filter passed — the rewritten ones against their NEW assertions, every untouched one unchanged).

No full build and no full `ctest` regression were run this phase, per this phase document's own explicit "No
Full Build"/"Fast Compile Check" rules (PHASE5 is the only phase in this campaign that does a full clean
build + full regression + live smoke test).

## Design decisions / ambiguity check

No `ask_questions` call was needed this phase. The one genuine fork encountered (Step 3.3's merged-loop code
sample vs. its own explicitly-stated `eventIndex` monotonic-ordering invariant) was resolved by following the
document's own explicit, unambiguous prose requirement rather than its possibly-buggy inline code sample —
this was judged a verifiable implementation bug caught by the document's own requested test coverage, not a
genuine design ambiguity/product decision needing user input. The Step 3.4 "optional friendly-label lookup
table" was deliberately NOT added (matches the document's own "genuinely optional... raw pass name is always
truthful" framing, and no reviewer signal suggested it was wanted this phase).

## Next phase

PHASE3 (`PHASE3_GENERIC_PER_PASS_RETAINED_PREVIEW_CAPTURE.md`) is expected to replace
`BuildInspectorPane()`'s now-permanently-inert `selectedEventIsGpuSkinning` check (see touch-point 3 above)
with a real per-compute-pass retained-preview lookup, closing the "selecting a new compute leaf still shows
the whole Game View image" gap this phase deliberately leaves open (per PHASE0's Step 3.7, explicitly
out-of-scope for PHASE2).
