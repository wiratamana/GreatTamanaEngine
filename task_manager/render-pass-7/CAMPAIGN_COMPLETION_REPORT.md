# render-pass-7 — Core Campaign 1: De-hardcode `RenderPassCategory` — CAMPAIGN COMPLETION REPORT

**Parent:** `PHASE0_MASTER_STRATEGY.md`. This report is the single, top-level summary of the whole
five-phase campaign, mirroring `render-pass-6/CAMPAIGN_COMPLETION_REPORT.md`'s own shape. See each
`PHASEn_COMPLETION_REPORT.md` in this same folder for full per-phase detail.

## What this campaign set out to do

Implement exactly ONE item from
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\render-pass\CORE_EXPANSION_STRATEGY_v2.md`,
Section 3, "Core Campaign 1 — De-hardcode `RenderPassCategory`": make it structurally impossible
for a Core (Layer 1) file — `src/Renderer/RenderGraph/RenderGraphTypes.h` — to ever need to name a
specific Layer-2 plugin feature (Atmosphere, GPU Skinning) again, while keeping the Frame
Debugger's visible tree output byte-for-byte unchanged.

## What shipped, phase by phase

**PHASE1 — Tag Vocabulary Relocation + End-to-End `tags` Threading.** `RenderPassTag`/
`RenderPassTagMask` relocated from `RenderPipeline.h` into `RenderGraphTypes.h` (mirroring
`RenderPassEvent`'s own established precedent for "a type that must live in a lower Core file").
`PassRecord`/`RenderGraphPassSnapshot` gained a real `tags` field; both `RenderGraphBuilder::
AddRenderPass()` overloads gained a new trailing, defaulted `tags` parameter. A real,
PRE-EXISTING dead-field bug was found and fixed as a necessary prerequisite: `RenderPipeline::
DeclareOnePhase()`'s own `builder.AddRenderPass(...)` call never actually passed `desc.tags`
through, silently dropping it every frame for every provider — meaning the tag mechanism the
source strategy document assumed was "already partly proven" was, in fact, completely inert for
anything routed through `RenderPipeline` before this fix. Zero behavior change: every pass's
`tags` value was `0` both before and after this phase. 7 new tests added; 55/55 targeted + 270/270
broader Render Graph tests passed.

**PHASE2 — The Generic Core Facility: `RenderPassGroupRegistry`.** A brand-new, additive Core
facility, `RenderPassGroupRegistry.h/.cpp` (`RegisterPassGroupLabel()`, `FindPassGroupIndexForTags()`,
plus enumeration/reset helpers), lets any Layer-2 module register a human-readable Frame Debugger
tree heading for its own tag, with Core itself never learning or caring what any tag or heading
means. Pure vocabulary + mechanism with zero real consumers wired up yet — zero existing call site
touched. 9 new tests added, all passing; compiles cleanly in both `GTE_ENABLE_EDITOR=ON` and
`=OFF`.

**PHASE3 — Trim `RenderPassCategory`, Add Feature Tag Headers, Migrate All Real Call Sites.**
`RenderPassCategory` trimmed from 4 enumerators (`General`, `AtmosphereLut`, `GpuSkinning`,
`Debug`) to exactly 2 (`General`, `Debug`). Two new Layer-2 tag headers created:
`src/Renderer/Atmosphere/AtmosphereRenderPassTags.h` (`kAtmosphereLutPassTag` = bit 0) and
`src/Renderer/GpuSkinning/GpuSkinningRenderPassTags.h` (`kGpuSkinningDispatchPassTag` = bit 1) —
both exactly the bit values the phase document's own examples proposed, confirmed live to be free
of any prior conflict. All 7 real production call sites (5 Atmosphere LUT passes in
`AtmosphereLutRenderer.cpp`, the `"GpuSkinning"` `RenderPipeline` provider in `Application.cpp`,
`AddGpuSkinningPasses()`'s direct-render-only fallback in `RenderPasses.cpp`) migrated from their
old feature-named category to `RenderPassCategory::General` plus a real, non-zero `tags` value.
`AtmosphereLutRenderer`'s own constructor now self-registers the `"Compute LUT"` heading via
`RegisterPassGroupLabel()`. A necessary, honestly-documented gap in the phase document's own task
list was found and fixed: `src/Editor/FrameDebuggerData.cpp` (the one real consumer of `category`
beyond pass-through storage) needed a compile-only `if (false)` stopgap since the phase document's
Step 3 checklist never assigned it an edit — deliberately NOT implementing PHASE4's own assigned
generic-registry rewrite here, per this phase's explicit scope boundary. As an expected, documented
consequence, `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`'s 3 grouping-BEHAVIOR tests went
red (32/35 passing) — correctly, for the single anticipated reason (the "Compute LUT" heading was
temporarily never produced), not a real regression. 62/62 targeted call-site tests passed; 372
broader tests ran with exactly the 3 expected failures.

**PHASE4 — Rewrite the Frame Debugger's Tree-Grouping Logic to be Fully Generic.**
`FrameDebuggerData.cpp`'s pre-GameView compute-dispatch discovery loop no longer branches on
`RenderPassCategory` at all — it now builds one bucket per currently-registered `(tag -> heading)`
pair from `RenderPassGroupRegistry`, in registration order, and looks up which bucket a surviving
pass belongs to via `FindPassGroupIndexForTags(pass.tags)`, falling back to the generic
`"Compute Dispatches (Pre-GameView)"` bucket when a pass carries no registered tag. PHASE3's own
`if (false)` placeholder is gone, replaced by this real, generic lookup. The 3 tests PHASE3 left
red went green again with **zero assertion changes** — proving the rewrite reproduces the exact
old behavior byte-for-byte. A new, dedicated genericity-proof test
(`ASyntheticThirdPartyTagAndHeadingGetsGroupedWithZeroProductionCodeAwareness`) registers a
synthetic, test-only tag/heading pair with zero relationship to any real feature and confirms it
is correctly bucketed by code that never mentions it — the campaign's own required proof that a
future Layer-2 plugin can get this treatment by editing only its own files. 36/36 targeted tests
passed (35 pre-existing + 1 new); 373/373 broader tests passed. A live HTTP-driven smoke test
against a real running engine confirmed the exact same tree shape every prior campaign documents.

**PHASE5 — Full Verification, Live Smoke Test, and Campaign Completion.** The one phase allowed a
full clean build and the full regression suite. `cmake --build build` (already up to date, zero
work needed — confirming no stale-object-file issue from PHASE1's header relocation) and
`cmake --build build-editor-off` (65 build steps, full relink, zero warnings/errors) both
succeeded. The full `ctest` run: **1753 tests, 1752 passed, 1 skipped (100% of runnable tests
passing)** — up from `render-pass-6`'s own 1736 baseline, an increase of exactly 17 tests (7 from
PHASE1, 9 from PHASE2, 0 from PHASE3, 1 from PHASE4), the exact expected delta, with zero new
failures and the same single pre-existing environment-gated skip
(`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`). A live, HTTP-driven Frame
Debugger smoke test against a freshly launched engine instance confirmed the Game View tree's
`"Compute LUT"` (5 Atmosphere LUT passes, in true execution order, each owning a "Compute
Dispatch" child), `"RenderOpaque"`, `"DrawSkyBackground"` (owning "Draw Quad"), and `"Compute
Dispatches (Post-GameView)"` (owning `AtmosphereAerialPerspectiveCompositePass`) grouping and
per-leaf Inspector data are visually and structurally identical to every prior campaign's own
documented baseline; `GET /get_game_view` confirmed the rendered image itself is visually
unaffected; `GET /get_logs` confirmed the PHASE2 "tag registered twice under a different heading"
soft warning never fired. `AGENTS.md`'s "Render Pass System" section was updated with a new
paragraph matching the section's established prose style, and this report plus
`PHASE5_COMPLETION_REPORT.md` were written.

