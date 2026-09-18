# PHASE1 COMPLETION REPORT — Render Pass Draw-Kind Vocabulary

_Child of `PHASE0_MASTER_STRATEGY.md` (`task_manager/render-pass-2/`). This
phase's own strategy file: `PHASE1_RENDER_PASS_DRAW_KIND_VOCABULARY.md`._

## What Was Done

Implemented the plan in `PHASE1_RENDER_PASS_DRAW_KIND_VOCABULARY.md` exactly
as written, step by step:

1. **`src/Renderer/RenderGraph/RenderGraphTypes.h`** — added
   `enum class RenderPassDrawKind : std::uint8_t { DrawMesh, DrawQuad, Blit };`
   plus `const char* ToString(RenderPassDrawKind drawKind) noexcept;`
   immediately after the existing `RenderPassCategory` enum + its `ToString()`
   declaration, with the exact doc comment from the plan (Step 3.1). Also
   appended `RenderPassDrawKind drawKind = RenderPassDrawKind::DrawMesh;` as
   the new LAST field of `PassRecord`, immediately after `category` (Step
   3.2).
2. **`src/Renderer/RenderGraph/RenderGraphTypes.cpp`** — added the
   `default:`-free `ToString(RenderPassDrawKind)` definition immediately
   after `ToString(RenderPassCategory)` (Step 3.3).
3. **`src/Renderer/RenderGraph/RenderGraphSnapshot.h`** — added
   `RenderPassDrawKind drawKind = RenderPassDrawKind::DrawMesh;` to
   `RenderGraphPassSnapshot`, immediately after `category` (Step 3.4).
4. **`src/Renderer/RenderGraph/RenderGraphSnapshot.cpp`** — `BuildPassSnapshot()`
   now also copies `snapshot.drawKind = pass.drawKind;` immediately after the
   existing `snapshot.category = pass.category;` line, unconditionally, for
   both the surviving-pass loop and the culled-pass loop (both routed through
   the same shared helper, so one line covers both) (Step 3.4).
5. **`src/Renderer/RenderGraph/RenderGraphBuilder.h`** — both `AddRenderPass()`
   overloads (the full 6-argument form and the 4-argument convenience form)
   now accept a new, trailing, defaulted `RenderPassDrawKind drawKind =
   RenderPassDrawKind::DrawMesh` parameter, stamped onto `m_passes.back()`
   exactly like `category` already is. Added doc comments explaining the
   "trailing defaulted plain-type parameter never touches template
   deduction" reasoning (Step 3.5).
6. **`src/Application/RenderPasses.cpp`** — `AddDrawSkyBackgroundPass()`'s
   `builder.AddRenderPass(...)` call now passes `rg::RenderPassDrawKind::DrawQuad`
   explicitly as its new trailing argument, with a comment referencing
   `AtmosphereSkyBackgroundRenderer.cpp`'s own `vkCmdDraw(cmd, 3, 1, 0, 0)`
   fact. Added a one-line doc-comment addition above the function referencing
   this campaign (Step 3.6).
