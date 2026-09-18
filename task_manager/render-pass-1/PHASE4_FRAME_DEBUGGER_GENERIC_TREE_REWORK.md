# PHASE4: Frame Debugger Generic Tree Rework (remove the `"GameView"`-literal search + the Sky Background hack)

_Child of `PHASE0_MASTER_STRATEGY.md`. Depends on `PHASE1`, `PHASE2`, and
`PHASE3` already being merged. Part of the `render-pass-1` campaign. This
is flagged by `PHASE0_MASTER_STRATEGY.md` as one of the HEAVIEST, highest-
risk phases in this campaign — it touches the Frame Debugger's core
snapshot builder, its HTTP contract, and several existing test files.
Read this phase file TWICE before starting, and re-read
`docs/conventions/frame-debugger.md` in full before touching
`FrameDebuggerData.cpp`._

_**PRE-CHECK NOTE (added by the `render-pass-1` PHASE4 pre-check pass,
before any implementation happened)**: this file was reviewed against the
REAL, current source (`FrameDebuggerData.h/.cpp`,
`FrameDebuggerCapture.h/.cpp`, `Application.cpp`, `RenderPasses.cpp`,
`docs/conventions/frame-debugger.md`) and against `PHASE5`'s own text. One
real, confirmed gap was found and fixed in place below (Step 2's new
bullet + Step 3.3's rewrite + Step 3.6's new test + Definition of Done):
`AddFrameDebuggerReplayPasses()` is CONFIRMED (by reading `Application.cpp`
directly) to be declared strictly between the Game-View draw passes and
the Aerial Perspective Composite pass — i.e. structurally INSIDE this
phase's own "view region" walk — so, without an explicit fix, THIS phase's
own live HTTP verification (enable + capture + screenshot) would show N
spurious extra tree leaves for the Frame Debugger's own internal replay
passes the very first time it is exercised. See the new material below;
`PHASE5_REMAINING_PASSES_MIGRATION.md` was also updated to point back
here instead of leaving this ambiguous ("add it now if missing") across
two phases. Nothing else in this file needed correction — the rest of the
plan was verified accurate against the real, current source._

## Step 1: The Goal

Rework `BuildRealFrameDebuggerSnapshot()`
(`src/Editor/FrameDebuggerData.cpp`) so the whole Frame Debugger event
tree is built GENERICALLY from real, structural pass metadata
(`PassKind`, `RenderPassCategory`, `ViewScope`, execution order) —
removing BOTH (a) the hardcoded search for a pass literally named
`"GameView"` (which no longer exists as of PHASE2 — it is now
`"RenderOpaque"`) and (b) the `isSkyBackgroundDraw`/
`RecordSkyBackgroundDraw()` special-case hack entirely (Sky Background is
now a real, separate, generically-discoverable pass as of PHASE2/this
phase). The end result, for the test scene described in the original
task, is the tree the user actually asked for:

```
"Game View"                                   (root label - cosmetic, no longer tied to a literal pass name -
                                                confirmed: the real root node's own literal `name` string is
                                                "Game View", WITH a space, exactly as it is today - see
                                                BuildRealFrameDebuggerSnapshot()'s existing `root.name = "Game View";`
                                                line, which this phase does NOT change)
  |-- "Compute LUT"                           (AtmosphereLut-category compute passes before the view root)
  |     |-- AtmosphereTransmittanceLutPass
  |     |-- AtmosphereMultiScatteringLutPass
  |     |-- AtmosphereSkyViewLutPass (GameView-scoped instance)
  |     |-- AtmosphereAerialPerspectiveVolumePass (GameView-scoped instance)
  |     |-- AtmosphereAerialPerspectiveVolumeDebugSlicePass (if present)
  |-- "Compute Dispatches (Pre-GameView)"      (any OTHER category compute pass before the view root - e.g. GPU Skinning - only if non-empty)
  |-- "RenderOpaque" leaf                      (per-entity children - UNCHANGED mechanism from today)
  |-- "DrawSkyBackground" leaf                 (NEW - a real, individually selectable leaf, no more hack)
  |-- "RenderTransparent" leaf                 (only appears once it is ever a real, non-culled pass - today it never is, see PHASE2)
  |-- "Compute Dispatches (Post-GameView)"      (General-category compute passes after the view root - e.g. AerialPerspectiveComposite, ComputeBlurValidation - only if non-empty)
```

