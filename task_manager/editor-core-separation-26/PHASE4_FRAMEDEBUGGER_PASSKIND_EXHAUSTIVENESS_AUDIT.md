# PHASE4 — Required Companion Audit: `FrameDebuggerData.cpp`'s `PassKind` exhaustiveness

Read `PHASE0_MASTER_STRATEGY.md` in full first (especially Locked Decision 2
and Step 2's own 5-site grep list — this whole phase implements that
decision). Read the source document Part B.1 (headline finding) and Part
B.2's "REQUIRED COMPANION AUDIT" bullet in full. Read `PHASE3_COMPLETION_REPORT.md`
first — confirm `PassKind::Blit` exists but is NOT yet producible anywhere.

**This phase MUST land before PHASE5.** The moment `AddBlitPass()` exists and
is called for real (PHASE5), a not-yet-updated binary `PassKind` check in
`FrameDebuggerData.cpp` would silently mis-happen instead of failing to
compile — this is the source document's own explicit "implement in this
order" requirement.

## Step 1: The Goal

Of the 5 real `PassKind` comparison sites in `src/Editor/FrameDebuggerData.cpp`
(PHASE0 Step 2 lists all 5 with line numbers — re-confirm current line
numbers before editing, they may have drifted slightly since PHASE0 was
written):

1. **3 sites are converted** to a real, exhaustive, compiler-enforced
   three-way dispatch over `PassKind` (`Graphics`/`Compute`/`Blit`) — the
   Pre-GameView compute-discovery loop, the GameView view-region walk, and
   the final "Other Render Passes" catch-all sweep. The view-region walk's
   own fix is the one genuine, provable correctness bug this whole audit
   exists to close: a `PassKind::Blit` pass must NEVER be treated as, or
   silently displace, "RenderOpaque."
2. **2 sites are independently RE-VERIFIED (not just cited) to be already
   safe, and are left as plain one-way filters** —
   `FindPostGameViewCompositePassExecutionIndex()`'s own check, and the
   Post-GameView compute-discovery loop. This phase must add an explicit,
   in-code comment at BOTH sites recording this finding and the reasoning,
   so a future re-run of this same grep does not re-litigate it from zero.
3. A new Frame-Debugger-tier test (`FrameDebuggerSnapshotBuilderTests.cpp`)
   proves a synthetic `PassKind::Blit` pass positioned inside the GameView
   view-region's own index range renders as a correctly labeled "Blit" leaf
   and does NOT displace or corrupt the real "RenderOpaque" leaf in that
   same synthetic frame — this is the source document's own literal Part
   B.3 "Frame Debugger correctness" acceptance criterion.

## Step 2: The Situation

Re-confirm every line number below against the ACTUAL current file before
editing (`FrameDebuggerData.cpp` is large; PHASE1-3 did not touch it, but
always re-check per PHASE0 Rule 9).

- **Site 1 (line ~241, `FindPostGameViewCompositePassExecutionIndex()`)**:
  `if (pass.kind != rg::PassKind::Compute || pass.isCulled || pass.viewScope
  == rg::ViewScope::SceneView) { continue; }` followed by a loop body that
  ONLY ever reads `pass.writeNames` (a field equally valid/meaningful for
  ANY `PassKind`, including `Blit`). **Verify**: nothing after this `if`
  reads a Graphics-only field. **Action**: leave the check as-is; add a
  comment: "Confirmed safe for `PassKind::Blit` (editor-core-separation-26
  campaign, PHASE4's own companion audit) — this is a one-way Compute-only
  FILTER (a Blit pass is simply skipped, exactly like a Graphics pass
  already is), never a disguised 'not Compute = Graphics' binary branch;
  the only field read below (`writeNames`) is valid for any `PassKind`. Not
  one of the 3 sites the source document names for conversion — see
  PHASE0_MASTER_STRATEGY.md's Locked Decision 2."
