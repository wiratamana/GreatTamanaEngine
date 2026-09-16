# PHASE2 — Make the Real "Aerial Perspective Composite" Pass Visible in the Event Tree — COMPLETION REPORT

Campaign: `task_manager/frame-debugger-4/`
Branch: `feature/frame-debugger-impl`
Phase document: `PHASE2_AERIAL_PERSPECTIVE_COMPOSITE_EVENT_TREE_LEAF.md`
Parent: `PHASE0_MASTER_STRATEGY.md`
Also read: `PHASE0_DOUBLE_CHECK_NOTES.md`, `PHASE1_DOUBLE_CHECK_NOTES.md`, `PHASE1_COMPLETION_REPORT.md` (confirming
PHASE1 landed exactly as its own plan describes, with no deviation this phase needed to account for).

## Summary

Implemented PHASE2's "Step 3: The Plan" exactly as written, in full: the Frame Debugger's left-hand event tree
now gains a new, real, selectable leaf for the already-real `"AtmosphereAerialPerspectiveCompositePass"`
render-graph pass, sibling to the existing `"GameView"` leaf. This is a pure, additive extension of
`src/Editor/FrameDebuggerData.cpp`/`.h` — no other production file was touched, exactly matching this phase's
own "Risk level: LOW" framing and its explicit "does not touch `Panels/FrameDebuggerPanel.cpp`,
`FrameDebuggerHistory.h`/`.cpp`, or `ImGuiEditorLayer.cpp`" scope note. Thanks to PHASE1's already-generalized
picking rule (`details->passName == "GameView"` → pre-composite; anything else → post-composite), selecting
this new leaf automatically shows the true, final, atmosphere-composited preview image with zero further
changes anywhere else.

## Files changed

