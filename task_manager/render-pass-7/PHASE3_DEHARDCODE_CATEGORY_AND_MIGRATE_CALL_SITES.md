# PHASE3 — Trim `RenderPassCategory`, Add Feature Tag Headers, Migrate All Real Call Sites

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it first). **Depends on:** `PHASE1` (real
`tags` threading) and `PHASE2` (the registry API). This is the phase that actually deletes
the two feature-named enumerators — the literal fix for the violation.

---

## Step 1 — The Goal

1. `RenderPassCategory` (`RenderGraphTypes.h`) has exactly two enumerators:
   `General`, `Debug`. `AtmosphereLut`/`GpuSkinning` no longer exist anywhere in this codebase.
2. Two new, tiny, Layer-2-owned header files exist, each defining exactly ONE
   `RenderPassTag` constant for its own feature — never in a Core file.
3. All 7 real production call sites enumerated in `PHASE0` Step 2.2 now pass
   `RenderPassCategory::General` plus a real, non-zero `tags` value (using PHASE1's new
   trailing parameter), instead of the deleted category enumerators.
4. Atmosphere's own code registers the "Compute LUT" Frame Debugger heading for its own tag,
   using PHASE2's registry — from Atmosphere's own file, never from a Core file.
5. GPU Skinning's tag is real and threaded, but deliberately registers NO heading (preserving
   today's exact behavior: GPU Skinning dispatches fall into the generic fallback bucket,
   never their own heading — see `PHASE0` Step 2.3's parenthetical).
6. Every existing test that referenced the now-deleted enumerators is updated to use the new
   tag-based vocabulary instead, and still passes.

---

## Step 2 — The Situation

Re-read `PHASE0_MASTER_STRATEGY.md` Step 2.2 for the exact 7 call sites (with approximate
line numbers) and Step 2.5 for why the tag types now live in `RenderGraphTypes.h`. Before
editing ANY call site, re-read that exact file/line range live — prior phases in this same
campaign, or unrelated concurrent work on this branch, may have shifted lines.

Relevant existing folders to place the two new tag headers in (already-established feature
homes, confirmed by directory listing):

- `src/Renderer/Atmosphere/` (existing files: `AtmosphereLutRenderer.h/.cpp`,
  `AtmosphereTypes.h`, etc.)
- `src/Renderer/GpuSkinning/` (existing files: `GpuSkinningPipelines.h/.cpp`, etc.)

`AtmosphereLutRenderer`'s constructor is currently `AtmosphereLutRenderer() = default;`
(declared inline in the header, `AtmosphereLutRenderer.h` ~line 106) — this phase must turn
it into a real, declared-only constructor with a body in `AtmosphereLutRenderer.cpp`, so it
can call `rg::RegisterPassGroupLabel(...)` once.

---

## Step 3 — The Plan

### 3.1 New file: `src/Renderer/Atmosphere/AtmosphereRenderPassTags.h`

```cpp
#pragma once

// render-pass-7 campaign (task_manager/render-pass-7), PHASE3 - Core Campaign 1
// ("De-hardcode RenderPassCategory"). This feature's OWN RenderPassTag bit - lives here,
// in the Atmosphere feature's own header, NEVER in a Core RenderGraph file (see
// RenderPassTag's own doc comment, RenderGraphTypes.h, for the rule this file exists to
// follow). Every AtmosphereLutRenderer pass that should be grouped under the Editor Frame
// Debugger's "Compute LUT" heading stamps this tag via AddRenderPass()'s trailing `tags`
// argument - see AtmosphereLutRenderer.cpp's own AddXxxLutPass() methods, and this same
// feature's own RegisterPassGroupLabel() call (AtmosphereLutRenderer's constructor) that
// actually gives this bit its "Compute LUT" heading.

#include "../RenderGraph/RenderGraphTypes.h"

namespace gte {

inline constexpr rg::RenderPassTag kAtmosphereLutPassTag{ 1ull << 0 };

} // namespace gte
```

