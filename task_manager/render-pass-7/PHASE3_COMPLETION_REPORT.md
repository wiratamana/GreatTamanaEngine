# PHASE3 — Trim `RenderPassCategory`, Add Feature Tag Headers, Migrate All Real Call Sites — COMPLETION REPORT

**Parent:** `PHASE0_MASTER_STRATEGY.md`. **Phase doc followed:**
`PHASE3_DEHARDCODE_CATEGORY_AND_MIGRATE_CALL_SITES.md`. **Depends on:** PHASE1 (real `tags`
threading, already landed) and PHASE2 (`RenderPassGroupRegistry`, already landed) — both
confirmed by reading `PHASE1_COMPLETION_REPORT.md`/`PHASE2_COMPLETION_REPORT.md` and the live
source tree before starting.

## Summary

Implemented exactly what the phase document's Step 3 specified: `RenderPassCategory` is now
trimmed to exactly `{General, Debug}`, two new Layer-2 tag headers exist
(`AtmosphereRenderPassTags.h`, `GpuSkinningRenderPassTags.h`), all 7 real production call sites
enumerated by `PHASE0` Step 2.2 now carry `RenderPassCategory::General` plus a real, non-zero
`tags` value, Atmosphere's own code registers the "Compute LUT" heading from its own file via
`RenderPassGroupRegistry`, and every existing test that referenced the deleted enumerators was
migrated to the new tag-based vocabulary. As explicitly anticipated by the phase document,
`tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`'s 3 grouping-BEHAVIOR tests are now
red/failing — this is the expected, documented, temporary state pending PHASE4's own rewrite of
`FrameDebuggerData.cpp`'s tree-grouping consumer logic, not a regression introduced here.

## Bit values chosen for the two new tags

- `gte::kAtmosphereLutPassTag` (`src/Renderer/Atmosphere/AtmosphereRenderPassTags.h`) —
  `RenderPassTag{ 1ull << 0 }` (bit 0), exactly as the phase document's own Step 3.1 example
  proposed. Confirmed live before implementing that no other code in the tree had already
  claimed bit 0 for anything else (this is the campaign's first real tag consumer of any kind —
  PHASE1/PHASE2 only ever exercised `tags` with test-only literal values, never a real
  production bit).
- `gte::kGpuSkinningDispatchPassTag` (`src/Renderer/GpuSkinning/GpuSkinningRenderPassTags.h`) —
  `RenderPassTag{ 1ull << 1 }` (bit 1), exactly as the phase document's own Step 3.2 example
  proposed.

No bit-allocation conflict was found; both values were used exactly as the phase document
suggested with no deviation.

## Changes made (file by file)

### New files

1. **`src/Renderer/Atmosphere/AtmosphereRenderPassTags.h`** — `kAtmosphereLutPassTag`, verbatim
   per Step 3.1.
2. **`src/Renderer/GpuSkinning/GpuSkinningRenderPassTags.h`** — `kGpuSkinningDispatchPassTag`,
   verbatim per Step 3.2.

### `RenderGraphTypes.h` / `RenderGraphTypes.cpp` (Core)

- `RenderPassCategory` trimmed from 4 enumerators (`General`, `AtmosphereLut`, `GpuSkinning`,
  `Debug`) to exactly 2 (`General`, `Debug`), per Step 3.3.
- `ToString(RenderPassCategory)` trimmed to a 2-case switch (still no `default:` case, still the
  trailing `return "Unknown";` fallback), per Step 3.4.

### 5 `AtmosphereLutRenderer.cpp` call sites (Step 3.5) — re-confirmed against the LIVE file

