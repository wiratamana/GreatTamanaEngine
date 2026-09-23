# PHASE4 — Rewrite the Frame Debugger's Tree-Grouping Logic to be Fully Generic — COMPLETION REPORT

**Parent:** `PHASE0_MASTER_STRATEGY.md`. **Phase doc followed:**
`PHASE4_FRAME_DEBUGGER_GENERIC_GROUPING.md`. **Depends on:** PHASE1 (real `tags` threading),
PHASE2 (`RenderPassGroupRegistry`), PHASE3 (`RenderPassCategory` trimmed to `{General, Debug}`,
real tags flowing through the 7 real production call sites, `AtmosphereLutRenderer`'s own
constructor registering `"Compute LUT"`) — all confirmed already landed by reading
`PHASE1_COMPLETION_REPORT.md`/`PHASE2_COMPLETION_REPORT.md`/`PHASE3_COMPLETION_REPORT.md` and the
live source tree before starting.

## Summary

Implemented exactly what the phase document's Step 3 specified: `src/Editor/FrameDebuggerData.cpp`'s
pre-GameView compute-dispatch discovery loop no longer branches on `RenderPassCategory` at all
(the enum only has `{General, Debug}` left, per PHASE3) — it now builds one `FrameDebuggerEventNode`
bucket per currently-registered `(tag -> heading)` pair from `gte::rg::RenderPassGroupRegistry`
(PHASE2), in registration order, and looks up which bucket a surviving pre-GameView compute pass
belongs to via `rg::FindPassGroupIndexForTags(pass.tags)` — falling back to the generic
`"Compute Dispatches (Pre-GameView)"` bucket when a pass carries no registered tag. This is a pure
internal refactor of **how** the grouping decision is made; the observable output is unchanged (and
was confirmed unchanged both by the 3 previously-red PHASE3 tests going green again with zero
behavior-assertion changes, and by a live HTTP-driven capture against a real running frame — see
below).

The old, PHASE3-left `if (false) { computeLutGroup... } else { preGameViewGroup... }` placeholder
(a deliberate, honestly-documented compile-only stopgap explained in
`PHASE3_COMPLETION_REPORT.md`) is now gone, replaced with the real, generic lookup. A brand-new
Tier-1 test proves the mechanism is genuinely generic by registering a synthetic, test-local-only
tag/heading pair with zero relationship to any real feature and confirming it is correctly bucketed
by `FrameDebuggerData.cpp`'s own compiled code, which never mentions that tag, heading, or test.

## Changes made (file by file)

### `src/Editor/FrameDebuggerData.cpp`

- Added `#include "../Renderer/RenderGraph/RenderPassGroupRegistry.h"` to the include block
  (immediately after `#include "FrameDebuggerData.h"`), matching this file's existing minimal
  include style.