7. **Verified by grep** (`search_in_dir` for `AddRenderPass(` across `src/`)
   that every other pre-existing call site (`AddRenderOpaquePass()`,
   `AddFrameDebuggerReplayPasses()`'s per-step loop, `AddPresentPass()`,
   `AddGpuSkinningPasses()` in `RenderPasses.cpp`; `ComputeBlurValidation.cpp`;
   all six passes in `AtmosphereLutRenderer.cpp`) ends its call exactly at the
   `execute` lambda argument with no trailing argument — every one of them
   compiles completely unmodified against the new trailing defaulted
   parameter (Step 3.5's own required verification).
8. **Tests added** (Step 3.7):
   - `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp` — two new tests,
     `RenderGraphRenderPassDrawKindTest.ToStringCoversEveryEnumeratorNonNullNonEmpty`
     and `...ToStringProducesDistinctNamesForDistinctEnumerators`, mirroring
     the existing `PassKind`/`RenderPassCategory` `ToString()` test shape.
   - `tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp` — two new
     tests, `RenderGraphSnapshotTest.DrawKindIsCopiedThroughForSurvivingPass`
     and `...DrawKindIsCopiedThroughForCulledPass`, mirroring
     `ViewScopeIsCopiedThroughForSurvivingAndCulledPasses`'s own one-surviving
     / one-culled assertion shape (per this phase's own "Correction from this
     document's own double-check pass" note — there is no `category`-specific
     copy-through test to mirror, so `kind`/`viewScope`'s tests were used as
     the template instead).
   - `tests/Renderer/RenderGraph/RenderPassTests.cpp` — four new tests
     covering both `AddRenderPass()` overloads' default-vs-explicit `drawKind`
     behavior: `AddRenderPassSixArgumentOverloadDefaultsDrawKindToDrawMeshWhenOmitted`,
     `AddRenderPassSixArgumentOverloadStoresExplicitDrawKind`,
     `AddRenderPassFourArgumentOverloadDefaultsDrawKindToDrawMeshWhenOmitted`,
     `AddRenderPassFourArgumentOverloadStoresExplicitDrawKind`.

## Deviations From The Plan

None. Every step (3.1 through 3.7) was implemented exactly as specified, at
the exact line locations the plan predicted (confirmed by direct source read
before each edit). No design ambiguity was hit that the plan didn't already
resolve, so `ask_questions` was not needed for this phase.

## Verification

- `cmake --build build --target GreatTamanaEngine` (working directory
  `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`) — **succeeded, zero
  errors, zero warnings** (46/46 build steps, `GreatTamanaEngine.exe` linked
  successfully).
- `cmake --build build --target GreatTamanaEngineTests` — also run as an
  ADDITIONAL incremental compile-only check (the new tests are already wired
  into `tests/CMakeLists.txt`'s existing source list, since they were added to
  pre-existing test files) — **succeeded, zero errors, zero warnings**
  (14/14 build steps, `GreatTamanaEngineTests.exe` linked successfully). Per
  PHASE0's Cross-Cutting Rules, the resulting test binary/`ctest` was
  DELIBERATELY NOT RUN this phase — that full regression pass is reserved for
  PHASE4 only.

## Definition of Done — Status

- [x] `rg::RenderPassDrawKind` exists in `RenderGraphTypes.h` with exactly
      three enumerators (`DrawMesh`/`DrawQuad`/`Blit`), plus a
      `default:`-free `ToString()` in `RenderGraphTypes.cpp`.
- [x] `PassRecord::drawKind` exists, defaults to `DrawMesh`, appended at the
      end of the struct.
- [x] `RenderGraphPassSnapshot::drawKind` exists, defaults to `DrawMesh`, and
      `BuildPassSnapshot()` copies it through unconditionally (surviving AND
      culled passes alike).
- [x] Both `RenderGraphBuilder::AddRenderPass()` overloads accept a new,
      trailing, defaulted `RenderPassDrawKind` parameter — every pre-existing
      call site across `src/` compiles completely unmodified.
- [x] `AddDrawSkyBackgroundPass()` explicitly tags its pass
      `RenderPassDrawKind::DrawQuad`.
- [x] New Tier-1 tests cover `ToString()`, the snapshot copy-through
      (surviving + culled), and both `AddRenderPass()` overloads'
      default-vs-explicit `drawKind` behavior.
- [x] `cmake --build build --target GreatTamanaEngine` succeeds with zero
      errors/warnings.
- [x] `PHASE1_COMPLETION_REPORT.md` written and committed alongside the code.

## What Was NOT Done (by design, per this phase's own scope)

- No third `PassKind` enumerator was added; `RenderGraph.cpp`/
  `RenderGraphCompiler.cpp`/`RenderGraphBarrierPlanner.cpp` were not touched.
- No real Blit/copy pass was implemented anywhere — `Blit` remains a
  real-but-unused scaffold value.
- `src/Editor/FrameDebuggerData.cpp` and
  `src/Editor/Panels/FrameDebuggerPanel.cpp` were not touched — that is
  PHASE2's job.
- No full build or `ctest` regression run was performed — reserved for
  PHASE4.

## Next Step

PHASE2 (`PHASE2_FRAME_DEBUGGER_UNIFIED_PASS_OWNERSHIP_REWORK.md`) can now
consume `RenderGraphPassSnapshot::drawKind` to label each Graphics-kind
pass's new Frame Debugger child event row (`"Draw Mesh"`/`"Draw Quad"`/
`"Blit"`) without ever hardcoding a pass-name string comparison.
