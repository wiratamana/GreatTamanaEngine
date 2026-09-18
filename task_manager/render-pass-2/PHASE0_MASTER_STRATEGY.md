# PHASE0_MASTER_STRATEGY — Frame Debugger Pass-Ownership Campaign (`render-pass-2`)

_Part of the `feature/render-pass-impl` branch. Lives under
`task_manager/render-pass-2/`. This is the ORCHESTRATOR document — every
other `PHASEn_*.md` file in this same folder is a child of this one. Read
this file FIRST, always, before touching any child phase. This campaign is
the direct successor to `task_manager/render-pass-1/` (read that folder's
`CAMPAIGN_COMPLETION_REPORT.md` once, for context, but do not re-do any of
its work — it already shipped and is not being revisited here)._

## Step 1: The Goal (Where are we going?)

The user opened the Frame Debugger, expanded `"RenderOpaque"` (which
correctly shows a "v" expandable arrow, owning two per-entity children,
`SmokeTestCube (Entity 0)` and `Entity 2 (Entity 2)`), and then looked at
the very next sibling row, `"DrawSkyBackground"` — which has **no**
expand arrow at all, sits flat, and visually looks like a stray, orphaned
row that belongs to no render pass. The user's own words: _"DrawSkyBackground
seems not owned by any render-pass? ... perhaps there something wrong with
current implementation?"_ Their own target shape:

```
v {RENDER_PASS_NAME}
   {DRAW_MESH}
   {COMPUTE_DISPATCH}
   {BLIT}
```

i.e. every real render pass in the tree should be its own expandable
`"v {PASS_NAME}"` header that OWNS one or more real child event rows
describing the actual GPU operation(s) it issues — never a flat, childless
row that happens to sit at the same indentation level as a real pass group.

**Root cause, confirmed by direct source inspection (this is not a
guess):** `src/Editor/Panels/FrameDebuggerPanel.cpp`'s `RenderEventNode()`
(lines 578-629) decides whether a row gets the expandable "v" arrow purely
by checking `if (!node.children.empty())` — nothing else. `"RenderOpaque"`
gets the arrow only because `BuildRealFrameDebuggerSnapshot()`
(`src/Editor/FrameDebuggerData.cpp`) happens to attach real per-entity
children to it (via `capture.DrawRecords()`). Every OTHER real pass leaf
this function builds — `"DrawSkyBackground"` (`BuildGraphicsPassLeaf()`,
lines 584-622) AND every individual Atmosphere `"Compute LUT"` pass (e.g.
`"AtmosphereTransmittanceLutPass"`) AND every Pre/Post-GameView compute
dispatch (`BuildComputeDispatchLeaf()`, lines 282-367) — is built as a
single FLAT `FrameDebuggerEventNode` with `children` always empty. This is
not a display bug and not a missing "owner" pointer anywhere in the data —
it is a genuine STRUCTURAL gap: these pass leaves have never been given a
child node describing their own actual draw/dispatch operation, the way
`"RenderOpaque"` has.

**The goal of this campaign is to close that structural gap for EVERY real
pass leaf in the Game View tree** — not just `"DrawSkyBackground"` — so
every one of them uniformly becomes a "v {PASS_NAME}" header owning exactly
one real child event row (`"Compute Dispatch"` for a Compute-kind pass;
`"Draw Mesh"` / `"Draw Quad"` / `"Blit"` for a Graphics-kind pass,
depending on what that specific pass structurally does), while leaving
`"RenderOpaque"`'s own already-correct per-entity-children shape completely
untouched. This was confirmed as the desired scope directly with the user
(see "Locked Design Decisions" below) — a narrower "just patch
`DrawSkyBackground`" fix was explicitly rejected in favor of this
consistent, generalized one.

Target tree shape once this campaign ships (test scene: 2 mesh entities,
default Atmosphere LUT chain, no GPU Skinning):