- **Site 2 (line ~808, Pre-GameView compute-discovery loop)**:
  `if (pass.kind != rg::PassKind::Compute) { continue; }`. **This IS one of
  the 3 the source document names** — convert it to a real, exhaustive
  switch, even though (per this campaign's own independent re-analysis) it
  is STRUCTURALLY the same one-way-filter shape as Site 4 below. Do not
  silently skip converting it just because it looks equivalent to Site 4 —
  the user's own explicit instruction (PHASE0 Locked Decision 2) is to
  convert exactly the 3 the source document names, this being one of them.
  Suggested shape (a real, `default:`-less, compiler-enforced switch,
  computing a plain local bool so the SUBSEQUENT `continue`/loop-control
  statements stay outside any switch — see Site 3's own note below for why
  that separation matters):
  ```cpp
  bool isComputeKind = false;
  switch (pass.kind) {
  case rg::PassKind::Compute:
      isComputeKind = true;
      break;
  case rg::PassKind::Graphics:
  case rg::PassKind::Blit:
      break;
  }
  if (!isComputeKind) {
      continue; // Not this loop's concern - see this loop's own header comment above.
  }
  ```
- **Site 3 (lines ~866-926, GameView view-region walk)** — THE real
  correctness fix. Current shape (paraphrased): a `bool isRenderOpaqueLeaf =
  true;` flag; inside the loop, `if (pass.kind == rg::PassKind::Compute) {
  if (!pass.isCulled) { break; } claimed[i] = true; continue; }` (a real
  loop-breaking `break;`, NOT inside any switch today); then an
  UNCONDITIONAL comment "`pass.kind == rg::PassKind::Graphics` from here
  on," followed by code that, on its FIRST reachable iteration
  (`isRenderOpaqueLeaf == true`), calls `BuildRenderOpaqueLeaf()`
  unconditionally — **this is the bug**: a `PassKind::Blit` pass sitting at
  that first position would be wrongly built as "RenderOpaque," and the
  REAL RenderOpaque pass arriving later in the same walk would then be
  mislabeled as a generic leaf instead (since the flag would already be
  consumed).
  **CRITICAL PITFALL — read before editing**: do NOT naively wrap the
  existing `if (pass.kind == rg::PassKind::Compute) { ... break; ... }`
  block in a literal `switch (pass.kind) { case Compute: ... break; ...
  }` — inside a `switch`, a bare `break;` exits the SWITCH, not the
  enclosing `for` loop, silently changing this code's real control flow
  (the walk would no longer stop at the first surviving compute pass). Fix
  this the same way Site 2 does: compute a local, exhaustive-switch-derived
  bool FIRST, then keep every `break;`/`continue;` loop-control statement in
  plain `if` statements OUTSIDE any switch. Full suggested replacement:
  ```cpp
  bool isComputeKind = false;
  switch (pass.kind) {
  case rg::PassKind::Compute:
      isComputeKind = true;
      break;
  case rg::PassKind::Graphics:
  case rg::PassKind::Blit:
      break;
  }

  if (isComputeKind) {
      if (!pass.isCulled) {
          break; // Stop the walk entirely - unchanged from today (this break is
                 // OUTSIDE the switch above, so it correctly still breaks the for loop).
      }
      claimed[static_cast<std::size_t>(i)] = true;
      continue;
  }

  // pass.kind is Graphics or Blit from here on - NEVER assume Graphics.
  if (pass.isCulled || pass.viewScope == rg::ViewScope::SceneView
      || pass.category == rg::RenderPassCategory::FrameDebuggerInternal) {
      claimed[static_cast<std::size_t>(i)] = true;
      continue;
  }
  claimed[static_cast<std::size_t>(i)] = true;

  // THE FIX: a Blit pass is NEVER treated as RenderOpaque, and - critically -
  // does NOT consume isRenderOpaqueLeaf when it isn't. This is what lets the
  // REAL RenderOpaque pass, whenever it is next encountered, still correctly
  // claim the slot even if one or more Blit passes sat in front of it.
  if (isRenderOpaqueLeaf && pass.kind == rg::PassKind::Graphics) {
      FrameDebuggerEventNode renderOpaqueLeaf = BuildRenderOpaqueLeaf(pass, capture, nextEventIndex++);
      int perObjectStepIndex = 0;
      for (const FrameDebuggerDrawRecord& record : capture.DrawRecords()) {
          renderOpaqueLeaf.children.push_back(BuildRenderOpaqueDrawRecordLeaf(
              record, capture.LastViewProjection(), nextEventIndex++, perObjectStepIndex));
          ++perObjectStepIndex;
      }
      root.children.push_back(std::move(renderOpaqueLeaf));
      isRenderOpaqueLeaf = false;
  } else {
      FrameDebuggerEventNode leaf =
          BuildGraphicsPassLeaf(pass, nextEventIndex++, FrameDebuggerStepPreviewKind::PreComposite);
      leaf = WrapPassWithOwnedChildEvent(
          std::move(leaf), nextEventIndex++, GraphicsChildEventLabelFor(pass.drawKind));
      root.children.push_back(std::move(leaf));
      // isRenderOpaqueLeaf is DELIBERATELY NOT touched here - see this block's own comment above.
  }
  ```
  Preserve every existing comment you are not directly replacing (the
  "isRenderOpaqueLeaf" declaration's own doc comment, the loop's own header
  comment) — only the body shown above changes.
- **Site 4 (line ~950, Post-GameView compute-discovery loop)**:
  `if (pass.kind != rg::PassKind::Compute) { continue; }`, followed by code
  that (per the pre-existing comment immediately above it) DELIBERATELY
  already documents "a Graphics-kind pass encountered here... is simply
  skipped and deliberately NOT claimed here either... falls through to the
  final 'Other Render Passes' sweep instead." **Verify**: confirm this
  documented behavior is genuinely correct for a Blit pass too (it is — a
  Blit pass here is skipped exactly like Graphics already is, and correctly
  falls through to Site 5's own sweep, which is being fixed in this same
  phase). **Action**: leave the check as-is; add a comment mirroring Site
  1's own exact wording, adapted: "...Not one of the 3 sites the source
  document names for conversion — this is structurally identical to
  `FindPostGameViewCompositePassExecutionIndex()`'s own already-safe check
  above (Site 1) — see PHASE0_MASTER_STRATEGY.md's Locked Decision 2."
- **Site 5 (line ~1010, "Other Render Passes" catch-all sweep)**: current
  shape is a plain ternary,
  `(pass.kind == rg::PassKind::Compute) ? WrapPassWithOwnedChildEvent(BuildComputeDispatchLeaf(...), ...) :
  WrapPassWithOwnedChildEvent(BuildGraphicsPassLeaf(...), ..., GraphicsChildEventLabelFor(pass.drawKind))`.
  Unlike Site 3, this ternary is ALREADY functionally correct for
  `PassKind::Blit` today (its "else" arm already calls
  `BuildGraphicsPassLeaf()` + `GraphicsChildEventLabelFor(pass.drawKind)`,
  which already has a real `case RenderPassDrawKind::Blit: return "Blit";`
  arm) — but must still be converted to a real, exhaustive, compiler-
  enforced dispatch for defensive future-proofing (this IS one of the 3 the
  source document names). No loop-control `break`/`continue` pitfall exists
  here (this is a plain sequential statement, not inside a `for`'s own
  control flow), so a literal `switch` is safe and correct:
  ```cpp
  FrameDebuggerEventNode leaf;
  switch (pass.kind) {
  case rg::PassKind::Compute:
      leaf = WrapPassWithOwnedChildEvent(
          BuildComputeDispatchLeaf(pass, nextEventIndex++, stepPreviewKind), nextEventIndex++,
          "Compute Dispatch");
      break;
  case rg::PassKind::Graphics:
  case rg::PassKind::Blit:
      leaf = WrapPassWithOwnedChildEvent(
          BuildGraphicsPassLeaf(pass, nextEventIndex++, stepPreviewKind), nextEventIndex++,
          GraphicsChildEventLabelFor(pass.drawKind));
      break;
  }
  otherPassesGroup.children.push_back(std::move(leaf));
  ```
  (Confirm `FrameDebuggerEventNode` is default-constructible before relying
  on `FrameDebuggerEventNode leaf;` — it already is, used the same way for
  `otherPassesGroup` a few lines above this exact spot.)
- Test file: `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` — already
  has `MakePass()`, `MakeComputePass()`, `MakeGraphicsPassWithCategory()`
  helpers (lines ~67-115) and dozens of `TEST(FrameDebuggerSnapshotBuilderTest,
  ...)` cases calling `BuildRealFrameDebuggerSnapshot(graphSnapshot,
  capture)`. Add a new `MakeBlitPass(const std::string& name)` helper
  mirroring `MakeComputePass()`'s exact shape:
  ```cpp
  rg::RenderGraphPassSnapshot MakeBlitPass(const std::string& name)
  {
      rg::RenderGraphPassSnapshot pass = MakePass(name);
      pass.kind = rg::PassKind::Blit;
      pass.drawKind = rg::RenderPassDrawKind::Blit;
      return pass;
  }
  ```

## Step 3: The Plan

1. Convert Sites 2, 3, 5 exactly as shown above (re-verify current line
   numbers first).
2. Add the "checked, found already-safe, and why" comments at Sites 1 and 4.
3. Add `MakeBlitPass()` to `FrameDebuggerSnapshotBuilderTests.cpp`.
4. Add at least these 2 new tests:
   - `BlitPassImmediatelyAfterPivotIsNeverTreatedAsRenderOpaqueAndDoesNotDisplaceRealRenderOpaque`
     — build a `graphSnapshot` with, in order: `MakePass("RenderOpaque")`
     (the pivot — default `renderPassEvent == Opaques`), THEN
     `MakeBlitPass("HistoryBufferBlit")` positioned as the very NEXT pass
     (so it is the first pass the view-region walk sees AFTER the pivot
     itself is consumed... re-check `FindViewRegionPivot()`'s own exact
     semantics before assuming whether the pivot pass itself is ALSO walked
     by this same loop or handled separately — read `BuildRealFrameDebuggerSnapshot()`'s
     full body fresh before writing this fixture, do not guess), THEN a
     second real `MakePass("DrawSkyBackground")` pass. Call
     `BuildRealFrameDebuggerSnapshot(graphSnapshot, capture)`. Assert: a
     leaf named "RenderOpaque" exists in the result (built via
     `BuildRenderOpaqueLeaf`, i.e. carrying whatever
     `BuildRenderOpaqueLeaf` uniquely produces — e.g. its per-entity-draw
     children shape, if `capture` has any `DrawRecords()`), a SEPARATE leaf
     named "HistoryBufferBlit" exists with its wrapped child event labeled
     "Blit" (via `GraphicsChildEventLabelFor(RenderPassDrawKind::Blit)`),
     and a THIRD leaf named "DrawSkyBackground" ALSO exists as a normal
     Graphics leaf (proving the real RenderOpaque pass was not displaced by
     the Blit pass sitting in front of it, and that the Graphics pass
     coming AFTER a Blit pass is unaffected too).
   - `BlitPassInOtherRenderPassesSweepGetsCorrectBlitLabel` — a pass built
     via `MakeBlitPass()`, tagged with a `renderPassEvent` that places it
     structurally AFTER everything else (mirroring how the pre-existing
     "DemoRenderFeaturePlugin_Clear"-shaped fixtures in this same file
     already test the catch-all sweep — search this file for
     `AfterEverything` for the exact existing pattern to copy), with no
     pivot pass positioned to claim it first. Assert it lands inside the
     "Other Render Passes" group, with its wrapped child event labeled
     "Blit".
5. Re-run the FULL grep from PHASE0 Step 2 (regex `\.kind\s*[=!]=` across
   all of `src/`) one more time, fresh, right now — confirm still exactly 5
   hits, all 5 still inside `FrameDebuggerData.cpp`, and confirm no NEW
   `PassKind` comparison was introduced anywhere else by PHASE1-3. If
   anything has changed since PHASE0 was written, use `ask_questions`.

## Step 4: Verification

1. `ask_questions` first for any genuine ambiguity — in particular, if
   `FindViewRegionPivot()`'s exact semantics (does the walk in Site 3 start
   AT the pivot pass itself, or immediately after it?) are unclear once you
   read the real code, ask rather than guess; the new test's fixture
   ordering depends on getting this exactly right.
2. Incremental build: `cmake --build build`.
3. Targeted test run:
   `cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug -R FrameDebuggerSnapshotBuilderTest --output-on-failure`
   — this test file is large (98KB); confirm the FULL pre-existing suite
   still passes, not just the 2 new tests.
4. This phase's own diff is large enough (3 real control-flow rewrites) that
   PHASE0 Rule 4 explicitly permits (but does not require) ONE
   `dispatch_sub_agent` self-double-check of this phase's own just-finished
   diff before writing the completion report, IF you judge it warranted —
   it must instruct the sub-agent to ALSO use `ask_questions` for its own
   ambiguities (PHASE0 Rule 1), and it must NOT create its own report file;
   it reports back inline.
5. Write `PHASE4_COMPLETION_REPORT.md` into this same folder — explicitly
   quote the final code at all 5 sites (not just the 3 changed ones), and
   explicitly state the re-run grep's result.
6. `git_add` + `git_commit`.