(Bit index `0` is a free, arbitrary choice for this campaign's first real tag consumer — if a
later, concurrent phase/branch has ALREADY claimed bit 0 for something else by the time you
implement this, pick the next free bit and say so explicitly in this phase's completion
report; there is no central bit-allocation authority yet, and inventing one is explicitly
out of scope for this campaign — see `PHASE0`'s non-goals.)

### 3.2 New file: `src/Renderer/GpuSkinning/GpuSkinningRenderPassTags.h`

Same shape, different feature, different bit:

```cpp
#pragma once

// render-pass-7 campaign (task_manager/render-pass-7), PHASE3 - Core Campaign 1. This
// feature's OWN RenderPassTag bit - see AtmosphereRenderPassTags.h's identical header
// comment for the full "why". Deliberately has NO matching RegisterPassGroupLabel() call
// anywhere (see PHASE0's own Step 2.3) - a GPU Skinning compute dispatch keeps falling into
// the Editor Frame Debugger's generic "Compute Dispatches (Pre-GameView)" fallback bucket,
// exactly like it always has - this tag exists so GPU Skinning is a real, structurally
// identifiable pass group (proving the mechanism generically), without changing one pixel
// of today's actual Frame Debugger tree output.

#include "../RenderGraph/RenderGraphTypes.h"

namespace gte {

inline constexpr rg::RenderPassTag kGpuSkinningDispatchPassTag{ 1ull << 1 };

} // namespace gte
```

### 3.3 Trim `RenderPassCategory` (`RenderGraphTypes.h`)

Remove the `AtmosphereLut`/`GpuSkinning` enumerators and their doc-comment lines. Rewrite the
enum's own header comment (currently references both by name) to describe only the two
remaining, genuinely Core-level values. Result:

```cpp
enum class RenderPassCategory : std::uint8_t {
    General, // The default - no special Frame Debugger grouping treatment.
    Debug,   // Frame-Debugger-internal replay passes / Compute Blur Validation - never a
             // real Frame Debugger tree citizen themselves.
};
```

### 3.4 Update `ToString(RenderPassCategory)` (`RenderGraphTypes.cpp`)

Trim the switch to 2 cases, keep the "no `default:` case, ever" discipline and the trailing
`return "Unknown";` fallback exactly as every sibling `ToString()` in this file already does.

### 3.5 Migrate the 5 `AtmosphereLutRenderer.cpp` call sites

For EACH of the 5 call sites listed in `PHASE0` Step 2.2 (re-read the live file/line first):
change `rg::RenderPassCategory::AtmosphereLut` to `rg::RenderPassCategory::General`, and
append a new trailing argument, `kAtmosphereLutPassTag.bit`, to that same `AddRenderPass()`
call. Since the new `tags` parameter (PHASE1) sits AFTER `drawKind`/`renderPassEvent` in the
parameter list, and none of these 5 call sites currently specify those two explicitly (they
rely on their defaults), you must spell out the two defaults explicitly to reach past them —
**re-read each call site's actual current behavior first** (do not assume `DrawMesh`/
`Opaques` blindly; confirm by reading `RenderGraphBuilder.h`'s current default values for
`drawKind`/`renderPassEvent` at the time you implement this), e.g.:

```cpp
builder.AddRenderPass(
    "AtmosphereTransmittanceLutPass", rg::PassKind::Compute, rg::ViewScope::Shared, rg::RenderPassCategory::General,
    [outputHandle](rg::RenderGraphBuilder::PassBuilder& pass) { ... },
    [this, &renderer, outputHandle](rg::PassContext& ctx) { ... },
    rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::Opaques, kAtmosphereLutPassTag.bit);
```

Add `#include "AtmosphereRenderPassTags.h"` to `AtmosphereLutRenderer.cpp`. The 6th call site
at ~line 784 (`AtmosphereAerialPerspectiveCompositePass`, already `General`) is explicitly
**left untouched** — it needs no tag, per `PHASE0` Step 2.2's own table.

### 3.6 Register the "Compute LUT" heading

`AtmosphereLutRenderer.h`: change `AtmosphereLutRenderer() = default;` to a plain declared
constructor, `AtmosphereLutRenderer();` (no body in the header). `AtmosphereLutRenderer.cpp`:
add the definition:

