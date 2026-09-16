# PHASE0 DOUBLE-CHECK NOTES — full campaign-wide review (2nd iteration)

Scope: this is the full, campaign-wide double-check called for by this iteration's own task — every strategy
file under `task_manager/frame-debugger-4/` (`PHASE0_MASTER_STRATEGY.md`, `PHASE1_DUAL_STAGE_RETAINED_CAPTURE_
AND_PREVIEW_SELECTION.md`, `PHASE2_AERIAL_PERSPECTIVE_COMPOSITE_EVENT_TREE_LEAF.md`,
`PHASE3_TESTS_DOCS_FULL_BUILD_AND_LIVE_VERIFICATION.md`) was re-read end-to-end, and every factual
claim/file/function/struct/field name it cites was re-verified against the LIVE, current source tree (via
direct `read_file`/`search_in_dir`, never trusted blindly). `PHASE1_DOUBLE_CHECK_NOTES.md` (the earlier,
narrower, PHASE1-only review already run because PHASE1 is this campaign's flagged highest-risk phase) was
read first so this pass would not re-litigate ground it already covered — its own two small additions (the
`FrameDebuggerHistoryEntry` top-of-struct comment update in PHASE1 Step 3.1, and the two extra manual smoke-
check edge cases in PHASE1 Step 3.8) were confirmed already present, verbatim, in the current
`PHASE1_DUAL_STAGE_RETAINED_CAPTURE_AND_PREVIEW_SELECTION.md` — so PHASE1 itself was NOT modified again this
pass; this session's own new findings are entirely in PHASE0/PHASE2/PHASE3 instead. No production code was
written or built this session, and no new markdown files were created (per this task's own rules).

## What was checked

Every real source file any of the four documents cites by exact name was opened and cross-checked line-by-line
against the doc's own claims/code sketches:

- `src/Application/Application.cpp` — confirmed `AddGameViewPass()` (line 615) runs, then
  `AddAtmosphereCompositePass()` (line 629) runs "immediately after" in the same builder callback, exactly as
  PHASE0's Step 2.1 claims; confirmed `SetGameViewCompositedTexture()`'s real call sites and
  `IEditorLayer`/`NullEditorLayer` overrides.
- `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`'s `AddAerialPerspectiveCompositePass()` (line 711) —
  confirmed the exact `builder.AddComputePass("AtmosphereAerialPerspectiveCompositePass", ...)` call, its two
  `pass.ReadTexture(sourceColorHandle, ...)` calls (color + `isDepthResource=true`), `pass.ReadVolumeTexture(...)`,
  and `pass.WriteTexture(outputHandle, ...)` — byte-for-byte matching PHASE2's Step 2 code quote.
- `src/Editor/ImGuiEditorLayer.cpp` — confirmed the exact current `m_frameDebuggerPanel.Build(m_ctx, renderer,
  renderGraph, m_gameView, gpuSkinningPassNames);` call at line 593, the `gameSource = (m_gameViewComposited !=
  nullptr) ? m_gameViewComposited : &m_gameView` precedent a few dozen lines above it, and
  `m_gameViewComposited`'s declaration/assignment — all matching PHASE1's Step 3.6 quotes exactly.
- `src/Editor/FrameDebuggerHistory.h`/`.cpp` — confirmed the CURRENT (pre-PHASE1) single-`preview`-field/
  single-copy `CaptureFrame()` shape matches PHASE1's own "before" description verbatim (barrier sequence,
  debug-name buffer, `AdvanceFrameDebuggerHistoryWriteState()`/`ClampFrameDebuggerHistoryCursor()` calls).
- `src/Editor/Panels/FrameDebuggerPanel.h`/`.cpp` — confirmed `Build()`'s current signature,
  `TriggerCapture()`'s current single-argument `CaptureFrame()` call, `EnsurePreviewDescriptor()`'s current
  body, `BuildInspectorPane()`'s `selectedEventIsGpuSkinning`/`showPreviewTexture` logic, and `Build()`'s own
  `m_lastKnownRawPreviewView` reset block — all matching PHASE1's plan's assumptions with zero drift.
- `src/Editor/FrameDebuggerData.h`/`.cpp` — confirmed `BuildGameViewLeaf()`/`BuildGpuSkinningLeaf()`/
  `FindPassByName()`/`BuildRealFrameDebuggerSnapshot()`'s exact current bodies, confirming PHASE2's two exact
  insertion points ("immediately after the existing `BuildGameViewLeaf()`" and "immediately after
  `root.children.push_back(BuildGameViewLeaf(...))`") are real, present, literal anchors in the live file.
- `src/Renderer/RenderGraph/RenderGraphSnapshot.h` (`RenderGraphPassSnapshot::name`/`readNames`/`writeNames`/
  `stats`), `src/Renderer/GpuTiming.h` (`GpuTimingSample`/`Status`), `src/Renderer/Renderer.h`
  (`CreateRenderTexture()`'s real 7-parameter/4-default signature), `src/Renderer/RenderTexture.h`
  (`Extent()`/`Format()`/`Image()`/`View()`/`Sampler()`), `src/Renderer/RenderGraph/RenderGraphBarrierPlanner.h`
  (`EmitImageBarrier()`/`RequiredStateFor()`/`ResourceState`), and `src/Editor/FrameDebuggerPreviewProcessing.h`
  (`FrameDebuggerPreviewRenderer::RenderPreview()`'s real `(Renderer&, const RenderTexture&, ...)` signature) —
  every one matches every code sketch in PHASE1/PHASE2 exactly; no compile error, UB, or const-correctness
  mistake found in any snippet.
- `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`/`FrameDebuggerHistoryTests.cpp` — confirmed the existing
  `MakePass()` helper/anonymous-namespace convention PHASE2's new test cases are meant to extend, and confirmed
  (via a repo-wide search) that no existing test anywhere hand-fabricates a pass named
  `"AtmosphereAerialPerspectiveCompositePass"`, so PHASE2's additive leaf cannot silently break anything.
- `src/Editor/NullEditorLayer.cpp` — confirmed `BuildUI()` is still a complete no-op that never calls
  `FrameDebuggerPanel::Build()` (the class doesn't exist under `GTE_ENABLE_EDITOR=OFF`), matching PHASE1's
  Step 3.7 claim.
- `src/Network/NetworkServer.cpp`/`NetworkRoutes.h`, `src/Application/FrameDebuggerCommandBridge.h`/`.cpp` —
  confirmed all eight `/frame_debugger/*` routes already exist and need no campaign changes, and confirmed
  `FrameDebuggerStateSnapshotView`'s real field list (`EditorLayer.h`) — this is what surfaced this session's
  one genuine new finding (see below).
- `TODO.md`'s "Frame Debugger" section — confirmed this exact bug was never previously listed there (so
  PHASE3's own Step 3.4 instruction to "confirm nothing to check off" is correct), and confirmed the
  three-kinds-of-tree-node staleness PHASE3's Step 2 predicts for `docs/conventions/frame-debugger.md` (that
  file currently still describes only "GPU Skinning" group + "GameView" leaf, confirmed stale after PHASE2).
- `src/Instantiation/MeshAssetGpuCatalog.cpp`/`Game.h`'s `CollectGpuSkinningDispatchRequests()` — confirmed
  GPU-skinning registration only ever happens for a loaded `.pmx` model with real bone+skin-weight data, never
  for `POST /instantiate_primitive`-spawned primitives — this is what let this session resolve the new finding
  below with a concrete, verifiable answer rather than leaving it as an open question.
- Cross-phase consistency: PHASE1's Locked-Design-Decision-driven picking rule (`details->passName ==
  "GameView"` → pre-composite; everything else → post-composite) was verified to require, and receive, ZERO
  changes from PHASE2's new leaf (PHASE2 never touches `Panels/FrameDebuggerPanel.cpp`), and PHASE3's own doc-
  update targets (`AGENTS.md`, `docs/conventions/frame-debugger.md`, `TODO.md`, `README.md`) were confirmed to
  be exactly the four files that actually need correcting, no more and no fewer. No child phase was found to
  silently contradict any of PHASE0's 9 Locked Design Decisions or its Non-Goals list.

## What was changed, and why

The technical substance of all three child phases (every function signature, every code sketch, every barrier/
copy sequence, the picking-rule logic, the new leaf's field values, the doc-update targets) was found to be
**fully correct and already consistent with the live codebase and with each other** — the same conclusion the
earlier, narrower `PHASE1_DOUBLE_CHECK_NOTES.md` pass reached for PHASE1 specifically. Two real, non-cosmetic
gaps were found and fixed this pass, both in the "worth improving / insufficiency" category (a competent AI
implementer would otherwise have had to guess or could have produced a misleading verification artifact):

1. **`PHASE0_MASTER_STRATEGY.md` (Step 1) and `PHASE2_AERIAL_PERSPECTIVE_COMPOSITE_EVENT_TREE_LEAF.md`
   (Step 1) — added a "naming clarification" each.** Both documents' own narrative prose describes the new
   tree entry as "a new `'Aerial Perspective Composite'` leaf appears in the event tree", which reads as if the
   TREE ROW itself will display the short, friendly text `"Aerial Perspective Composite"`. In fact, PHASE2's
   own Step 3.1 code (verified against `BuildGpuSkinningLeaf()`'s real, live precedent) sets
   `leaf.name = pass.name`, i.e. the tree row's actual displayed text is the longer, raw pass name
   `"AtmosphereAerialPerspectiveCompositePass"` — `"Aerial Perspective Composite"` only ever appears as
   `details.passName` in the Inspector's own "Pass" field once the leaf is selected. This was not a code bug
   (PHASE2's own test-case guidance already correctly asserts `.name == "AtmosphereAerialPerspectiveCompositePass"`
   and `.details->passName == "Aerial Perspective Composite"` as two DIFFERENT things) — it was purely an
   imprecise narrative description that could have misled a live-verification pass (PHASE3's Step 3.7) into
   treating a correct screenshot as a discrepancy, or a future reader into expecting the wrong on-screen text.
   Both documents now explicitly say which string appears where.
2. **`PHASE3_TESTS_DOCS_FULL_BUILD_AND_LIVE_VERIFICATION.md`, Step 3.7 — added a concrete
   "determining the exact `select_event?index=` values" note.** The original plan told the live-smoke-test
   implementer to call `GET /frame_debugger/select_event?index=<the "GameView" leaf's own index>` and
   `index=<the new "Aerial Perspective Composite" leaf's own index>`, but `GET /frame_debugger/state`
   deliberately reports only `totalEventCount` (a count) — verified against `FrameDebuggerStateSnapshotView`'s
   real field list (`EditorLayer.h`) — never the tree's own node names/shape, and no other HTTP route lists
   them either (confirmed: this campaign's own Non-Goals list correctly excludes adding one). A future
   implementer with no memory of this campaign's own design discussions would have had to guess these indices
   with no way to verify them purely over HTTP. This pass adds the concrete, verifiable reasoning instead:
   `BuildRealFrameDebuggerSnapshot()`'s construction order is deterministic (optional "GPU Skinning" group
   first, then "GameView", then the optional new leaf last), and — confirmed by checking
   `MeshAssetGpuCatalog.cpp`/`Game.h` — spawning ONLY primitives/lights via `POST /instantiate_primitive`/
   `POST /instantiate_light` (exactly what Step 3.7's own scene-setup instruction already asked for) can never
   register a GPU-skinned mesh, so the "GPU Skinning" group is guaranteed absent and the two real indices are
   deterministically `0` (`"GameView"`) and `1` (the new leaf) for this specific smoke test's own scene. Step
   3.7 now states this reasoning explicitly, asks the implementer to cross-check it live via
   `totalEventCount == 2`, and folds in the same tree-row-text clarification from finding #1 above so the
   screenshot evidence is interpreted correctly. (While editing this section, a copy/paste slip on this
   session's own first attempt briefly duplicated the trailing "Record every step's..."/"If no distant..."
   paragraphs and briefly clipped one sentence mid-word — both were caught and corrected before finishing, and
   the file was re-read in full afterward to confirm it is clean.)

No other change was made to any file. `PHASE1_DUAL_STAGE_RETAINED_CAPTURE_AND_PREVIEW_SELECTION.md` was left
completely untouched this pass — the earlier, dedicated PHASE1-only review already found it correct and made
its own two small additions, both independently re-confirmed present and unchanged during this pass; making a
cosmetic-only edit to an already-correct file was avoided per this task's own explicit instruction.

## Conclusion

All four strategy documents are technically accurate against the live, current source tree as of this session,
mutually consistent with each other and with `PHASE0_MASTER_STRATEGY.md`'s own Locked Design Decisions/
Non-Goals (no child-phase contradiction found), and sufficiently detailed for a competent AI implementer to
execute without guesswork, apart from the two clarifications added above (both now fixed in place). No gap,
incorrectness, missing file, or missing phase was found beyond what is described above — in particular, no
4th chunk of genuinely necessary work was discovered, and no interaction with `src/Network/`,
`FrameDebuggerCommandBridge`, or `NullEditorLayer.cpp` was found unaccounted for.
