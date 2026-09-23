# PHASE4 — Rewrite the Frame Debugger's Tree-Grouping Logic to be Fully Generic

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it first). **Depends on:** `PHASE1`, `PHASE2`,
`PHASE3` (the enum must already be trimmed and real tags must already be flowing through real
passes before this phase's rewrite makes sense).

---

## Step 1 — The Goal

`src/Editor/FrameDebuggerData.cpp` currently contains the ONLY real, behavioral consumer of
`RenderPassCategory::AtmosphereLut` left anywhere before this campaign — a hardcoded
`if (pass.category == rg::RenderPassCategory::AtmosphereLut)` branch deciding whether a
pre-GameView compute pass gets grouped under a "Compute LUT" heading. This phase replaces
that branch with a fully generic lookup against PHASE2's registry, with **one hard
requirement: the live, HTTP-captured Frame Debugger tree for a real running frame must look
EXACTLY the same after this phase as it did before this entire campaign started** — same
headings, same pass placement, same order, same event indices. This is a pure internal
refactor of HOW the grouping decision is made, never a change to WHAT it produces.

A second, equally important goal: prove, with an automated Tier-1 test, that this mechanism
is now genuinely generic — i.e. that a hypothetical THIRD Layer-2 module could register its
own heading and get correctly bucketed, with zero further `FrameDebuggerData.cpp` code
changes. This is the concrete, checkable proof of the source document's central claim.

---

## Step 2 — The Situation

Re-read `PHASE0_MASTER_STRATEGY.md` Step 2.3 for the exact two lines this phase touches
(~775, ~828) and the one loop this phase does NOT touch (post-GameView, ~872-893, which never
branched on `category` at all).

The current pre-GameView loop shape (`BuildRealFrameDebuggerSnapshot()`, re-read the live
function before editing — it has shifted across many prior campaigns and will keep shifting):

```cpp
FrameDebuggerEventNode computeLutGroup;
computeLutGroup.name = "Compute LUT";
computeLutGroup.isDrawCall = false;

FrameDebuggerEventNode preGameViewGroup;
preGameViewGroup.name = "Compute Dispatches (Pre-GameView)";
preGameViewGroup.isDrawCall = false;

for (int i = 0; i < pivotIndex; ++i) {
    const auto& pass = graphSnapshot.passesInExecutionOrder[i];
    if (pass.kind != rg::PassKind::Compute || pass.isCulled || pass.viewScope == rg::ViewScope::SceneView) {
        continue;
    }
    FrameDebuggerEventNode leaf = /* build leaf, consumes 2 eventIndex values */;
    if (pass.category == rg::RenderPassCategory::AtmosphereLut) {
        computeLutGroup.children.push_back(std::move(leaf));
    } else {
        preGameViewGroup.children.push_back(std::move(leaf));
    }
}
if (!computeLutGroup.children.empty()) { root.children.push_back(std::move(computeLutGroup)); }
if (!preGameViewGroup.children.empty()) { root.children.push_back(std::move(preGameViewGroup)); }
```

The required NEW shape must preserve the EXACT visible order ("Compute LUT" always listed
first when non-empty, THEN the generic fallback bucket when non-empty) while sourcing "which
bucket does this pass go in" from `rg::FindPassGroupIndexForTags(pass.tags)` instead of
`pass.category`. Because `rg::PassGroupLabelCount()`/registration order is DATA (only ONE
label, "Compute LUT", is registered in production today — by `AtmosphereLutRenderer`'s own
constructor, PHASE3), the rewritten loop must build a bucket per REGISTERED label
dynamically, not hardcode "exactly one special bucket named 'Compute LUT'" — otherwise the
"prove genericity" test in Step 3.4 below could never actually exercise a second bucket.

---

## Step 3 — The Plan

### 3.1 Rewrite the pre-GameView discovery loop

Replace the two named `FrameDebuggerEventNode` locals with a small, ordered, dynamically-sized
collection built from the registry BEFORE the loop starts:

```cpp
// One FrameDebuggerEventNode per CURRENTLY REGISTERED (tag -> heading) pair, in
// REGISTRATION ORDER - see RenderPassGroupRegistry.h (PHASE2). Core itself never
// hardcodes which headings exist; every heading comes from whichever Layer-2 module
// registered it (Atmosphere's own AtmosphereLutRenderer constructor registers "Compute
// LUT" today - see AtmosphereRenderPassTags.h - but this loop has zero awareness that
// "Atmosphere" is the module behind it).
std::vector<FrameDebuggerEventNode> labeledGroups;
labeledGroups.reserve(rg::PassGroupLabelCount());
for (std::size_t g = 0; g < rg::PassGroupLabelCount(); ++g) {
    FrameDebuggerEventNode group;
    group.name = rg::PassGroupLabelUiHeadingAt(g);
    group.isDrawCall = false;
    labeledGroups.push_back(std::move(group));
}

FrameDebuggerEventNode preGameViewGroup;
preGameViewGroup.name = "Compute Dispatches (Pre-GameView)";
preGameViewGroup.isDrawCall = false;

for (int i = 0; i < pivotIndex; ++i) {
    const rg::RenderGraphPassSnapshot& pass = graphSnapshot.passesInExecutionOrder[static_cast<std::size_t>(i)];
    if (pass.kind != rg::PassKind::Compute || pass.isCulled || pass.viewScope == rg::ViewScope::SceneView) {
        continue;
    }
    FrameDebuggerEventNode leaf =
        BuildComputeDispatchLeaf(pass, nextEventIndex++, FrameDebuggerStepPreviewKind::NotYetDrawn);
    leaf = WrapPassWithOwnedChildEvent(std::move(leaf), nextEventIndex++, "Compute Dispatch");

    const std::optional<std::size_t> groupIndex = rg::FindPassGroupIndexForTags(pass.tags);
    if (groupIndex.has_value()) {
        labeledGroups[*groupIndex].children.push_back(std::move(leaf));
    } else {
        preGameViewGroup.children.push_back(std::move(leaf));
    }
}

for (FrameDebuggerEventNode& group : labeledGroups) {
    if (!group.children.empty()) {
        root.children.push_back(std::move(group));
    }
}
if (!preGameViewGroup.children.empty()) {
    root.children.push_back(std::move(preGameViewGroup));
}
```

This reproduces today's exact production output byte-for-byte: exactly ONE label is
registered today ("Compute LUT", index 0), so `labeledGroups` has exactly one entry, appended
first (if non-empty) — identical order to today's hardcoded `computeLutGroup` then
`preGameViewGroup` sequence. Add `#include "../Renderer/RenderGraph/RenderPassGroupRegistry.h"`
to `FrameDebuggerData.cpp` (match this file's existing relative-include style — check its
current include block first).

### 3.2 Update stale doc comments

The multi-paragraph comment block immediately above this loop (currently explains "Step 3.2 -
the pre-view compute-dispatch discovery is now split into TWO groups... 'Compute LUT' (every
surviving... AtmosphereLut-category compute pass)...") is now factually WRONG — it describes
Core as knowing about `AtmosphereLut`/`GpuSkinning` by name, which is exactly the violation
this campaign removes. Rewrite it to describe the actual, current, generic mechanism: "this
loop buckets each surviving pre-GameView compute pass by whichever Frame-Debugger-heading
tag (if any) it carries, per the generic `RenderPassGroupRegistry` (see
`RenderPassGroupRegistry.h`) — Core has no knowledge of which specific Layer-2 module
registered which heading; today exactly one heading ('Compute LUT') happens to be registered,
by the Atmosphere feature, which is why this loop's real output today still shows exactly the
same two buckets it always has." Do the same for the smaller comment near the Debug-exclusion
check (~line 805-807) if it also references the now-deleted enumerators by name — that
specific EXCLUSION check itself (`pass.category == rg::RenderPassCategory::Debug`) is
untouched code, only its surrounding prose may need a small factual correction if it
mentions `AtmosphereLut`/`GpuSkinning` in passing.

**Also fix `src/Editor/FrameDebuggerData.h`'s own public doc comment** — this header carries a
whole ASCII-art tree-shape diagram (immediately above `BuildRealFrameDebuggerSnapshot()`'s own
declaration, re-read the live file to find its current line range — it sat at approximately
lines 446-462 at the time this phase document was written) that STILL literally says
`"Compute LUT" (AtmosphereLut-category compute passes before the pivot)`, `... one such
v-parent per surviving AtmosphereLut-category pass`, and `"Compute Dispatches
(Post-GameView)" (General-category compute passes after the view region)`. This is a
PUBLIC HEADER doc comment (more durable/authoritative-looking than the `.cpp`-local comment
above), and it is now just as factually wrong for the exact same reason — grouping is decided
by the generic tag/registry lookup, never by `RenderPassCategory`, once this phase lands.
Rewrite those three lines to describe the mechanism generically (e.g. `"Compute LUT" (every
compute pass before the pivot carrying a tag registered via RenderPassGroupRegistry — today
that is Atmosphere's own tag, but this header must never say so as if it were a permanent
rule)`, `... one such v-parent per surviving pass carrying that registered tag`, and
`"Compute Dispatches (Post-GameView)" (every compute pass after the view region, regardless of
tag — this group is never subdivided by tag)`) — do not leave this diagram naming
`AtmosphereLut`/`GpuSkinning`/`General` as if those enumerators still exist.

### 3.3 Finish migrating `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`

PHASE3 already made this file COMPILE (tags instead of the deleted category values) but left
its actual grouping-behavior assertions red pending this phase. Now:

- `MakeComputePass()` fixtures that set `.tags = kAtmosphereLutPassTag.bit;` — confirm each
  such test ALSO calls `rg::ResetPassGroupRegistryForTesting();` then
  `rg::RegisterPassGroupLabel(kAtmosphereLutPassTag, "Compute LUT");` before invoking
  `BuildRealFrameDebuggerSnapshot()` (production's own self-registration, PHASE3's
  `AtmosphereLutRenderer` constructor, never runs inside this Tier-1 test binary — no such
  object gets constructed here). Add a small local test-fixture helper function at the top of
  this file, e.g. `void EnsureAtmosphereLutGroupRegisteredForTesting()`, to avoid repeating
  the two-line reset+register boilerplate across every test that needs it — call it inside
  each such test's own body (do NOT rely on global `SetUp()`/`TearDown()` unless this file
  already uses a shared `::testing::Test` fixture class; check the file's existing structure
  first and match its established pattern).
- **Exactly THREE tests in this file need that helper call — not two.** A fresh read of the
  live file (confirmed at the time this phase document was written) shows the ONLY tests that
  set `.tags = kAtmosphereLutPassTag.bit;` on a fixture pass AND assert a `"Compute LUT"`
  heading in their expected output are: `MixedComputeLutAndPreGameViewCategoriesProduceBothGroupsInFixedOrder`,
  `OnlyAtmosphereLutCategoryPreGameViewPassProducesOnlyComputeLutGroup`, **AND**
  `ComputeLutSubPassAlsoOwnsAComputeDispatchChild` (this third one is easy to miss since it is
  physically far away from the other two in the file, near its own "Step 3.5 test 3" comment
  header — it asserts `lutGroup.name == "Compute LUT"` exactly like the other two, and WILL
  stay red without the same reset+register call). Re-run all three and confirm they now pass
  with zero further test-body changes beyond the reset+register calls (the actual node names/
  order/eventIndex assertions inside them should need NO changes at all — that is the whole
  point of the "byte-identical" acceptance bar).
- Consider renaming these three tests if "CategoriesProduce"/"Category" in their names (or in
  their own surrounding fixture-description comments, e.g. "one AtmosphereLut-category pass")
  is now misleading (e.g. `MixedComputeLutAndPreGameViewTagsProduceBothGroupsInFixedOrder`) —
  purely optional polish, do it if it is a cheap rename, skip it if it risks introducing an
  unrelated mistake under time pressure.

### 3.4 NEW test — prove the mechanism is genuinely generic (the concrete "unlocks future plugins" proof)

Add a brand-new test, e.g. `FrameDebuggerSnapshotBuilderTest.
ASyntheticThirdPartyTagAndHeadingGetsGroupedWithZeroProductionCodeAwareness`:

1. `rg::ResetPassGroupRegistryForTesting();`
2. Register a synthetic, TEST-LOCAL-ONLY tag/heading pair that has no relationship to any
   real feature, e.g. `constexpr rg::RenderPassTag kFakeFuturePluginTag{ 1ull << 40 };` then
   `rg::RegisterPassGroupLabel(kFakeFuturePluginTag, "Totally Fake Future Plugin Group");`
   (a large, deliberately unused bit, unlikely to collide with any real production tag bit
   chosen so far in this campaign — confirm against PHASE3's actual chosen bits before
   picking this literal).
3. Build a `graphSnapshot` with ONE pre-GameView compute pass whose `.tags =
   kFakeFuturePluginTag.bit;` (use `MakeComputePass()`), plus the usual `"RenderOpaque"`
   pivot pass.
4. Call `BuildRealFrameDebuggerSnapshot()` and assert the resulting tree has a root child
   literally named `"Totally Fake Future Plugin Group"` containing that one pass — proving
   `FrameDebuggerData.cpp`'s own compiled code never once mentions this string, this tag, or
   this test, and still correctly buckets it purely from registry DATA.
5. This test is the literal, automated, permanent regression guard for the source strategy
   document's own central claim (Section 1.1): "a future plugin... gets equivalent treatment"
   without editing Core — if this test ever starts failing, the boundary has regressed.

### 3.5 Build + targeted test run

Incremental build; run `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`'s full test
suite specifically (targeted, not the full `ctest` run — that is PHASE5's job) and confirm
100% pass, including the new synthetic-tag test from 3.4.

### 3.6 Report

Write `PHASE4_COMPLETION_REPORT.md`; commit via `git_add`/`git_commit`.

### Acceptance bar for this phase

- `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` — 100% pass, zero test deleted/skipped
  without an explicit, justified reason recorded in the completion report.
- The new synthetic-tag genericity test (3.4) exists and passes.
- No string literal naming a specific Layer-2 feature ("Atmosphere", "GpuSkinning", "LUT",
  "Skinning") remains anywhere in `FrameDebuggerData.cpp`'s own compiled logic (comments
  describing HISTORY are fine; executable code/string literals driving behavior are not —
  the ONLY heading string this file should ever construct itself is the generic fallback,
  `"Compute Dispatches (Pre-GameView)"`, which is a Core-level concept, not a feature name).
- `src/Editor/FrameDebuggerData.h`'s own ASCII tree-shape diagram (see 3.2 above) no longer
  says "AtmosphereLut-category"/"General-category" as if those enumerators still exist.
- A live, HTTP-driven smoke test (`gte_send_request`, e.g. `GET /frame_debugger/open`, `GET
  /frame_debugger/enable`, `GET /frame_debugger/capture`, `GET /frame_debugger/state`) against
  a running instance shows the exact same tree shape/headings as a pre-campaign baseline — if
  you cannot easily capture a true "before" baseline at this point in the campaign (PHASE3
  already changed the underlying data model), it is acceptable to defer the FULL live-compare
  smoke test to `PHASE5` (which explicitly owns end-to-end verification) — but do run at
  least one live capture here as a basic sanity check that the Editor doesn't crash/misbehave
  with real GPU-produced data, not just synthetic Tier-1 fixtures.