Every one of the 5 call sites from `PHASE0` Step 2.2 was re-read live before editing (lines had
drifted slightly from the doc's own estimates, as expected):

| Pass name | Category line (actual) | Trailing-args line (actual) |
|---|---|---|
| `AtmosphereTransmittanceLutPass` | 237 | 272 |
| `AtmosphereMultiScatteringLutPass` | 335 | 385 |
| `AtmosphereSkyViewLutPass` | 475 | 525 |
| `AtmosphereAerialPerspectiveVolumePass` | 617 | 670 |
| `AtmosphereAerialPerspectiveVolumeDebugSlicePass` | 972 | 1006 |

Every one of these 5 call sites already spelled out `rg::RenderPassDrawKind::DrawMesh,
rg::RenderPassEvent::PreOpaques` explicitly at its own trailing-args line (a leftover from the
`render-pass-3` campaign's own PHASE4 root-cause fix, confirmed live rather than assumed) — so
no "spell out the two hidden defaults" step was actually needed here; only `kAtmosphereLutPassTag.bit`
was appended as a new trailing argument to each. Category changed from
`rg::RenderPassCategory::AtmosphereLut` to `rg::RenderPassCategory::General` at each pass's own
declaration line. The 6th call site (`AtmosphereAerialPerspectiveCompositePass`, already
`General`) was confirmed untouched, per Step 3.5's own explicit instruction.

`#include "../RenderGraph/RenderPassGroupRegistry.h"` and `#include "AtmosphereRenderPassTags.h"`
added to `AtmosphereLutRenderer.cpp`'s include block (alongside the pre-existing
`../RenderGraph/RenderGraph.h`/`../Vulkan/DescriptorSetLayoutBuilder.h`).

### "Compute LUT" heading registration (Step 3.6)

- `AtmosphereLutRenderer.h`: `AtmosphereLutRenderer() = default;` (inline) → `AtmosphereLutRenderer();`
  (declared-only).
- `AtmosphereLutRenderer.cpp`: added the constructor definition, immediately before the existing
  destructor, calling `rg::RegisterPassGroupLabel(kAtmosphereLutPassTag, "Compute LUT");` —
  wording matches the phase document's own suggested body verbatim.
- Confirmed by a successful full incremental build (see below) that every translation unit
  constructing an `AtmosphereLutRenderer` (it is always default-constructed, no call site passes
  arguments) still compiles unmodified.

### `Application.cpp` (`"GpuSkinning"` `RenderPipeline` provider, Step 3.7)

- Re-read the live file: the call site was at line 452 (`desc.legacyCategory =
  rg::RenderPassCategory::GpuSkinning;`), inside the `"GpuSkinning"` provider's per-request
  `RenderPassDesc` construction.
- Replaced that single line with `desc.tags = kGpuSkinningDispatchPassTag.bit;` (the
  `legacyCategory` field is simply no longer set at all here now, so it keeps its own struct
  default of `RenderPassCategory::General` — exactly per the phase document's instruction to
  delete the line rather than set it explicitly to `General`).
- Added `#include "../Renderer/GpuSkinning/GpuSkinningRenderPassTags.h"` immediately after the
  pre-existing `#include "../Renderer/GpuSkinning/GpuSkinningPipelines.h"` line, matching this
  file's existing include style/relative-path convention.

### `RenderPasses.cpp` (`AddGpuSkinningPasses()`, Step 3.8)

- Re-read the live file: the call site was at line 409. Unlike the 5 Atmosphere call sites, this
  one did NOT already spell out `drawKind`/`renderPassEvent` explicitly — it relied on both
  hidden defaults. Confirmed live in `RenderGraphBuilder.h` (the 9-argument overload) that the
  current defaults are `RenderPassDrawKind::DrawMesh` and `RenderPassEvent::Opaques` before
  spelling them out.
- Category changed from `rg::RenderPassCategory::GpuSkinning` to `rg::RenderPassCategory::General`
  at the pass's own declaration line; the closing `});` of the `execute` lambda became `},` and a
  new trailing line, `rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::Opaques,
  kGpuSkinningDispatchPassTag.bit);`, was appended.
- Added `#include "../Renderer/GpuSkinning/GpuSkinningRenderPassTags.h"` immediately after the
  pre-existing `GpuSkinningPipelines.h` include, matching this file's own convention.

### One necessary, undocumented-by-the-phase-doc production fix: `src/Editor/FrameDebuggerData.cpp`

`PHASE0_MASTER_STRATEGY.md` Step 2.3 identifies `FrameDebuggerData.cpp` line ~775 (`if
(pass.category == rg::RenderPassCategory::AtmosphereLut) { ... }`) as "the ONLY real consumer of
`category` for anything beyond pass-through storage" — but neither `PHASE0` nor this phase's own
document (`PHASE3_DEHARDCODE_CATEGORY_AND_MIGRATE_CALL_SITES.md`) actually lists an edit to this
file in Step 3's plan. Trimming the enum (Step 3.3) as instructed would otherwise leave this
production `.cpp` file referencing a deleted enumerator, which does not compile. This is a real
gap in the phase document's own enumerated task list, not a judgment call — fixing it was
mandatory to satisfy this same phase's own acceptance bar ("Full incremental build succeeds").

The fix applied is deliberately minimal and honest about the tradeoff: the condition
`pass.category == rg::RenderPassCategory::AtmosphereLut` was replaced with a literal `if
(false)`, with a comment explaining explicitly that this is intentional and temporary — PHASE4
owns rewriting this consumer to read the new generic tag/registry mechanism instead (per
`PHASE0` Step 2.3 and this campaign's own Step 3 sequencing rationale, which explicitly assigns
that rewrite to PHASE4, not PHASE3). The practical effect: every pre-GameView compute pass
(including the 5 real Atmosphere LUT passes) now falls into the generic "Compute Dispatches
(Pre-GameView)" bucket, and the "Compute LUT" heading is never produced by
`BuildRealFrameDebuggerSnapshot()` today — this is the root cause of the 3 documented test
failures below, a real, deliberate, temporary behavior change, not a silent one and not a
compile-workaround that masks anything.

An alternative was considered and rejected: directly reading `pass.tags &
kAtmosphereLutPassTag.bit` here would have made the 3 grouping tests pass again immediately —
but that would mean this phase silently doing PHASE4's own assigned work (a hardcoded,
feature-specific tag check, not the generic registry-driven consumer PHASE4 is specified to
build), directly contradicting this phase's own explicit instruction: "note explicitly in this
phase's own completion report that this specific test file is EXPECTED to still show
pre-existing failures until PHASE4 completes, and do not attempt to force it green here — that
would mean prematurely doing PHASE4's own work."