- Rewrote the pre-GameView discovery loop (previously ~lines 745-802, live-re-read before editing
  per this phase's own instruction — drift from `PHASE0`'s ~775/~828 estimate was, as expected,
  real) exactly per the phase document's Step 3.1 template:
  - `computeLutGroup` (a single hardcoded `FrameDebuggerEventNode` named `"Compute LUT"`) was
    replaced with `std::vector<FrameDebuggerEventNode> labeledGroups`, built BEFORE the loop starts
    from `rg::PassGroupLabelCount()`/`rg::PassGroupLabelUiHeadingAt(g)`, one entry per currently
    REGISTERED heading, in REGISTRATION ORDER.
  - The loop's own per-pass branch changed from `if (pass.category ==
    rg::RenderPassCategory::AtmosphereLut)` (PHASE3's own `if (false)` placeholder) to
    `const std::optional<std::size_t> groupIndex = rg::FindPassGroupIndexForTags(pass.tags); if
    (groupIndex.has_value()) { labeledGroups[*groupIndex]... } else { preGameViewGroup... }`.
  - The tree-append step at the end of the pre-GameView section now loops over `labeledGroups` in
    registration order (appending each non-empty one), THEN appends `preGameViewGroup` if non-empty
    — identical presentation-order contract to the old hardcoded
    computeLutGroup-then-preGameViewGroup sequence, confirmed by the live smoke test below.
  - `postGameViewGroup`'s own declaration/usage (further down in the function, unaffected by this
    phase per `PHASE0` Step 2.3's own confirmation that the post-GameView loop never branched on
    `category`) was left completely untouched, exactly where it already was.
  - Doc comments immediately above and inside the rewritten loop were rewritten to describe the
    actual, current, generic mechanism (per Step 3.2) instead of the now-deleted
    `AtmosphereLut`/`GpuSkinning` enumerator names — the surviving `AtmosphereLut`/`GpuSkinning`
    string occurrences left in this file (confirmed via a fresh `search_in_dir` after all edits)
    are all HISTORY-describing comments (e.g. "by the Atmosphere feature's own
    AtmosphereLutRenderer constructor", "replacing the old hardcoded
    RenderPassCategory::AtmosphereLut check"), never executable code or a string literal driving
    behavior — matching this phase's own acceptance bar.
  - The smaller comment near the `Debug`-category exclusion check (view-region walk, ~line 819)
    was re-checked per Step 3.2's own instruction and needed NO change — it only ever mentioned
    `Debug`, a permanent, untouched Core value, never `AtmosphereLut`/`GpuSkinning`.

### `src/Editor/FrameDebuggerData.h`

Per Step 3.2's explicit instruction to fix the PUBLIC header's own ASCII tree-shape diagram (the
comment block immediately above `BuildRealFrameDebuggerSnapshot()`'s declaration, live-re-read at
its actual current lines ~446-465, close to the phase doc's own ~446-462 estimate):

- `"Compute LUT" (AtmosphereLut-category compute passes before the pivot)` → rewritten to a
  4-line, generic description: `"Compute LUT" (every compute pass before the pivot carrying a tag
  registered via RenderPassGroupRegistry - today that is Atmosphere's own tag, but this header must
  never say so as if it were a permanent rule)`.
- `... one such v-parent per surviving AtmosphereLut-category pass` → `... one such v-parent per
  surviving pass carrying that registered tag`.
- `"Compute Dispatches (Post-GameView)" (General-category compute passes after the view region)` →
  `"Compute Dispatches (Post-GameView)" (every compute pass after the view region, regardless of
  tag - this group is never subdivided by tag)`.

No other line in this diagram names a deleted enumerator (`"RenderOpaque"`, `"DrawSkyBackground"`,
`"RenderTransparent"`, `Debug`-related lines are all still-real, permanent Core concepts and were
left untouched).

### `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`

- The 3 tests PHASE3 left red (`MixedComputeLutAndPreGameViewCategoriesProduceBothGroupsInFixedOrder`,
  `OnlyAtmosphereLutCategoryPreGameViewPassProducesOnlyComputeLutGroup`,
  `ComputeLutSubPassAlsoOwnsAComputeDispatchChild`) already had their
  `rg::ResetPassGroupRegistryForTesting(); rg::RegisterPassGroupLabel(kAtmosphereLutPassTag,
  "Compute LUT");` pair added by PHASE3 itself (confirmed live before touching anything) — no
  further test-body change was needed; all 3 now pass with **zero assertion changes**, exactly the
  "byte-identical" acceptance bar this campaign has held itself to throughout. No extra
  reset/register-helper function was added, since PHASE3 already inlined the two-line
  reset+register pair directly into each of the 3 test bodies (a cheap, already-existing pattern —
  introducing a shared helper now would have meant touching all 3 test bodies again for a purely
  cosmetic change, which this phase's own Step 3.3 explicitly allows skipping under "purely
  optional polish... skip if it risks introducing an unrelated mistake").
- **New test added**, per Step 3.4: `FrameDebuggerSnapshotBuilderTest.
  ASyntheticThirdPartyTagAndHeadingGetsGroupedWithZeroProductionCodeAwareness` — registers a
  synthetic `constexpr rg::RenderPassTag kFakeFuturePluginTag{ 1ull << 40 }` (a deliberately unused
  bit, well clear of the two real production bits this campaign chose in PHASE3 —
  `kAtmosphereLutPassTag` = bit 0, `kGpuSkinningDispatchPassTag` = bit 1) under the heading
  `"Totally Fake Future Plugin Group"`, builds a fixture with one pre-GameView compute pass tagged
  with that synthetic tag plus the usual `"RenderOpaque"` pivot, and asserts the resulting tree's
  root has exactly 2 children: the synthetic heading group (containing that one pass) first, then
  `"RenderOpaque"` — proving `FrameDebuggerData.cpp`'s own compiled code, which never once mentions
  this string/tag/test, still correctly buckets it purely from registry DATA. Inserted immediately
  after `ComputeLutSubPassAlsoOwnsAComputeDispatchChild`.

## Deviations from the phase document

None of substance. Line numbers in the phase document were approximate, as explicitly warned — the
live file was re-read immediately before every edit and actual positions differed slightly (e.g.
the loop actually started at line 746, not exactly `PHASE0`'s own ~775 estimate, after PHASE1's new
include line shifted everything down by 2). The optional "add a small local test-fixture helper
function" suggestion in Step 3.3 was deliberately skipped (see above) since PHASE3 had already
inlined the reset+register pair into all 3 relevant tests — the phase document itself flags this
exact helper-extraction step as optional polish, skippable if it risks an unrelated mistake.

## Build & test result

- **Incremental build** (`cmake --build build`): succeeded cleanly, zero new warnings/errors (10
  build steps: `FrameDebuggerData.cpp.obj`, `FrameDebuggerHistory.cpp.obj`,
  `FrameDebuggerHistoryTests.cpp.obj`, `FrameDebuggerPanel.cpp.obj`,
  `FrameDebuggerDataTests.cpp.obj`, `FrameDebuggerSnapshotBuilderTests.cpp.obj`,
  `ImGuiEditorLayer.cpp.obj`, plus relink of `gte_core`, `GreatTamanaEngine`, and
  `GreatTamanaEngineTests`).
- **Targeted test run** (`GreatTamanaEngineTests.exe --gtest_filter="FrameDebuggerSnapshotBuilderTest.*"`):
  **36/36 tests passed** — all 35 pre-existing tests (including the 3 that were red after PHASE3)
  plus the 1 new synthetic-genericity test, with **zero test deleted or skipped**.
- **Broader safety-net run** (`--gtest_filter="*RenderGraph*:*RenderPass*:*RenderPipeline*:*FrameDebugger*"`):
  **373/373 tests passed** (up from PHASE3's own 372-total/369-passing baseline — the +1 total is
  this phase's new test; zero other regression anywhere in the Render Graph/Render Pipeline/Render
  Pass Group Registry/Frame Debugger test surfaces).
- Per this campaign's own process rules, the FULL `ctest` suite was deliberately **not** run — that
  is PHASE5's job. No full/clean build was performed either, only the incremental checks above.

## Live HTTP-driven smoke test (`gte_send_request`)

Per the phase document's own acceptance bar ("run at least one live capture here as a basic sanity
check... it is acceptable to defer the FULL live-compare smoke test to PHASE5"), ran the full
open → enable → capture → state → screenshot sequence against a real, freshly-launched
`GreatTamanaEngine.exe` instance:

1. `GET /frame_debugger/open` → `{"success":true, ...}`.
2. `GET /frame_debugger/enable?value=true` → `{"success":true, "enabled":true, ...}` (note: the
   query parameter is `value`, not `enable` — confirmed live after an initial `400` on the wrong
   parameter name; not a tool bug, just this endpoint's own documented contract).
3. `GET /frame_debugger/capture` → `{"success":true, "hasCapturedFrame":true,
   "totalEventCount":15, ...}` — a real frame was captured with 15 real events.
4. `GET /get_swapchain` → a real screenshot of the running Editor with the Frame Debugger window
   open, confirming the exact tree shape this whole campaign is required to preserve
   byte-for-byte:
   - `"Game View"` root
     - `"Compute LUT"` (expanded) — containing, in true execution order,
       `AtmosphereTransmittanceLutPass`, `AtmosphereMultiScatteringLutPass`,
       `AtmosphereSkyViewLutPass`, `AtmosphereAerialPerspectiveVolumePass`,
       `AtmosphereAerialPerspectiveVolumeDebugSlicePass` — each its own real "v PassName" parent
       owning one real "Compute Dispatch" child, exactly as every prior campaign's own baseline
       screenshot shows.
     - `"RenderOpaque"` leaf
     - `"DrawSkyBackground"` leaf (owning "Draw Quad")
     - `"Compute Dispatches (Post-GameView)"` — containing
       `AtmosphereAerialPerspectiveCompositePass` (owning "Compute Dispatch").
   - Selecting event #0 (`AtmosphereTransmittanceLutPass`'s own "Compute Dispatch" child) correctly
     shows its real Shader/Pass name in the Inspector pane (`"AtmosphereTransmittanceLutPass"`).

This is **identical** to the tree shape documented in every prior render-pass campaign's own
screenshots and to `AGENTS.md`'s own "Frame Debugger" section description — confirming the
generic registry-driven rewrite produces byte-identical real, live output, not just
byte-identical Tier-1 fixture output. `GET /frame_debugger/enable?value=false` was called
afterward to leave the Editor in a clean state, and the process was terminated via
`stop_app_background` once the check was complete.

## Acceptance bar check (against the phase document's own criteria)

- ✅ `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` — 100% pass (36/36), zero test
  deleted/skipped.
- ✅ The new synthetic-tag genericity test (Step 3.4) exists and passes.
- ✅ No string literal naming a specific Layer-2 feature (`"Atmosphere"`, `"GpuSkinning"`,
  `"LUT"`, `"Skinning"`) remains anywhere in `FrameDebuggerData.cpp`'s own compiled logic/string
  literals — confirmed via a fresh `search_in_dir` after all edits: every remaining
  `AtmosphereLut`/`GpuSkinning` hit in this file is inside a `//` comment describing HISTORY, never
  executable code. The only heading string this file constructs itself is the generic fallback,
  `"Compute Dispatches (Pre-GameView)"`.
- ✅ `src/Editor/FrameDebuggerData.h`'s own ASCII tree-shape diagram no longer says
  "AtmosphereLut-category"/"General-category" as if those enumerators still exist.
- ✅ A live, HTTP-driven smoke test (`gte_send_request`) against a running instance shows the exact
  same tree shape/headings as every prior campaign's own documented baseline (see above) — the
  full historical "before this entire campaign" A/B comparison is explicitly deferred to PHASE5
  per this phase document's own stated allowance (PHASE3 already changed the underlying data model,
  so a true pre-campaign live baseline is no longer directly capturable at this point).

PHASE4 is complete and ready for PHASE5 (`PHASE5_VERIFICATION_AND_CAMPAIGN_COMPLETION.md`), which
owns the full `ctest` regression pass, the full historical live A/B Frame Debugger comparison,
`AGENTS.md`'s own update, and this campaign's `CAMPAIGN_COMPLETION_REPORT.md`.