## Final verification snapshot

- **Build:** Full clean build succeeds with zero warnings/errors in both `build`
  (`GTE_ENABLE_EDITOR=ON`) and `build-editor-off` (`GTE_ENABLE_EDITOR=OFF`).
- **Tests:** 1753 total, 1752 passed, 1 pre-existing environment-gated skip — 100% of runnable
  tests passing, up from `render-pass-6`'s own 1736 baseline (+17 new tests, exactly accounted
  for).
- **Live smoke test:** Confirmed via `gte_send_request` against a real running
  `GreatTamanaEngine.exe` — Frame Debugger tree grouping, per-leaf Inspector data, and the actual
  rendered Game View image are all visually/structurally unchanged from every prior campaign's own
  baseline.
- **Structural acceptance bar:** A fresh `search_in_dir` for `RenderPassCategory::AtmosphereLut`
  and `RenderPassCategory::GpuSkinning` across all of `src/` and `tests/` returns **zero hits** —
  the only surviving occurrence anywhere is one `//` HISTORY comment in `FrameDebuggerData.cpp`
  describing what the old code used to check, never executable code.

## Deviations from the original plan (consolidated from every phase's own report)

1. **PHASE1 — none of substance.** Line-number estimates in `PHASE1_TAG_VOCABULARY_AND_THREADING.md`
   were approximate, as expected; actual positions differed slightly but every edit matched the
   phase document's own instructions exactly.