### Test file migrations (Step 3.9)

- **`tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp`** — `RenderGraphRenderPassCategoryTest`
  suite's `values[]` array trimmed to `{General, Debug}`; the two `ToString()`-equality
  assertions trimmed to match.
- **`tests/Renderer/RenderGraph/RenderPassTests.cpp`** — a fresh `search_in_dir` (done live,
  before editing) found the deleted-enumerator reference appears TWICE in this file, not once as
  `PHASE0`'s own table implied (lines ~72/80 AND ~195/202 — the second occurrence was added by
  PHASE1's own new tags-threading tests, using `AtmosphereLut` merely as an arbitrary non-`General`
  category value, unrelated to any real feature tag):
  - Lines ~72/80 (`AddRenderPassFourArgumentOverloadStampsViewScopeAndCategory`) — migrated
    exactly per the phase document's own instruction: fixture now uses
    `RenderPassCategory::General` plus a real trailing `kAtmosphereLutPassTag.bit` argument (with
    `drawKind`/`renderPassEvent` spelled out explicitly to reach past them, matching this
    overload's real parameter order), a new `EXPECT_EQ(input.passes[0].tags,
    kAtmosphereLutPassTag.bit);` assertion added alongside the still-valid `category ==
    General` one, and the test renamed to
    `AddRenderPassFourArgumentOverloadStampsViewScopeCategoryAndTags` (its old name no longer
    matched what it proves). Added `#include "Renderer/Atmosphere/AtmosphereRenderPassTags.h"` to
    this file (matching this test folder's already-established `"Renderer/Atmosphere/..."`
    include-path convention, confirmed via `search_in_dir` against 3 existing sibling test files).
  - Lines ~195/202 (`AddRenderPassNineArgumentOverloadStampsTagsAlongsideEveryOtherField`, a
    PHASE1 test) — this one already tests category+tags pass-through generically via an
    arbitrary `RenderPassTagMask{0x4u}` literal, with no real dependency on the actual Atmosphere
    feature; simply changed `RenderPassCategory::AtmosphereLut` → `RenderPassCategory::General`
    at both the fixture line and its own assertion, with zero other change — this test's own
    purpose (proving `tags` and every other field travel through the 9-argument overload
    together) is completely unaffected by the category enum's trim.
- **`tests/Renderer/RenderGraph/RenderPipelineTests.cpp`** — `LegacyCategoryAndDrawKindSurviveUnchangedIntoTheProducedPassRecord`
  (confirmed live at line ~292, close to the doc's own ~301 estimate for the specific field
  assignment inside it): `desc.legacyCategory = RenderPassCategory::AtmosphereLut;` →
  `desc.legacyCategory = RenderPassCategory::General;` plus a new `desc.tags =
  kAtmosphereLutPassTag.bit;` line; the corresponding assertion updated from
  `EXPECT_EQ(input.passes[0].category, RenderPassCategory::AtmosphereLut);` to
  `RenderPassCategory::General` plus a new `EXPECT_EQ(input.passes[0].tags,
  kAtmosphereLutPassTag.bit);` line, exactly per the phase document's own instruction. Added
  `#include "Renderer/Atmosphere/AtmosphereRenderPassTags.h"` to this file. Test name left
  unchanged (still accurate — it proves legacyCategory AND drawKind AND, now additionally, tags,
  survive unchanged; the phase document did not require renaming this specific test).