```cpp
AtmosphereLutRenderer::AtmosphereLutRenderer()
{
    // render-pass-7 campaign, PHASE3 - this feature registers its OWN Frame Debugger
    // grouping heading for its OWN tag, from its OWN file - Core never learns this string
    // exists. Idempotent (RenderPassGroupRegistry.h) - safe even if more than one instance
    // of this class is ever constructed in the same process (e.g. Tier-1 tests).
    rg::RegisterPassGroupLabel(kAtmosphereLutPassTag, "Compute LUT");
}
```

Add `#include "../RenderGraph/RenderPassGroupRegistry.h"` to `AtmosphereLutRenderer.cpp`.
Double-check every other translation unit that constructs an `AtmosphereLutRenderer` still
compiles (this only changes it from an implicit to an explicit, still-default-argument-free,
still-trivially-callable default constructor — no call site should need edits, but verify by
building).

### 3.7 Migrate the `"GpuSkinning"` `RenderPipeline` provider (`Application.cpp` ~line 452)

Delete the line `desc.legacyCategory = rg::RenderPassCategory::GpuSkinning;` (the field
defaults to `General` already — no need to set it explicitly). Add
`desc.tags = kGpuSkinningDispatchPassTag.bit;` on its own line nearby. Add
`#include "../Renderer/GpuSkinning/GpuSkinningRenderPassTags.h"` (match this file's own
existing include style/relative-path convention — check its current include block first)
to `Application.cpp`.

### 3.8 Migrate `AddGpuSkinningPasses()` (`RenderPasses.cpp` ~line 409)

Same treatment as 3.5: `rg::RenderPassCategory::GpuSkinning` → `rg::RenderPassCategory::General`,
append trailing `rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::Opaques,
kGpuSkinningDispatchPassTag.bit` (re-read the live call site's current args first — this one
already spans multiple lines with a lambda `setup`/`execute` pair; confirm the exact current
defaults before spelling them out). Add the matching `#include` to `RenderPasses.cpp`.

### 3.9 Update every existing test referencing the deleted enumerators

Enumerated exhaustively (from `search_in_dir` results already gathered for this campaign —
re-confirm via a fresh `search_in_dir` for `RenderPassCategory::AtmosphereLut` and
`RenderPassCategory::GpuSkinning` across `tests/` before starting, in case anything shifted):