```
v Game View
  v Compute LUT
     v AtmosphereTransmittanceLutPass
        Compute Dispatch
     v AtmosphereMultiScatteringLutPass
        Compute Dispatch
     v AtmosphereSkyViewLutPass
        Compute Dispatch
     v AtmosphereAerialPerspectiveVolumePass
        Compute Dispatch
     v AtmosphereAerialPerspectiveVolumeDebugSlicePass
        Compute Dispatch
  v RenderOpaque                              (UNCHANGED shape)
     SmokeTestCube (Entity 0)
     Entity 2 (Entity 2)
  v DrawSkyBackground                          (FIXED — was flat, now owns its own child)
     Draw Quad
  v Compute Dispatches (Post-GameView)
     v AtmosphereAerialPerspectiveCompositePass
        Compute Dispatch
```

Every pass-level row (`"AtmosphereTransmittanceLutPass"`,
`"DrawSkyBackground"`, etc.) stays independently selectable in the
Inspector too, exactly like `"RenderOpaque"` already is today — clicking
either the pass row or its one child row shows real, correct details for
that pass (Locked Design Decision #2 below).

## Step 2: The Situation (Where are we now?)

Confirmed by direct source inspection (this document is the single source
of truth for every fact quoted in every child phase — do not re-derive
these from scratch):

1. **`src/Editor/Panels/FrameDebuggerPanel.cpp`'s `RenderEventNode()`**
   (lines 578-629) needs ZERO changes for this whole campaign. It already,
   generically, renders any node with `!node.children.empty()` as an
   expandable `ImGui::TreeNodeEx()` row (selectable too, if
   `node.isDrawCall` is also true — this is the exact dual-role shape
   `"RenderOpaque"` already exercises), and any childless node with
   `isDrawCall == true` as a flat `ImGui::Selectable()` row. This campaign
   is a DATA-layer-only fix — the whole bug lives in what
   `BuildRealFrameDebuggerSnapshot()` builds, never in how it's drawn.
2. **`src/Editor/FrameDebuggerData.cpp`'s `BuildRealFrameDebuggerSnapshot()`**
   (lines 626-805) builds the whole tree from a real
   `rg::RenderGraphSnapshot` + `FrameDebuggerCaptureContext` every captured
   frame. It has three call sites that build a single, FLAT, childless pass
   leaf today:
   - Lines 674-686 (`Compute LUT` / `Compute Dispatches (Pre-GameView)`
     loop) — calls `BuildComputeDispatchLeaf()` once per surviving
     Compute-kind pass before the `"RenderOpaque"` pivot.
   - Lines 755-761 (the "view region" walk's `else` branch) — calls
     `BuildGraphicsPassLeaf()` once for every Graphics-kind pass in the view
     region that is NOT `"RenderOpaque"` itself (today, always exactly
     `"DrawSkyBackground"`; a real future `"RenderTransparent"` lands here
     too, per `render-pass-1`'s own PHASE2).
   - Lines 771-790 (`Compute Dispatches (Post-GameView)` loop) — calls
     `BuildComputeDispatchLeaf()` again, for every surviving Compute-kind
     pass after the view region (today, always exactly
     `AtmosphereAerialPerspectiveCompositePass`).
3. **`BuildComputeDispatchLeaf()`** (`FrameDebuggerData.cpp` lines 282-367)
   and **`BuildGraphicsPassLeaf()`** (lines 584-622) each build exactly ONE
   `FrameDebuggerEventNode` with `children` left at its default-empty
   state. Both already set a real, correct `details.eventLabel` (`"Compute
   Dispatch"` for the former; `"Draw Pass"` for the latter — see PHASE2's
   own Step 3 for why `"Draw Pass"` is replaced) and real pass-level facts
   (draw stats, blend/Z/stencil state, GPU timing) — none of that pass-level
   data is wrong or missing; it simply never gets a CHILD event attached the
   way `"RenderOpaque"` does.
4. **`BuildRenderOpaqueLeaf()`** (lines 378-475) + its call site (lines
   738-754) already build the CORRECT shape this campaign is generalizing:
   one selectable parent leaf (`"RenderOpaque"`, with real pass-level
   aggregate stats/details) PLUS one real, independently-selectable child
   leaf per real per-entity draw record (`BuildRenderOpaqueDrawRecordLeaf()`,
   lines 494-558, fed by `capture.DrawRecords()`). This mechanism is
   UNTOUCHED by this whole campaign — it is not broken, and per-entity
   identity is real, per-object data no generic wrapper could reconstruct
   for an arbitrary pass anyway.
5. **There is no way today to tell, for an arbitrary Graphics-kind pass,
   WHAT KIND of draw operation it issues** (a real per-object mesh draw vs.
   a full-screen "quad" trick vs. a raw image blit/copy) other than reading
   its actual `execute` lambda's C++ source. `rg::PassKind` only
   distinguishes Graphics vs. Compute (`RenderGraphTypes.h`, lines 342-345).
   `AtmosphereSkyBackgroundRenderer.cpp` (line 290) confirms
   `"DrawSkyBackground"` literally issues `vkCmdDraw(cmd, 3, 1, 0, 0)` — a
   real, hand-verified 3-vertex full-screen-triangle ("quad") draw, never a
   real per-object mesh draw and never a raw `vkCmdBlitImage`/
   `vkCmdCopyImage` either. This campaign needs a real, structural,
   name-free way to know this fact (see Locked Design Decision #3/#4 below)
   — PHASE1 adds it.
6. **A genuine `PassKind::Blit` third enumerator was explicitly considered
   and rejected** as this campaign's mechanism for the "BLIT" concept in the
   user's example — `RenderGraphTypes.h`'s own existing `PassKind` doc
   comment (lines 332-341) already anticipated "a future pure-blit/copy
   pass" as a candidate third `PassKind` value, but a real Vulkan blit
   (`vkCmdBlitImage`/`vkCmdCopyImage`) CANNOT be recorded inside a
   `vkCmdBeginRendering`/`vkCmdEndRendering` bracket — `RenderGraph::Execute()`
   would need a genuinely new, non-bracketed recording path to ever support
   one for real. That is real Render Graph ORCHESTRATOR work, explicitly out
   of scope for this campaign (mirrors `render-pass-1`'s own foundational
   rule: "the orchestrator itself... is NOT what's broken and is explicitly
   OUT OF SCOPE"). See Locked Design Decision #4.
7. **Existing unit test coverage hardcodes today's flat shape.**
   `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` has tests like
   `DrawSkyBackgroundLeafSurvivesAlongsideComputeDispatchSplit` (line 975)
   and `ViewRegionHasExactlyRenderOpaqueAndDrawSkyBackgroundWhenRenderTransparentAbsent`
   (line 1034) that assert `"DrawSkyBackground"` has no children and rely on
   exact `eventIndex`/`totalEventCount` numbers that this campaign's fix
   necessarily changes (every pass leaf now consumes 2 event indices — one
   for the pass row, one for its new child — instead of 1). Confirmed with
   the user: rewriting these tests to match the new shape is expected and
   accepted (Locked Design Decision #5).
8. `docs/conventions/frame-debugger.md` and `AGENTS.md`'s "Render Pass
   System"/"Frame Debugger" sections both currently describe the OLD flat
   `"DrawSkyBackground"` leaf shape and will go stale once this campaign
   ships — they need a documentation pass too (PHASE3).

## Step 3: The Plan — Phase Index

Each phase below is its own `PHASEn_*.md` file in this same folder. Work
through them in order — later phases assume earlier ones already landed.
Every phase file follows the same "Goal / Situation / Plan / Definition of
Done / What We Will NOT Do" shape `render-pass-1`'s own phase files already
use.

| Phase | File | One-line summary |
|---|---|---|
| 1 | `PHASE1_RENDER_PASS_DRAW_KIND_VOCABULARY.md` | New, purely-descriptive `rg::RenderPassDrawKind` enum (`DrawMesh`/`DrawQuad`/`Blit`) threaded through `PassRecord` → `AddRenderPass()` → `RenderGraphPassSnapshot`; `"DrawSkyBackground"` explicitly tagged `DrawQuad`. Zero orchestrator changes. |
| 2 | `PHASE2_FRAME_DEBUGGER_UNIFIED_PASS_OWNERSHIP_REWORK.md` | The actual bug fix: every real pass leaf (`Compute LUT` sub-passes, Pre/Post-GameView compute dispatches, `"DrawSkyBackground"`/future `"RenderTransparent"`) becomes a "v PassName" parent owning one real child event row. `"RenderOpaque"` untouched. Zero `FrameDebuggerPanel.cpp` changes needed. |
| 3 | `PHASE3_TEST_SUITE_MIGRATION_AND_DOCS_UPDATE.md` | Rewrite every existing test that hardcodes the old flat shape/old event-index numbers to match the new nested shape; add new tests for the new wrapping helper, dual-selectability, and `RenderPassDrawKind`-driven labeling; update `docs/conventions/frame-debugger.md` and `AGENTS.md`. |
| 4 | `PHASE4_FINAL_INTEGRATION_FULL_BUILD_AND_LIVE_VERIFICATION.md` | The ONLY phase that runs a full build + full `ctest` regression suite + a live, HTTP-driven Frame Debugger screenshot verification against the real running engine. Campaign completion report. |

## Locked Design Decisions (from user Q&A — do not re-litigate these)

1. **Scope: BROAD, not narrow.** Every real pass leaf in the Game View tree
   gets the "v PassName -> child event" treatment — not just
   `"DrawSkyBackground"`. This includes each individual Atmosphere `"Compute
   LUT"` pass (e.g. `"AtmosphereTransmittanceLutPass"`) and every Pre/Post-
   GameView compute dispatch, in addition to `"DrawSkyBackground"` itself.
   `"RenderOpaque"` is explicitly EXCLUDED from this rework — it already has
   the correct shape via its own real per-entity-children mechanism and must
   not be touched.
2. **Selectability: dual-role, kept.** The pass-level row (e.g.
   `"DrawSkyBackground"`, `"AtmosphereTransmittanceLutPass"`) stays
   independently selectable/inspectable in its own right, in ADDITION to its
   new child event row also being independently selectable — mirroring
   `"RenderOpaque"`'s own existing dual-role behavior exactly. (The
   alternative considered — turning the pass row into a pure,
   non-selectable group header like the `"Compute Dispatches"` bucket
   headers — was explicitly rejected.)
3. **Child event naming: agnostic, structural, never a pass-name string
   match.** The child row's label must be derived from a real, structural
   fact about what the pass does — never a hardcoded `pass.name ==
   "DrawSkyBackground"`-style check, and never using the word "Sky"
   anywhere in the label (the user was explicit about this: keep it
   pass-agnostic). Concretely:
   - Compute-kind pass → child labeled `"Compute Dispatch"` (unchanged,
     already correct/agnostic today).
   - Graphics-kind pass → child labeled `"Draw Mesh"`, `"Draw Quad"`, or
     `"Blit"`, chosen by that pass's own new `rg::RenderPassDrawKind` tag
     (PHASE1) — never by comparing against a literal pass name.
   - `"DrawSkyBackground"` itself is tagged `RenderPassDrawKind::DrawQuad`
     (a real, hand-verified fact: it issues a 3-vertex full-screen-triangle
     draw — see `AtmosphereSkyBackgroundRenderer.cpp` line 290 — the classic
     "full-screen quad via one oversized triangle" technique, never a real
     per-object mesh draw and never a raw image blit/copy).
4. **"Blit" is a real-but-currently-unused SCAFFOLD value on the new,
   purely-descriptive `RenderPassDrawKind` enum — NOT a new `PassKind`
   enumerator, and NOT a real orchestrator feature.** A genuine Vulkan blit/
   copy pass would need `RenderGraph::Execute()` to grow a whole new
   recording path outside the `vkCmdBeginRendering`/`vkCmdEndRendering`
   bracket every Graphics-kind pass uses today — real orchestrator work,
   explicitly out of scope (mirrors `render-pass-1`'s foundational rule).
   Adding `Blit` as an enumerator now (mirroring how `"RenderTransparent"`
   was added as a real-but-permanently-empty pass scaffold by
   `render-pass-1`'s own PHASE2) means a FUTURE campaign that adds a real
   blit/copy pass — and separately teaches the orchestrator how to record
   one — needs zero further Frame Debugger changes to have it show up
   correctly labeled.
5. **Breaking changes to tests: EXPLICITLY ALLOWED and expected.** Every
   test hardcoding the old flat shape, or an old `eventIndex`/
   `totalEventCount` number, is rewritten to match the new nested shape —
   confirmed directly with the user. Document every changed assertion
   plainly in PHASE3's own completion report, exactly like every
   `render-pass-1` phase already did for ITS breaking changes.
6. **Final verification**: the LAST phase (PHASE4) ONLY does a full build +
   `ctest` regression run AND a live, `gte_send_request`-driven Frame
   Debugger screenshot check of the real running engine, confirmed against
   this document's own target tree shape (Step 1, above).

## Cross-Cutting Rules For Every Phase (do not repeat verbatim in each
child, but every implementer must follow these)

- **No full build/regression test until PHASE4.** Every earlier phase does
  an INCREMENTAL compile check only — build just the `GreatTamanaEngine`
  target (`cmake --build build --target GreatTamanaEngine`), NOT the
  `GreatTamanaEngineTests` target, and NOT a full `ctest` run. PHASE2 in
  particular is expected to leave `GreatTamanaEngineTests` temporarily
  broken (failing to compile and/or failing assertions against the old
  shape) — this is a deliberate, accepted, DOCUMENTED intermediate state
  between phases of the SAME campaign, fixed by PHASE3, never swept under
  the rug.
- **Every phase ends with**: (a) an incremental compile check (narrowest
  target that proves the files touched actually compile), (b) a short
  Markdown completion report written into this SAME folder
  (`task_manager/render-pass-2/PHASEn_COMPLETION_REPORT.md`), (c) a git
  commit of the code changes + the report together.
- **Stay on branch `feature/render-pass-impl`.** Never switch branches.
  Always read `README.md` and `AGENTS.md` at the repo root before starting a
  phase, and always read the previous phase's own completion report first —
  it may carry a clue, a deviation, or an open question for the next phase
  to pick up.
- **`AGENTS.md`'s general house rules still apply in full**: Clean
  Architecture layering (Renderer never depends on ECS/Editor; only
  Application knows about both), RAII, everything lives in `namespace gte`
  (or `gte::rg` for Render Graph internals), and the Testability/
  Regression-Safety rules (extract Tier-1-testable pure logic wherever the
  problem allows it — PHASE1's `ToString(RenderPassDrawKind)` and PHASE2's
  new tree-wrapping helper are both excellent Tier-1 candidates; add/update
  a matching test file under `tests/` in the SAME change it's introduced,
  never as an afterthought — though see PHASE2's own note about
  deliberately deferring the BULK of test-suite fixing to PHASE3).
- **IMPORTANT — delegation discipline.** If, while executing ANY phase
  (this one included), you find yourself needing to hand off further work
  via `delegate_task`, you MUST explicitly instruct that delegated task, in
  its own prompt text, to use the `ask_questions` tool for any genuine
  design ambiguity it hits — and to, in turn, pass that SAME instruction on
  to anything IT further delegates. This rule propagates recursively,
  forever, down every level of delegation this campaign ever produces.
- **If you are genuinely unsure about a design choice this document (or
  your own phase file) does not already pin down, use `ask_questions` to
  ask the user directly rather than guessing.** Every phase file below has
  already resolved every design decision this document's own Q&A session
  covered; if you hit a NEW ambiguity these docs don't already answer, that
  is exactly when to stop and ask.