- **`tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp`** — re-confirmed via a fresh
  `search_in_dir` (as instructed) that this file has zero references to either deleted
  enumerator; all 4 of its `RenderPassCategory::` hits already read `General`. No change needed,
  confirming `PHASE0`'s own earlier campaign-wide search finding.
- **`tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`** — made to COMPILE again, per this
  phase's explicit scope boundary (grouping-BEHAVIOR is PHASE4's job, not this phase's):
  - Added `#include`s for `Renderer/Atmosphere/AtmosphereRenderPassTags.h`,
    `Renderer/GpuSkinning/GpuSkinningRenderPassTags.h`, and
    `Renderer/RenderGraph/RenderPassGroupRegistry.h`.
  - `MixedComputeLutAndPreGameViewCategoriesProduceBothGroupsInFixedOrder` — `bufferPass.category
    = rg::RenderPassCategory::GpuSkinning;` → `bufferPass.tags = kGpuSkinningDispatchPassTag.bit;`;
    `lutPass.category = rg::RenderPassCategory::AtmosphereLut;` → `lutPass.tags =
    kAtmosphereLutPassTag.bit;`; added `rg::ResetPassGroupRegistryForTesting();` +
    `rg::RegisterPassGroupLabel(kAtmosphereLutPassTag, "Compute LUT");` at the top of the test
    body, per the phase document's own instruction ("add ... at the top of every test that needs
    the 'Compute LUT' grouping to still occur").
  - `OnlyAtmosphereLutCategoryPreGameViewPassProducesOnlyComputeLutGroup` — same treatment:
    `lutPass.category = ...AtmosphereLut;` → `lutPass.tags = kAtmosphereLutPassTag.bit;`, plus the
    same reset+register pair at the top.
  - `ComputeLutSubPassAlsoOwnsAComputeDispatchChild` (line ~1519, unaffected by the earlier edits
    since it sits well after them in the file) — identical treatment.
  - `MakeGraphicsPassWithCategory()`'s own `Debug`-category uses (lines ~1323/1325 in the ORIGINAL
    numbering) were left completely untouched, confirmed via `search_in_dir` before finishing —
    `RenderPassCategory::Debug` is out of this campaign's scope, per `PHASE0` Step 2.3.

## Deviations from the phase document

1. **`src/Editor/FrameDebuggerData.cpp` required a compile-only fix this phase document's own
   Step 3 plan never explicitly lists** (see the dedicated section above) — a real gap in the
   phase document's own enumerated task list (it correctly identifies this file as the sole real
   consumer in `PHASE0` Step 2.3, but Step 3's actual migration checklist never assigns it an
   edit). Handled with the smallest possible, most honest fix (`if (false)` plus an explanatory
   comment), deliberately NOT implementing PHASE4's own generic-registry-consumer logic here.
2. **`tests/Renderer/RenderGraph/RenderPassTests.cpp` had TWO occurrences of the deleted
   enumerator, not one** — `PHASE0`'s own Step 2.2 enumeration and this phase's own Step 3.9 both
   only mention the ~72/80 pair. The second (~195/202, a PHASE1-added test) was found via the
   fresh `search_in_dir` this phase's own Step 3.9 explicitly requires ("re-confirm via a fresh
   `search_in_dir` ... in case anything shifted") and migrated using the minimal, purpose-matching
   fix described above (arbitrary tag-mask test, not a real-feature-tag test).
3. Everything else matches the phase document's Step 3 instructions exactly — no other
   deviation.

## Build & test result

- **Incremental build** (`cmake --build build`): succeeded cleanly, zero new warnings/errors (68
  build steps, full relink of `gte_core`, `GreatTamanaEngineTests`, and `GreatTamanaEngine`).