- `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` — lines ~233, ~239, ~300, ~1523
  (`.category = rg::RenderPassCategory::GpuSkinning/AtmosphereLut`). **Do not fully rewrite
  this file's grouping-behavior tests in THIS phase** — PHASE4 owns that (it depends on
  PHASE4's own rewritten `FrameDebuggerData.cpp` logic). For THIS phase, only make the file
  COMPILE again: replace each `.category = rg::RenderPassCategory::AtmosphereLut;` with
  `.tags = kAtmosphereLutPassTag.bit;` and each `.category = rg::RenderPassCategory::GpuSkinning;`
  with `.tags = kGpuSkinningDispatchPassTag.bit;`, add
  `rg::RegisterPassGroupLabel(kAtmosphereLutPassTag, "Compute LUT");` (plus
  `rg::ResetPassGroupRegistryForTesting();` beforehand) at the top of every test that needs
  the "Compute LUT" grouping to still occur, and add the needed
  `#include`s (`AtmosphereRenderPassTags.h`, `GpuSkinningRenderPassTags.h`,
  `RenderPassGroupRegistry.h`). **Leave `MakeGraphicsPassWithCategory()`
  (its `Debug`-category uses, lines ~1323/1325, are still valid and unaffected) exactly as
  is.** Confirm these specific tests pass again with THIS phase's migration alone, even
  before PHASE4's own rewrite of the production consumer logic — if `FrameDebuggerData.cpp`
  itself has not been rewritten yet (that is PHASE4's job), these tests will only pass once
  PHASE4 lands; note explicitly in this phase's own completion report that this specific
  test file is EXPECTED to still show pre-existing failures until PHASE4 completes, and do
  not attempt to force it green here — that would mean prematurely doing PHASE4's own work.
- `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp` — the
  `RenderGraphRenderPassCategoryTest` test suite (~lines 532-553): trim the `values[]` array
  to `{General, Debug}` and the two `ToString()`-equality assertions to match; delete the
  `AtmosphereLut`/`GpuSkinning` lines entirely.
- `tests/Renderer/RenderGraph/RenderPassTests.cpp` — line ~72/80
  (`AddRenderPassFourArgumentOverloadStampsViewScopeAndCategory`) currently asserts
  `RenderPassCategory::AtmosphereLut` — change the fixture to use `RenderPassCategory::General`
  plus the new trailing `tags` argument (`kAtmosphereLutPassTag.bit`), and update the
  assertion to `EXPECT_EQ(input.passes[0].tags, kAtmosphereLutPassTag.bit);` in addition to
  the still-valid `EXPECT_EQ(input.passes[0].category, RenderPassCategory::General);`. Rename
  the test if its name ("...StampsViewScopeAndCategory") no longer reflects what it proves
  (e.g. "...StampsViewScopeCategoryAndTags").
- `tests/Renderer/RenderGraph/RenderPipelineTests.cpp` — line ~301
  (`desc.legacyCategory = RenderPassCategory::AtmosphereLut;`, test
  `LegacyCategoryAndDrawKindSurviveUnchangedIntoTheProducedPassRecord`) — change to
  `desc.legacyCategory = RenderPassCategory::General;` plus
  `desc.tags = kAtmosphereLutPassTag.bit;`, and update the corresponding assertion from
  `EXPECT_EQ(input.passes[0].category, RenderPassCategory::AtmosphereLut);` to
  `EXPECT_EQ(input.passes[0].category, RenderPassCategory::General);` plus a new
  `EXPECT_EQ(input.passes[0].tags, kAtmosphereLutPassTag.bit);` (this overlaps with, but is a
  separate concern from, PHASE1's own dedicated `DeclareIntoForwardsTagsOntoTheUnderlyingPassRecord`
  test — both should exist; this one specifically proves category+tags travel together
  correctly through the SAME real fixture pattern this file already established).
- `tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp` — confirm (via fresh
  `search_in_dir`) whether this file references either deleted enumerator directly; the
  earlier campaign-wide search found none, but re-confirm live before assuming so.

### 3.10 Build + targeted test run

Incremental build (`cmake --build build`). Run the targeted test subset for every file
touched in 3.9 EXCEPT `FrameDebuggerSnapshotBuilderTests.cpp` (expected to still fail until
PHASE4 — do not treat that as a regression in THIS phase, but DO confirm it fails for the
EXPECTED reason — a grouping-behavior mismatch — not a compile error or an unrelated crash).

### 3.11 Report

Write `PHASE3_COMPLETION_REPORT.md`, explicitly listing: the exact bit values chosen for both
new tags, the exact list of edited call sites (confirmed against live line numbers actually
used), and the explicit, expected, temporary red state of
`FrameDebuggerSnapshotBuilderTests.cpp` pending PHASE4. Commit via `git_add`/`git_commit`.

### Acceptance bar for this phase

- `RenderPassCategory` has exactly 2 enumerators anywhere in the codebase — confirm via a
  fresh `search_in_dir` for `AtmosphereLut`/`GpuSkinning` across all of `src/`/`tests/`
  returning zero hits tied to `RenderPassCategory` (a few doc-comment-only historical
  mentions inside OTHER campaigns' own `task_manager/*/*.md` files, or `AGENTS.md`'s own
  historical prose, are fine and out of scope to edit here — only source/test CODE matters).
- Every real production pass that used to carry a feature-named category now carries the
  exact same DISTINGUISHING information via a real, non-zero `tags` value instead.
- Full incremental build succeeds.
- Every touched test file compiles; every touched test PASSES except the explicitly-expected,
  explicitly-documented temporary `FrameDebuggerSnapshotBuilderTests.cpp` grouping-behavior
  failures, which PHASE4 resolves next.