**IMPORTANT — Frame-Debugger-internal passes never leak into the tree,
INCLUDING inside the view region above.** `AddFrameDebuggerReplayPasses()`
(`src/Application/RenderPasses.cpp`) declares its own N debug-only replay
passes with real `PassKind::Graphics`, real `ViewScope::GameView`, at a
real execution-order position strictly between the view passes above and
the Aerial Perspective Composite pass (confirmed by direct inspection of
`Application.cpp` — see Step 2's new bullet below) — i.e. structurally
inside the exact region the "view-region walk" in Step 3.3 sweeps over.
These must NEVER appear as extra leaves in either diagram above, on any
captured frame, including one where a replay actually happened this
session (which is exactly when the Frame Debugger's own live-HTTP
Definition-of-Done check below captures a frame). See Step 3.3's rewrite.

## Step 2: The Situation

- `BuildRealFrameDebuggerSnapshot()` (`src/Editor/FrameDebuggerData.cpp`)
  today: finds the pass whose `name == "GameView"` in
  `graphSnapshot.passesInExecutionOrder` (returns an empty
  `FrameDebuggerSnapshot{}` if none exists); uses that pass's own
  execution-order INDEX as the pivot for splitting every surviving,
  non-`SceneView`-scoped, `isComputePass == true` (PHASE1 renames this to
  `kind == PassKind::Compute`) pass into `"Compute Dispatches
  (Pre-GameView)"` (index strictly before) vs. `"(Post-GameView)"` (index
  strictly after); builds exactly ONE leaf for the `"GameView"` pass
  itself, with one child leaf per `FrameDebuggerCaptureContext::
  DrawRecords()` entry (via `BuildGameViewDrawRecordLeaf()`, which
  branches on `record.isSkyBackgroundDraw` to build a differently-shaped
  leaf with no fabricated "Entity" row for the sky).
- `FrameDebuggerCaptureContext::DrawRecords()`/`FrameDebuggerDrawRecord`
  (`src/Editor/FrameDebuggerCapture.h/.cpp`) carries the
  `isSkyBackgroundDraw` bool and the `RecordSkyBackgroundDraw()` method
  that fabricates a `FrameDebuggerDrawRecord` for the sky draw — this
  mechanism becomes fully redundant once `"DrawSkyBackground"` is its own
  real pass (PHASE2 already made the pass real; this phase is what
  finally lets the Frame Debugger stop needing the fabricated record at
  all).
- `RenderGraphPassSnapshot` (after PHASE1) carries `kind` (`PassKind`) and
  `category` (`RenderPassCategory`), both copied through for every
  surviving AND culled pass, exactly like `viewScope` already is.
- The existing "never build an empty, misleading group" rule (see
  `docs/conventions/frame-debugger.md`'s own repeated emphasis on this)
  MUST be preserved for every group this phase adds/renames, including
  the new `"Compute LUT"` group and the `"RenderTransparent"` leaf itself
  (which, per PHASE2, is a pass that literally never exists in the graph
  today — so it must never appear as an empty/fake leaf; it should simply
  not appear at all until a future campaign makes it real).
- `docs/conventions/frame-debugger.md`'s own `## Testing this feature`
  section lists every existing test file this logic is covered by:
  `tests/Editor/FrameDebuggerDataTests.cpp`,
  `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`,
  `tests/Editor/FrameDebuggerCaptureTests.cpp`. All three will need
  updates.
- **CONFIRMED, pre-check-verified fact (real ordering of
  `AddFrameDebuggerReplayPasses()`)** — reading `Application.cpp` directly
  (its Game-View `if (gameTarget != nullptr) { ... }` block) shows the
  real declaration order is: `AddGameViewPass(...)` (post-PHASE2:
  `AddRenderOpaquePass()`/`AddDrawSkyBackgroundPass()`/
  `AddRenderTransparentPass()`), THEN, conditionally (only on an armed,
  explicit-capture-trigger frame),
  `AddFrameDebuggerReplayPasses(b, m_game, m_renderer, aspect, objectCount,
  gpuSkinningBuffers, recordGameSkyBackground, *gameTarget,
  *frameDebuggerCapture)`, THEN `AddAtmosphereCompositePass(...)`. Each of
  the N replay passes is declared via `builder.AddPass(passName,
  rg::ViewScope::GameView, setup, execute)` (`RenderPasses.cpp`) — real
  `PassKind::Graphics` (the default for plain `AddPass()`), real
  `ViewScope::GameView`, `RenderPassCategory::General` (the default —
  PHASE5 was originally the phase that planned to tag this `Debug`).
  **This means these N passes sit, today, structurally INSIDE the exact
  index range Step 3.3's "view region" walk sweeps over** (from the
  `"RenderOpaque"` pivot through the pass immediately before
  `AddAtmosphereCompositePass()`'s own pass). `PHASE5_REMAINING_PASSES_MIGRATION.md`
  already flags this exact cross-phase dependency risk explicitly
  ("if [PHASE4's own walk] does not yet exist, add it now, as part of
  THIS phase") but leaves it as a conditional, easy-to-miss aside in a
  LATER phase rather than a locked requirement in THIS one — since a
  pass's `category` defaults to `General` (not `Debug`) until something
  explicitly tags it, simply adding a `category != RenderPassCategory::Debug`
  guard in THIS phase's own walk would be a NO-OP right up until PHASE5
  actually lands and stamps that tag — i.e. this phase's OWN live
  verification (Definition of Done, below) would still see the bug on the
  very frame it captures to prove the new tree shape. **Fix, locked here**:
  this phase (PHASE4) pulls forward the one, narrow piece of PHASE5's
  migration scope that this phase's own correctness genuinely depends on
  — migrating `AddFrameDebuggerReplayPasses()`'s own pass declaration onto
  `AddRenderPass()` with `RenderPassCategory::Debug` — so the exclusion
  guard this phase adds to its own walk is not just present but ACTUALLY
  EFFECTIVE the moment this phase lands. See Step 3.3 and Step 3.6 below;
  `PHASE5_REMAINING_PASSES_MIGRATION.md` has been updated to point back
  here instead of re-describing this as still-open/ambiguous.

## Step 3: The Plan

### 3.1 — Generalize "find the view root pass"

Replace the hardcoded `FindPassByName(graphSnapshot, "GameView")`-style
lookup with a lookup for `"RenderOpaque"` instead (this is now the ONE
pass whose own execution-order index is the correct pivot for Pre/Post
grouping, and the one pass whose `DrawRecords()` entries become its
per-entity children — this part of the mechanism is otherwise UNCHANGED
from today, just retargeted at the new pass name). Keep the existing
"return an empty `FrameDebuggerSnapshot{}` if this pass doesn't exist at
all" fallback behavior (mirrors today's exact `"GameView"`-not-found
behavior).

### 3.2 — Split the Pre-view compute group into two, by category

Where today there is one loop building `"Compute Dispatches
(Pre-GameView)"` from every surviving, non-`SceneView`, `isComputePass`
pass before the pivot index, build TWO groups instead, in this fixed
order (see the Locked Design Decision below for why FIXED order, not
interleaved-by-real-index):

1. `"Compute LUT"` — every surviving, non-`SceneView`-scoped pass with
   `kind == PassKind::Compute && category == RenderPassCategory::AtmosphereLut`
   and an execution-order index BEFORE the `"RenderOpaque"` pivot.
2. `"Compute Dispatches (Pre-GameView)"` — every surviving, non-
   `SceneView`-scoped pass with `kind == PassKind::Compute && category !=
   RenderPassCategory::AtmosphereLut` (i.e. `General`/`GpuSkinning`/
   `Debug`) and an execution-order index BEFORE the pivot.

Each group is added to the tree ONLY if it has at least one real child
this frame (unchanged "never an empty group" rule). `"Compute LUT"` is
listed FIRST (matching the user's own example ordering intent), then
`"Compute Dispatches (Pre-GameView)"` second.

### 3.3 — The view-root leaves become a flat, ordered sibling list

Replace the single `"GameView"` leaf (with its per-entity children) with
an ORDERED walk over every surviving, non-`SceneView`-scoped, non-`Debug`-
category, `kind == PassKind::Graphics` pass in the "view region" — DO NOT
hardcode literal pass names; walk the snapshot generically by
`kind`/`viewScope`/`category`/index so a FUTURE fourth graphics pass
inserted here is automatically picked up with zero further Frame Debugger
code changes, mirroring this whole tree's own "generic discovery, never a
hardcoded name list" philosophy that already governs compute-pass
discovery. **Concrete, bounded algorithm** (spelled out explicitly here
because "walk generically" alone is not precise enough for an
implementer to act on unambiguously — see the pre-check note at the top
of this file):

1. Start at the `"RenderOpaque"` pivot's own execution-order index.
2. Walk forward one index at a time. For each pass:
   - If its `kind == PassKind::Compute` and it is a genuine SURVIVOR
     (`!isCulled`) — regardless of its `viewScope`/`category` — STOP the
     walk entirely; this index (and everything from here on) belongs to
     the existing `"Compute Dispatches (Post-GameView)"` discovery in
     3.2's sibling logic, not to this walk. (In today's real engine this
     is always the Aerial Perspective Composite pass, but this rule is
     deliberately name-free.)
   - Otherwise, if its `kind == PassKind::Graphics`:
     - If it is culled, or `viewScope == ViewScope::SceneView`, or
       `category == RenderPassCategory::Debug` — SKIP it (a `continue`,
       NOT a `break`: keep walking forward, this pass is simply not part
       of this tree at all) — this is the guard that keeps
       `AddFrameDebuggerReplayPasses()`'s own N replay passes (see Step
       2's new bullet above) from ever appearing as spurious extra
       leaves, on ANY captured frame, including one where a replay
       actually ran.
     - Otherwise, build a real leaf for it (see below) and continue the
       walk.
3. The walk naturally ends at the end of `passesInExecutionOrder` if no
   Compute-kind survivor is ever found after the pivot (a captured frame
   taken before the composite pass has ever run this session).

In practice today this produces exactly `"RenderOpaque"`,
`"DrawSkyBackground"`, and (once real, per PHASE2) `"RenderTransparent"`,
in that real execution order, with `AddFrameDebuggerReplayPasses()`'s own
N passes correctly skipped over (never stopping the walk, never adding a
leaf).

Per-leaf construction rules:

- The FIRST such pass (`"RenderOpaque"`) keeps its existing "one child
  leaf per `DrawRecords()` entry" mechanism — but drop the
  `isSkyBackgroundDraw` branch from `BuildGameViewDrawRecordLeaf()`
  entirely (dead code after this phase — see 3.4). **Also rename this
  leaf's per-entity children's own `FrameDebuggerEventDetails::passName`
  literal from `"GameView (Entity Draw)"` to `"RenderOpaque (Entity
  Draw)"`** — this string is a real, user-visible/HTTP-consumed fact (the
  Inspector's "Pass" row), and leaving it reading `"GameView..."` after
  the pass itself is renamed to `"RenderOpaque"` would be a stale,
  factually-wrong leftover, not a cosmetic nicety. (The original
  string-collision reason this field had to differ from the parent
  leaf's own literal name — avoiding `FrameDebuggerPanel`'s old
  `isViewingGameViewLeaf` exact-string check — no longer even applies,
  since `frame-debugger-7` already replaced that check with the
  structural `FrameDebuggerStepPreviewKind` field; the rename is purely
  about keeping the displayed fact honest, not about avoiding a
  collision.)
- Every OTHER such pass (`"DrawSkyBackground"`, and, once real,
  `"RenderTransparent"`) becomes an ordinary, childless leaf, built by a
  NEW small leaf-building helper (mirroring `BuildComputeDispatchLeaf()`'s
  own overall shape, but for a `Graphics`-kind pass): pass name, real/
  aggregate draw stats from `RenderGraphPassSnapshot::stats`, and real
  blend/Z/stencil rows. For `"DrawSkyBackground"` specifically, reuse
  `DescribeSkyBackgroundPipelineState()` verbatim (`FrameDebuggerCapture.h/.cpp`
  — it already hand-transcribes the correct `Depth Test = Equal`/`Depth
  Write = Off` values); a real future `"RenderTransparent"` leaf's own
  blend/Z/stencil source is explicitly OUT OF SCOPE for this phase (see
  "What We Will NOT Do") since the pass itself never exists today.
  **`stepPreviewKind` for these two leaf kinds** (not previously
  specified anywhere in this document — resolved here to remove
  ambiguity): both get `FrameDebuggerStepPreviewKind::PreComposite`,
  exactly like the `"RenderOpaque"` leaf itself gets today. Reasoning:
  there is only ONE retained whole-frame `preview` texture representing
  "the Game View right after the whole view region finishes, before the
  atmosphere composite pass runs" — unlike the per-OBJECT leaves (which
  each get their own genuinely distinct retained replay texture via
  `FrameDebuggerHistoryEntry::perObjectStepPreviews`), there is no
  per-Graphics-pass-distinct retained image for `"DrawSkyBackground"`/
  `"RenderTransparent"` today, so `PreComposite` (not a new, more granular
  kind) is the correct, honest choice — do not invent a new
  `FrameDebuggerStepPreviewKind` enumerator for this; it would be over-
  engineering for a distinction no retained texture actually supports.
- Rename the tree row label from `"GameView"` to `"RenderOpaque"` for
  this specific leaf (the pass's own real name) — the TREE ROOT node
  itself (the very top of the tree) stays a cosmetic label meaning "the
  Game View capture as a whole" and is UNCHANGED (it was never itself
  tied to any one pass's literal name to begin with — confirmed: the real
  root node's own literal `name` is `"Game View"`, WITH a space, per
  `BuildRealFrameDebuggerSnapshot()`'s existing `root.name = "Game View";`
  line — re-read `FrameDebuggerSnapshot::rootNodes`'s own shape to
  reconfirm this before assuming otherwise, since this document's own
  ASCII diagrams write it without a space purely as a conceptual label).

### 3.3b — Pull forward one narrow piece of PHASE5's scope: tag the Frame Debugger replay passes `Debug` NOW

As established in Step 2's new bullet above, this phase's own correctness
(and its own live-verification Definition of Done) depends on
`AddFrameDebuggerReplayPasses()`'s N passes actually being excluded by
the `category == RenderPassCategory::Debug` guard added in 3.3 — which
requires them to actually BE tagged `Debug`, not merely default
`General`. Therefore, as PART OF THIS PHASE (not deferred to PHASE5):

- In `AddFrameDebuggerReplayPasses()` (`src/Application/RenderPasses.cpp`),
  replace `builder.AddPass(passName, rg::ViewScope::GameView, setup,
  execute)` with `builder.AddRenderPass(passName, rg::PassKind::Graphics,
  rg::ViewScope::GameView, rg::RenderPassCategory::Debug, setup,
  execute)`. Same `name`/`setup`/`execute`, zero behavior change beyond
  the new stamped metadata — identical in spirit to every other
  migration PHASE3/PHASE5 perform elsewhere in this campaign.
- This does NOT expand this phase's scope to the REST of PHASE5's list
  (GPU Skinning, Present, Compute Blur Validation stay PHASE5's job,
  unchanged) — only this one call site, because only this one call site's
  mistagging would otherwise break THIS phase's own tree/verification.
- `PHASE5_REMAINING_PASSES_MIGRATION.md` has been updated (see that file)
  to state this migration already landed here, so PHASE5 only needs to
  CONFIRM it (via its own grep audit, 3.5) rather than redo it.

### 3.4 — Delete the Sky Background special-case hack

Remove `FrameDebuggerDrawRecord::isSkyBackgroundDraw` and
`FrameDebuggerCaptureContext::RecordSkyBackgroundDraw()` entirely
(`src/Editor/FrameDebuggerCapture.h/.cpp`). Remove the corresponding call
site PHASE2 added inside `AddDrawSkyBackgroundPass()`'s `execute` lambda
(`src/Application/RenderPasses.cpp`) — the sky draw no longer needs to
manually fabricate a draw record at all; it is now automatically visible
as its own real pass leaf via the generic mechanism in 3.3. Remove the
now-dead `isSkyBackgroundDraw`-branching code inside
`BuildGameViewDrawRecordLeaf()` (`FrameDebuggerData.cpp`) — that function
now ONLY ever builds a per-entity leaf (real `Entity`, real name), since
the sky is no longer one of `"RenderOpaque"`'s own `DrawRecords()`
entries at all (it never draws through `RenderSystem::Draw()` — it never
was routed through `DrawRecords()` for entity-attribution purposes to
begin with, only for this now-removed cosmetic-leaf-fabrication purpose).

### 3.5 — `"RenderTransparent"` visibility

Since `AddRenderTransparentPass()` (PHASE2) never actually declares a
pass today, it simply never appears in `graphSnapshot.passesInExecutionOrder`
at all — the generic walk in 3.3 naturally produces zero leaf for it,
with NO special-casing required. Confirm this with a test case
(3.6) asserting a snapshot with no `"RenderTransparent"` pass present at
all produces a 3-leaf view region (`RenderOpaque`, `DrawSkyBackground`,
and nothing else) — this is the concrete proof this mechanism is
genuinely generic, not just "generic in theory."

### 3.6 — Tests

Update `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`'s
hand-fabricated `RenderGraphSnapshot` fixtures: rename every
`"GameView"` pass fixture to `"RenderOpaque"`, add new fixture cases for
`"DrawSkyBackground"` (a `Graphics`-kind pass with no draw records of its
own) and at least 2 `AtmosphereLut`-category compute passes (to prove the
`"Compute LUT"` vs. `"Compute Dispatches (Pre-GameView)"` split works
correctly against a MIXED pre-view compute list — e.g. one
`AtmosphereLut` pass plus one `GpuSkinning`-category pass in the same
fixture, asserting BOTH groups appear, each with exactly the right
children).

**NEW test, required by this phase's own 3.3/3.3b fix above** — add a
fixture with a `Graphics`-kind, `ViewScope::GameView`,
`RenderPassCategory::Debug` pass positioned in `passesInExecutionOrder`
BETWEEN `"DrawSkyBackground"` and the eventual Post-GameView compute pass
(mirroring `AddFrameDebuggerReplayPasses()`'s own real, confirmed
position — see Step 2's new bullet) and assert it produces ZERO extra
leaves in the view region (still exactly `RenderOpaque`/
`DrawSkyBackground`, nothing else) — this is the concrete regression test
proving the Frame Debugger's own internal replay passes can never leak
into the tree, which is exactly what this phase's pre-check found was
otherwise at real risk of happening on this phase's own first live
verification run.

Update `tests/Editor/FrameDebuggerCaptureTests.cpp` to remove any
coverage of the deleted `isSkyBackgroundDraw`/
`RecordSkyBackgroundDraw()` API. Update `tests/Editor/FrameDebuggerDataTests.cpp`
if it references the old `"GameView"` literal anywhere (it does — its
existing "nested shape" test builds a hand-fabricated parent leaf with
`passName = "GameView"` and a child with `passName = "GameView (Entity
Draw)"`; update both to `"RenderOpaque"`/`"RenderOpaque (Entity Draw)"`
respectively, per 3.3's rename above). Grep the whole `tests/` tree for
the literal string `"GameView"` and `isSkyBackgroundDraw` to make sure
nothing is missed.

### 3.7 — HTTP/network contract audit

`docs/conventions/networking.md` and `tests/Network/NetworkRoutesTests.cpp`
should be checked for any literal `"GameView"`-as-a-pass-name assumption
(as opposed to `"GameView"`-as-a-RenderTexture-name, which is UNCHANGED —
see PHASE0 Step 2 point 4's explicit distinction between the texture
import name and the pass name; confirmed by this phase's own pre-check:
every real `"GameView"` string literal found under `src/` outside
`FrameDebuggerData.cpp`/its own tests is a RenderTexture/debug-texture
name — e.g. `Application.cpp`'s `b.ImportTexture("GameView", ...)`,
`ImGuiEditorLayer.cpp`'s `CreateRenderTexture(..., "GameView", ...)`,
`AtmospherePanel.cpp`'s `ValidateAerialPerspectiveSkyPurity(..., "GameView",
"GameViewComposited")` — none of these name a render-graph PASS and none
need to change). The `/frame_debugger/*` HTTP routes themselves
(`FrameDebuggerCommandBridge`) are index-based (`select_event?index=N`)
and name-agnostic — confirmed by this phase's own pre-check (zero hits
for `"GameView"` in `tests/Application/FrameDebuggerCommandBridgeTests.cpp`)
— they need NO changes at all.

## Definition of Done

- `BuildRealFrameDebuggerSnapshot()` builds the tree shape described in
  Step 1 above, generically, from `PassKind`/`RenderPassCategory`/
  `ViewScope`/execution order — zero hardcoded pass-name string literals
  anywhere in this function except the one `"RenderOpaque"` pivot lookup
  (down from the prior single `"GameView"` lookup — a lateral rename, not
  a new hardcoding).
- `isSkyBackgroundDraw`/`RecordSkyBackgroundDraw()` no longer exist
  anywhere in the repo (confirmed via `search_in_dir`).
- `AddFrameDebuggerReplayPasses()` declares its passes via
  `builder.AddRenderPass(..., rg::RenderPassCategory::Debug, ...)` (3.3b)
  — confirmed via `search_in_dir` that this call site no longer calls
  plain `builder.AddPass(...)`.
- Every updated/added test in `tests/Editor/FrameDebugger*Tests.cpp`
  passes, INCLUDING the new 3.6 test proving a `Debug`-category Graphics
  pass sitting inside the view region produces zero extra leaves.
- Live, HTTP-driven confirmation (via `run_app_background` +
  `gte_send_request`): open the Frame Debugger
  (`GET /frame_debugger/open`), enable it
  (`GET /frame_debugger/enable?value=true`), capture
  (`GET /frame_debugger/capture`), and take a `/get_swapchain` screenshot
  showing the new tree shape with `"RenderOpaque"` and `"DrawSkyBackground"`
  as separate, selectable rows, and (if the running test scene has an
  atmosphere sun) a `"Compute LUT"` group distinct from any
  `"Compute Dispatches (Pre-GameView)"` group — **and explicitly confirm
  NO extra/spurious rows appear for the Frame Debugger's own N internal
  replay passes**, even though this exact HTTP sequence (enable + capture)
  is precisely what triggers those N passes to be declared this same
  frame (see Step 2's new bullet) — this is the concrete, live proof the
  3.3/3.3b fix actually works, not just the Tier-1 test in isolation.
  Compare this screenshot against the reference screenshot in the
  original task description.
- Incremental compile + the full `tests/Editor/` test subset (not the
  WHOLE suite — see PHASE0's "no full regression test until PHASE7" rule)
  passes; completion report + git commit as usual.

## What We Will NOT Do

- Do NOT relax the Frame Debugger's existing "Scope is Game View ONLY,
  permanently" rule — Scene View passes stay excluded exactly as they are
  today (the `viewScope != ViewScope::SceneView` filter is UNCHANGED).
- Do NOT change `FrameDebuggerCommandBridge`'s HTTP route shapes/names —
  every route is already index-based, not pass-name-based.
- Do NOT attempt true per-pass "stop"/breakpoint execution control — that
  remains explicitly out of scope for this entire campaign, matching
  every prior `frame-debugger-*` campaign's own documented "Still-
  deferred future work" note.
- Do NOT touch `ChooseFrameDebuggerPreviewSource()`/
  `FrameDebuggerStepPreviewKind`'s own picking rule — it already operates
  purely on execution-order position relative to the composite pass, not
  on any specific pass NAME, so it needs no changes for this rework
  (confirm this claim by re-reading its own doc comment before assuming
  it, rather than skipping the check). Do NOT add a new
  `FrameDebuggerStepPreviewKind` enumerator for `"DrawSkyBackground"`/
  `"RenderTransparent"` — both reuse the existing `PreComposite` value
  (see 3.3).
- Do NOT expand this phase's migration scope to GPU Skinning, `"Present"`,
  or Compute Blur Validation — those stay PHASE5's job, unchanged. The
  ONLY piece of PHASE5's original scope this phase pulls forward is
  tagging `AddFrameDebuggerReplayPasses()` itself `RenderPassCategory::Debug`
  (3.3b), because — and only because — this phase's own correctness and
  live verification genuinely depend on it; do not use this as a
  precedent for pulling any other PHASE5 item forward too.
- Do NOT implement a real `"RenderTransparent"` blend/Z/stencil source —
  that pass never exists in the graph today (PHASE2), so there is nothing
  real to source it from; a future transparency campaign's own job.