- **Targeted test run** (touched-file suites, excluding `FrameDebuggerSnapshotBuilderTests.cpp`
  per Step 3.10's own instruction): `--gtest_filter=
  "RenderGraphRenderPassCategoryTest.*:RenderPassTest.*:RenderGraphSnapshotTest.*:RenderPipelineTest.*:RenderPassGroupRegistryTest.*"`
  — **62/62 tests passed.**
- **`FrameDebuggerSnapshotBuilderTests.cpp` run** (Step 3.10's own required confirmation — must
  fail, but ONLY for the expected reason): `--gtest_filter="FrameDebuggerSnapshotBuilderTest.*"`
  — **32/35 passed, 3 failed**, and all 3 failures are genuine, expected grouping-behavior
  mismatches (`EXPECT_EQ` value mismatches on `root.children.size()`/`root.children[0].name`/
  `lutGroup.name` — the "Compute LUT" heading is simply never produced today, exactly as this
  phase's own `if (false)` fix in `FrameDebuggerData.cpp` predicts), **not** a compile error, a
  crash, an unrelated assertion, or any other kind of failure:
  - `MixedComputeLutAndPreGameViewCategoriesProduceBothGroupsInFixedOrder` — FAILED (expected 3
    root children, got 2 — no "Compute LUT" group produced).
  - `OnlyAtmosphereLutCategoryPreGameViewPassProducesOnlyComputeLutGroup` — FAILED (expected
    `root.children[0].name == "Compute LUT"`, got `"Compute Dispatches (Pre-GameView)"`).
  - `ComputeLutSubPassAlsoOwnsAComputeDispatchChild` — FAILED (same root cause: expected
    `lutGroup.name == "Compute LUT"`, got `"Compute Dispatches (Pre-GameView)"`).
  - The other 32 tests in this suite (including every `Debug`-category test, every
    `SceneView`-exclusion test, and every event-index/wrapped-child test untouched by this
    phase's changes) all still pass unmodified.
- **Broader safety-net run** (`--gtest_filter="*RenderGraph*:*RenderPass*:*RenderPipeline*:*FrameDebugger*"`):
  **372 tests ran total, 369 passed, 3 failed** — the exact same 3 documented failures above,
  confirming zero OTHER regression anywhere in the Render Graph, Render Pipeline, Render Pass
  Group Registry, or Frame Debugger test surfaces from this phase's edits (includes
  `RenderGraphBuilderTest`, `RenderGraphCompilerTest`, `RenderGraphBarrierPlannerTest`,
  `RenderGraphDebugTextureRegistryTest`, `RenderGraphDebugVolumeTextureRegistryTest`,
  `RenderGraphNameSlotTableTests`, `FrameDebuggerDataTest`, `FrameDebuggerCaptureContextTest`,
  `FrameDebuggerCommandBridgeTest`, `FrameDebuggerPreviewProcessingTest`, and every
  Network-route/query-parsing test tied to the Frame Debugger's HTTP surface).
- Per this campaign's own process rules, the FULL `ctest` suite was deliberately **not** run —
  that is PHASE5's job. No full/clean build was performed either, only the incremental checks
  above.

## Acceptance bar check (against the phase document's own criteria)

- ✅ `RenderPassCategory` has exactly 2 enumerators anywhere in the codebase — a fresh
  `search_in_dir` for `RenderPassCategory::AtmosphereLut` and `RenderPassCategory::GpuSkinning`
  across all of `src/` and `tests/` returns **zero hits**, confirmed after all edits above (the
  historical prose mentions inside `AGENTS.md`/other campaigns' own `task_manager/*/*.md` files
  are untouched, per the acceptance bar's own explicit carve-out).
- ✅ Every real production pass that used to carry a feature-named category now carries the exact
  same distinguishing information via a real, non-zero `tags` value instead (`kAtmosphereLutPassTag.bit`
  for the 5 Atmosphere LUT passes; `kGpuSkinningDispatchPassTag.bit` for both the `"GpuSkinning"`
  provider and the `AddGpuSkinningPasses()` fallback).
- ✅ Full incremental build succeeds (confirmed above).
- ✅ Every touched test file compiles; every touched test PASSES except the explicitly-expected,
  explicitly-documented temporary `FrameDebuggerSnapshotBuilderTests.cpp` grouping-behavior
  failures (3 of them, all confirmed above to fail for the correct, expected reason), which
  PHASE4 resolves next.

PHASE3 is complete and ready for PHASE4 (`PHASE4_FRAME_DEBUGGER_GENERIC_GROUPING.md`), which owns
rewriting `FrameDebuggerData.cpp`'s tree-grouping consumer logic (including the `if (false)`
placeholder this phase left behind) to read the new generic tag/registry mechanism, with a
byte-identical-output requirement against today's real, unchanged production behavior.