- `src/Editor/FrameDebuggerData.cpp`
  - New anonymous-namespace helper `BuildAerialPerspectiveCompositeLeaf(const rg::RenderGraphPassSnapshot& pass,
    int eventIndex)`, added immediately after the existing `BuildGameViewLeaf()`, exactly as the phase document's
    Step 3.1 specifies verbatim:
    - `leaf.name = pass.name` → the real, raw pass name, `"AtmosphereAerialPerspectiveCompositePass"` (mirrors
      `BuildGpuSkinningLeaf()`'s own raw-name convention — this is the literal text the TREE ROW itself displays,
      per this phase document's own "naming clarification").
    - `details.eventLabel = "Compute Composite"`, `details.passName = "Aerial Perspective Composite"` (the
      shorter, friendlier label shown in the Inspector's "Pass" field once selected — a different string from
      the tree row on purpose).
    - `details.shaderName = "AtmosphereAerialPerspectiveComposite.comp"` — a real, hardcoded fact (there is
      exactly one compute shader this pass ever dispatches).
    - All ten blend/Z/stencil rows set to `"n/a (compute pass)"`, mirroring `BuildGpuSkinningLeaf()`'s own
      "this is a compute dispatch, not a draw call" framing.
    - `details.textures` populated with one `"Read Texture"` row per `pass.readNames` entry followed by one
      `"Write Texture"` row per `pass.writeNames` entry — real, already-resolved strings straight from the
      `RenderGraphPassSnapshot` itself, never fabricated.
    - `details.vectors` gets exactly one `"GPU Time (ms)"` entry from `pass.stats.timing.milliseconds`, mirroring
      `BuildGpuSkinningLeaf()`'s own identical GPU-timing convention.
    - `details.matrices` deliberately left empty, exactly as the phase document specifies (pass-level
      read/write/timing facts only — no per-draw camera matrix for a compute pass).
  - `BuildRealFrameDebuggerSnapshot()` now, immediately after the existing
    `root.children.push_back(BuildGameViewLeaf(*gameViewPass, capture, nextEventIndex++));` line, looks up
    `"AtmosphereAerialPerspectiveCompositePass"` via the already-existing `FindPassByName()` helper (reused
    directly, no new helper needed, exactly as instructed) and — only if a real matching pass was actually found
    this frame — appends `BuildAerialPerspectiveCompositeLeaf(*aerialPerspectiveCompositePass, nextEventIndex++)`
    as root's third child. This mirrors the "GPU Skinning" group's own "only add if actually found" discipline:
    a `graphSnapshot` from before this feature existed (or any test fixture that doesn't include this exact pass
    name) produces no new leaf and no behavior change whatsoever.
- `src/Editor/FrameDebuggerData.h`
  - `BuildRealFrameDebuggerSnapshot()`'s own header-comment "Tree shape produced otherwise: ..." paragraph was
    updated to describe the new optional trailing leaf, using the phase document's own suggested wording almost
    verbatim.
- `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`
  - Added the three new test cases the phase document's Step 3.4 calls for (see below), appended at the end of
    the file inside the existing anonymous namespace, immediately following the pre-existing
    `RenderTargetInfoIsRealAndNamedGameView` test.

No new files were added or removed this phase, and — confirmed by a full re-read of every changed file after
editing — `Panels/FrameDebuggerPanel.cpp`, `FrameDebuggerHistory.h`/`.cpp`, and `ImGuiEditorLayer.cpp` were left
completely untouched, exactly as the phase document's own Step 3.6 ("What this phase deliberately does NOT do")
requires.

## New test cases (Step 3.4)

1. **`AerialPerspectiveCompositePassProducesThirdLeafAfterGameView`** — a `graphSnapshot` containing `"GameView"`
   then a hand-fabricated `"AtmosphereAerialPerspectiveCompositePass"` with `readNames = {"GameView"}`,
   `writeNames = {"GameViewComposited"}`, and a `Present` `GpuTimingSample` of `0.25` ms. Asserts
   `root.children.size() == 2`, the last child's `.name == "AtmosphereAerialPerspectiveCompositePass"`,
   `.eventIndex == 1` (the highest in the tree), `.details->passName == "Aerial Perspective Composite"`,
   `.details->eventLabel == "Compute Composite"`, `.details->blendMode == "n/a (compute pass)"`,
   `.details->textures.size() == 2` with the read entry's `.valueLabel == "GameView"` and the write entry's
   `.valueLabel == "GameViewComposited"`, and the GPU-timing vector reads back `0.25`. **Result: PASS.**
2. **`NoAerialPerspectiveCompositePassAddsNoThirdLeaf`** — a `graphSnapshot` containing only `"GameView"` (no
   composite pass at all, exactly like every pre-existing fixture in this file) — asserts
   `root.children.size() == 1` and `totalEventCount == 1`, explicitly naming and re-confirming the "only add if
   found" non-regression contract for future readers. **Result: PASS.**
3. **`AllThreeGroupsAppearTogetherInRealExecutionOrder`** — a `graphSnapshot` containing, in this exact order,
   one GPU-skinning pass (`"SkinPass_A"`), `"GameView"`, then `"AtmosphereAerialPerspectiveCompositePass"` —
   asserts `root.children.size() == 3` in the order `"GPU Skinning"` group, `"GameView"` leaf,
   `"AtmosphereAerialPerspectiveCompositePass"` leaf, with `eventIndex` values strictly increasing left-to-right
   across the whole tree (0, 1, 2) and `totalEventCount == 3`. **Result: PASS.**

All ten `FrameDebuggerSnapshotBuilderTest`-prefixed cases (7 pre-existing + 3 new) pass — see "Compile check"
below for the exact `ctest` output.

## Deviations from the phase document

None. Every function signature, code sketch, field value, and test-case description in the phase document
matched the live source tree exactly at implementation time (the tree already had exactly the two anchor points
the plan named — "immediately after the existing `BuildGameViewLeaf()`" and "immediately after
`root.children.push_back(BuildGameViewLeaf(...))`" — both real, literal, and unambiguous), and was implemented
essentially as a direct transcription of the phase document's own code sketch, adapted only to fit the
surrounding file's exact current line numbers (which had shifted slightly from PHASE1's own additions).

One clarification carried over from `PHASE0_DOUBLE_CHECK_NOTES.md`/this phase document's own "naming
clarification" note was double-checked directly against the new test assertions: the tree row's own `.name`
field is the raw pass name `"AtmosphereAerialPerspectiveCompositePass"`, never the shorter
`"Aerial Perspective Composite"` string, which only ever appears as `details.passName`. The new
`AerialPerspectiveCompositePassProducesThirdLeafAfterGameView` test explicitly asserts both of these as two
different, independently-checked values, matching the phase document's own guidance.

## Compile check (fast, per this phase's own Step 3.5 — not a full rebuild/regression)

```
cmake --build build --target GreatTamanaEngineTests
ctest --test-dir build -R FrameDebuggerSnapshotBuilderTest --output-on-failure
```

Both succeeded:

- `GreatTamanaEngineTests`: rebuilt `FrameDebuggerData.cpp.obj` (gte_core), `FrameDebuggerSnapshotBuilderTests.cpp.obj`,
  relinked `libgte_core.a` and `GreatTamanaEngineTests.exe` cleanly, zero errors/warnings from any new or
  modified file. (`FrameDebuggerHistory.cpp`/`Panels/FrameDebuggerPanel.cpp`/`ImGuiEditorLayer.cpp` were also
  rebuilt in this same incremental pass purely because their object files were stale from PHASE1's own
  session — no source line in any of those three files was touched this phase, confirmed by re-reading each of
  them in full after the edits above.)
- `ctest -R FrameDebuggerSnapshotBuilderTest`: **10/10 tests passed** (0.12s each, 1.53s total) — the 7
  pre-existing cases plus the 3 new ones added this phase, all green:

```
100% tests passed, 0 tests failed out of 10
```

Per this phase's own Step 3.5, the full test suite (`ctest` with no filter) and a full clean build were
deliberately NOT run — that is reserved for PHASE3.

## Live/manual verification

Not performed this phase (not required by Step 3.5, and this phase document's own Step 3.7 only asks for
"cross-checked against a live `graphSnapshot` if a runtime smoke test is convenient this phase" — optional). The
new leaf's field values were instead verified with the same rigor via the three new Tier-1 tests above, which
hand-fabricate a `RenderGraphPassSnapshot` with the exact real shape
`AtmosphereLutRenderer::AddAerialPerspectiveCompositePass()` actually produces
(`readNames = {"GameView"}`, `writeNames = {"GameViewComposited"}`), so no guesswork was needed about what a
live capture would show. A full, live, HTTP-automation-driven, screenshot-verified smoke test of the whole
feature (confirming `GET /frame_debugger/state`'s `totalEventCount`, `select_event?index=1` selecting this exact
new leaf, and the preview box showing the real atmosphere-composited image) is PHASE3's own explicit job, once
its doc corrections have also landed.

## Next step

PHASE3 (`PHASE3_TESTS_DOCS_FULL_BUILD_AND_LIVE_VERIFICATION.md`) — widen/add Tier-1 tests for both prior phases
(already substantially covered by this phase's own three new cases), correct every doc claim this bug made
false (`AGENTS.md`, `docs/conventions/frame-debugger.md`, `README.md`, `TODO.md`), then a full clean build (both
`GTE_ENABLE_EDITOR` configs) + full `ctest` regression + a live, HTTP-automation-driven, screenshot-verified
smoke test proving the Frame Debugger's captured/previewed image now genuinely includes the
atmosphere-scattering/aerial-perspective effect, with the new `"Aerial Perspective Composite"` leaf (this
phase's own deliverable) visibly selectable in the tree.