2. **PHASE2 — none.** Every new file/API/test matches the phase document's Step 3 instructions
   exactly.
3. **PHASE3 — two real deviations, both required and honestly documented at the time:**
   - `src/Editor/FrameDebuggerData.cpp` required a compile-only fix (`if (false)` plus an
     explanatory comment) that neither `PHASE0_MASTER_STRATEGY.md`'s Step 2.3 nor
     `PHASE3_DEHARDCODE_CATEGORY_AND_MIGRATE_CALL_SITES.md`'s own Step 3 checklist actually
     assigned as an edit — a real gap in the phase document's own enumerated task list, not a
     judgment call. Handled with the smallest, most honest possible fix, deliberately NOT
     performing PHASE4's own assigned generic-registry rewrite prematurely.
   - `tests/Renderer/RenderGraph/RenderPassTests.cpp` had **two** occurrences of the deleted
     `RenderPassCategory::AtmosphereLut` enumerator, not the one `PHASE0`'s own Step 2.2 table and
     this phase's own Step 3.9 both implied — found via the fresh `search_in_dir` this phase's own
     process explicitly required, and migrated using the minimal, purpose-matching fix.
4. **PHASE4 — none of substance.** The optional "add a small local test-fixture helper function"
   suggestion in the phase document's own Step 3.3 was deliberately skipped, exactly as that same
   document flagged as safe to skip ("purely optional polish... skip if it risks introducing an
   unrelated mistake") — PHASE3 had already inlined the reset+register pair into all 3 relevant
   tests, so extracting a helper now would have meant touching all 3 test bodies again for a
   purely cosmetic change.
5. **PHASE5 — none.** Every step matched the phase document's own Step 3 instructions exactly;
   zero regression was found, so the phase document's own "diagnose then `delegate_task`" fallback
   clause was never invoked (and could not have been, since this task's own top-level rules forbid
   `delegate_task` for this leaf implementation task regardless).

**Bit values actually chosen (Step 3.5's own required restatement):** `kAtmosphereLutPassTag` =
`RenderPassTag{ 1ull << 0 }` (bit 0); `kGpuSkinningDispatchPassTag` =
`RenderPassTag{ 1ull << 1 }` (bit 1) — both exactly the values `PHASE3_DEHARDCODE_CATEGORY_AND_MIGRATE_CALL_SITES.md`'s
own Step 3.1/3.2 examples proposed. No deviation from the suggested values; no bit-allocation
conflict was found with anything else in the tree at the time PHASE3 landed.

## What remains permanently unchanged (non-goals, confirmed still true after this campaign)

- No new `RenderPassCategory` enumerator was ever added, for anything — the entire point of this
  campaign.
- Zero change to `ViewScope`, `RenderPassDrawKind`, `RenderPassEvent`, or any compiler/barrier
  behavior — `RenderGraphCompiler`/`RenderGraphBarrierPlanner`/`RenderGraph::Execute()` read
  neither `category` nor `tags` before this campaign and still read neither after it.
- Atmosphere and GPU Skinning were not migrated into fully clean, independent Layer-2 modules —
  out of scope per the source strategy document's own Section 1.3/6; this campaign only removed
  the one concrete, evidenced Core-file violation their pass declarations triggered.
- No other Core Campaign (2-7) from the source strategy document was touched.

Core Campaign 1 ("De-hardcode `RenderPassCategory`") is complete, verified, and ready to merge.
