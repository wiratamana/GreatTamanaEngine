# PHASE3 — Completion Report: Pipeline Layer, N-Target PSO

Parent: `PHASE0_MASTER_STRATEGY.md`. Phase document:
`PHASE3_PIPELINE_LAYER_N_TARGET_PSO.md`.

## Status: DONE

`PHASE1_COMPLETION_REPORT.md` and `PHASE2_COMPLETION_REPORT.md` were both read
in full before starting, per this phase's own prerequisite (and this
campaign's top-level instructions). Neither report recorded anything that
changed this phase's own plan — PHASE2's handoff notes confirm
`RenderGraph::ExecuteCompiledGraph()` now records a real, correct
N-color-attachment `vkCmdBeginRendering` for a 2+-write pass, but that "no
real `Pipeline` can yet bind against such a pass" without this phase's own
work, exactly as `PHASE0`'s Phase Map anticipated.

## Administrative note (branch), re-confirmed per this campaign's own
recursive instruction

This phase's own top-level task instructions again said "Stay on the current
branch: `feature/logger-impl`" — the same stale/mistaken instruction
`PHASE2_COMPLETION_REPORT.md` already flagged and corrected once. Re-verified
directly via `git status` before starting: the repository's actual current
branch is `feature/render-pass-impl`, with a clean working tree (PHASE1/
PHASE2's commits already present). Stayed on `feature/render-pass-impl`;
`feature/logger-impl` was never touched. This did not need a fresh
`ask_questions` round-trip — PHASE2's own completion report already recorded
the exact same correction and instructed future phases to re-verify instead
of assuming, which is what this phase did.

## Re-confirmation of the corrected Definition of Done (task's own instruction)

Before finishing, re-ran `search_in_dir` for `.CreatePipeline(` across all of
`src/` (not trusting the phase document's own already-stated claim blindly).
Result: exactly the same three hits the corrected phase document/task
description predicted —

- `Game/Instantiation/MeshAssetGpuCatalog.cpp` (`Mesh`/`TexturedMesh`
  pipelines)
- `Game/Instantiation/PrimitiveGpuCatalog.cpp` (default/Triangle primitive
  pipeline)
- `Renderer/Renderer.cpp` line 298 (`Renderer::CreatePipeline()`'s own
  internal forwarding call into `m_resources.CreatePipeline()` — not an
  external call site, just the one link in the existing chain)

No sky-background/scene-grid/mesh-preview/GPU-skinning/Atmosphere call site of
any kind appeared, confirming the correction noted in this task's own
instructions and in the phase document's Definition of Done is still
accurate as of this phase actually running — nothing changed in the engine
between when that correction was written and now.

## What was done

### 3.1 — `Pipeline`: added an N-format constructor, kept the 1-format one

**Shape (a) — Overload — was used, the phase document's own RECOMMENDED
shape.** No `ask_questions` was needed: the document only asked to use it "if
genuinely torn," and shape (a) was clearly preferable here (single PSO-
building code path, smaller/cleaner diff, no risk to the one existing
canonical constructor's own parameter list).

- `Pipeline.h`: added a new constructor overload,
  `Pipeline(VkDevice, std::span<const VkFormat> colorFormats, VkFormat depthFormat, ...)`,
  with the exact same trailing defaulted parameters
  (`vertexLayout`/`materialSetLayout`/`debugName`) as the existing
  single-format constructor. Added `#include <span>`/`#include <cstddef>`.
- `Pipeline.cpp`: the ENTIRE previous constructor body now lives in this new
  span-taking constructor. The original single-`VkFormat` constructor is now
  a genuinely thin, delegating-constructor forwarder:
  ```cpp
  Pipeline::Pipeline(VkDevice device, VkFormat colorFormat, VkFormat depthFormat, ...)
      : Pipeline(device, std::span<const VkFormat>(&colorFormat, 1), depthFormat, ...)
  {
  }
  ```
  This compiled cleanly with no double-catch/try issue — the delegating
  constructor's own body is empty, so the target constructor's existing
  try/catch shader-module-cleanup structure runs exactly once, unmodified,
  exactly as the phase document anticipated as the likely-safe case (the
  fallback "shared private helper" shape was not needed).
- Inside the span constructor: the single `VkPipelineColorBlendAttachmentState`/
  `colorBlend.attachmentCount = 1` pair became a
  `std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments(colorFormats.size())`
  loop (all entries identical — opaque, no blending, full RGBA write mask),
  and `renderingInfo.colorAttachmentCount`/`pColorAttachmentFormats` are now
  built from `colorFormats.size()`/`colorFormats.data()` instead of the old
  hardcoded `1`/`&colorFormat`. Every other line (shader module load/destroy,
  vertex layout switch, viewport/rasterizer/multisample/depth-stencil state,
  push-constant range, `VkPipelineLayoutCreateInfo`, dynamic state, the
  `vkCreatePipelineLayout`/`vkCreateGraphicsPipelines` calls and their error
  handling) is **byte-for-byte unchanged** from before this phase.
- A debug-only `assert(!colorFormats.empty() && colorFormats.size() <= kPipelineMaxColorAttachments && ...)`
  guards the cap (Locked Design Decision 2, mirroring
  `RenderGraphBuilder::PassBuilder::WriteColorAttachment()`'s own identical
  assert from PHASE1).

**`kPipelineMaxColorAttachments` disposition (the design choice PHASE3's own
document explicitly flagged as needing `ask_questions` "if unsure")**: chose
the LOCAL CONSTANT option, `inline constexpr std::size_t kPipelineMaxColorAttachments = 8;`,
placed in `Pipeline.h` right after `VertexLayout`, rather than `#include`-ing
`RenderGraphTypes.h` to reach `gte::rg::kMaxColorAttachments` directly.
Reasoning, without needing to ask the user:

- The phase document itself already framed this as "if pulling in
  `RenderGraphTypes.h` here feels architecturally wrong ... instead define a
  LOCAL constant ... Use `ask_questions` if unsure which direction this
  project's own layering convention prefers" — i.e. it offered a clear,
  ready-made default alternative rather than leaving this fully open.
- `AGENTS.md`'s own "Clean Architecture" guideline states "Lower-level/core
  layers must not depend on higher-level or framework-specific details."
  `Pipeline` is a foundational Renderer primitive constructible with zero
  RenderGraph involvement at all (the original hardcoded triangle demo still
  builds one directly); `gte::rg` (`Renderer/RenderGraph/`) is conceptually
  layered ON TOP of `Renderer`/`Pipeline`, per this same campaign's own
  `PHASE0_MASTER_STRATEGY.md` wording ("a higher-level namespace built ON TOP
  of Renderer"). Reaching upward from `Pipeline.h` into `rg::` would invert
  that direction for a single shared integer constant — not worth the new
  cross-namespace include.
- Confirmed via re-reading `Pipeline.h`/`.cpp` before making this call: they
  include nothing from `Renderer/RenderGraph/` today (they did not, exactly
  as the phase document's own research already found).
- The comment on `kPipelineMaxColorAttachments` explicitly cross-references
  `gte::rg::kMaxColorAttachments` and instructs future maintainers to keep
  both in sync if either ever changes — the same "kept in sync by comment
  cross-reference" pattern the phase document's own sketch code explicitly
  suggested.

### 3.2 — `GpuResourceFactory`/`Renderer`: parallel N-format overloads

- `GpuResourceFactory.h`/`.cpp`: added
  `Pipeline CreatePipeline(std::span<const VkFormat> colorFormats, ...)`,
  forwarding straight into `Pipeline`'s new span constructor. The existing
  single-`VkFormat` overload's signature and body are **completely
  untouched** (kept forwarding into `Pipeline`'s single-format constructor,
  which itself now internally delegates — see 3.1 above — the smallest
  possible `GpuResourceFactory` diff, per the phase document's own stated
  preference).
- `Renderer.h`/`.cpp`: added a parallel
  `Pipeline CreatePipeline(std::span<const VkFormat> colorFormats, ...)`
  overload. Unlike the existing single-format `Renderer::CreatePipeline()`
  (which always auto-supplies exactly `ColorFormat()`, a single value, per
  PHASE3's own Step 2 research), this new overload takes the caller's
  `colorFormats` list **explicitly** — a genuine multi-target pass (e.g. a
  future G-buffer pass) may write into targets of differing formats, so
  auto-injecting a single `ColorFormat()` value would not make sense here.
  This was not an ambiguity needing `ask_questions`: the phase document's own
  Step 2 research already established that `Renderer::CreatePipeline()` has
  no existing "explicit override" parameter to preserve compatibility with,
  so there was nothing to reconcile — this is a genuinely new, additive
  overload with no existing behavior to match beyond "keep working
  unmodified," which the untouched existing overload already satisfies.
- Both new overloads added `#include <span>` to their respective headers.
  No existing call site of either `CreatePipeline()` needed any change —
  confirmed by the successful build (Verification section below).

### 3.3 — What this phase did not touch

- No real pass calls the new N-format overload yet (PHASE4's job) —
  confirmed via the `search_in_dir` re-check above: zero new call sites of
  either new overload exist anywhere in `src/` yet.
- `RenderGraph.cpp`, `RenderGraphBuilder.h`/`.cpp`, `RenderGraphTypes.h`/
  `.cpp` — untouched, exactly as scoped (already done in PHASE1/PHASE2).

## Live-smoke-instantiation check: DEFERRED to PHASE4

The phase document's own Definition of Done explicitly allows deferring this
specific check to PHASE4 "if this is impractical without PHASE4's shader/
pass existing yet." That is the case here: building a genuine, throwaway
2-format `Pipeline` requires a REAL compiled SPIR-V fragment shader that
actually declares 2 `layout(location = N) out` color outputs (a
`VkPipelineRenderingCreateInfo` NEEDS a real `VkGraphicsPipelineCreateInfo`
with real shader stages — there is no way to construct a `Pipeline` at all
without loading real `.vert`/`.frag` SPIR-V off disk). No such shader exists
anywhere in the engine yet — PHASE4
(`PHASE4_GBUFFER_VALIDATION_PASS_AND_SHADER.md`) is explicitly where
`Shaders/GBufferValidation.frag`/`.vert` (2-3 outputs) get built. Writing a
one-off throwaway shader here, purely to exercise this constructor once,
would have meant doing PHASE4's own shader-authoring work early and out of
order, or building a second, redundant, still-throwaway shader that PHASE4
would then duplicate anyway. **Decision: defer the live smoke instantiation
check entirely to PHASE4's own Definition of Done**, which already plans to
build a real `GBufferValidation` pass through this exact new code path —
that pass succeeding IS the live smoke test. This did not require
`ask_questions` — the phase document's own Definition of Done explicitly
pre-authorized this exact deferral and asked only that this report record
which path was taken (done, here).

## Verification

- **Fast, targeted incremental compile check**:
  `cmake --build build --target GreatTamanaEngineTests` — **succeeded**, no
  warnings promoted to errors. This target transitively rebuilds `gte_core`,
  including every touched file (`Pipeline.cpp`, `GpuResourceFactory.cpp`,
  `Renderer.cpp`) and every real, existing consumer
  (`MeshAssetGpuCatalog.cpp`, `PrimitiveGpuCatalog.cpp`, and everything else
  under `Game/`/`Editor/`/`Application/` that depends on `gte_core`),
  confirming every existing call site still compiles completely unmodified.
- **Test binary run, filtered to the touched/related suites**:
  `GreatTamanaEngineTests.exe --gtest_filter=RenderGraph*:Pipeline*` —
  **209/209 passed** (no test name matched `Pipeline*` — `Pipeline` remains
  Tier 2/GPU-dependent with no automated test coverage, exactly as
  `AGENTS.md`'s "Testability & Regression Safety" section already documents
  as a known, accepted, non-blocking gap; every one of the 209 matches came
  from the `RenderGraph*` half of the filter, all passing unchanged,
  including every PHASE1/PHASE2 test).
- Per `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision 8, no full build
  or full `ctest` was run — that is PHASE5's job only.
- No live running-engine check was performed this phase (see "Live-smoke-
  instantiation check" above for why, and where it moved to).

## Design decisions made (not fully pinned down verbatim by PHASE0/PHASE3)

1. **Branch re-confirmation** — see the dedicated section near the top of
   this report. Stayed on `feature/render-pass-impl`. No `ask_questions`
   needed (PHASE2 already established the correction and the "re-verify,
   don't assume" instruction).
2. **Shape (a) (overload via delegating constructor)** for `Pipeline`'s new
   N-format constructor — the phase document's own recommended shape; no
   reason found to prefer shape (b). No `ask_questions` needed.
3. **`kPipelineMaxColorAttachments` as a local constant**, not an `#include`
   of `RenderGraphTypes.h` — see the dedicated reasoning in Step 3.1 above.
   No `ask_questions` needed (the phase document's own text already supplied
   a ready-made default and the reasoning to choose it).
4. **Live-smoke-instantiation check deferred to PHASE4** — see the dedicated
   section above. No `ask_questions` needed (explicitly pre-authorized by
   the phase document's own Definition of Done).

No other genuine ambiguity was hit during this phase.

## Deviations from the plan

None beyond the four documented decisions above, all of which were already
anticipated/pre-authorized by the phase document itself (either as its own
recommended default, an explicitly offered alternative, or an explicitly
allowed deferral) rather than being an unplanned deviation from a locked
instruction.

## Files changed

- `src/Renderer/Pipeline.h` (new `kPipelineMaxColorAttachments` constant, new
  `std::span<const VkFormat>` constructor declaration, `#include <span>`/
  `<cstddef>`, doc-comment updates)
- `src/Renderer/Pipeline.cpp` (the actual PHASE3 rewrite: single-format
  constructor now delegates; new span constructor holds the real N-target
  PSO-building logic; `#include <span>`/`<cassert>`)
- `src/Renderer/GpuResourceFactory.h` (new `CreatePipeline()` span overload
  declaration, `#include <span>`)
- `src/Renderer/GpuResourceFactory.cpp` (new `CreatePipeline()` span overload
  implementation)
- `src/Renderer/Renderer.h` (new `CreatePipeline()` span overload
  declaration, `#include <span>`)
- `src/Renderer/Renderer.cpp` (new `CreatePipeline()` span overload
  implementation)
- `task_manager/mrt-1/PHASE3_COMPLETION_REPORT.md` (this file)

## Handoff notes for PHASE4

- `Renderer::CreatePipeline(std::span<const VkFormat> colorFormats, ...)` is
  ready to be called with 2-3 real color formats once
  `Shaders/GBufferValidation.frag`/`.vert` exist — that call, and the
  resulting `Pipeline`'s first real `vkCreateGraphicsPipelines` against a
  genuine multi-attachment `VkPipelineRenderingCreateInfo`, IS this
  campaign's still-owed live smoke test (see "Live-smoke-instantiation
  check" above) — PHASE4 should treat a clean, validation-layer-error-free
  construction as satisfying that proof, not as a separate, additional
  requirement.
- `kPipelineMaxColorAttachments` (`Pipeline.h`) and `gte::rg::kMaxColorAttachments`
  (`RenderGraphTypes.h`) are two independent constants that must be kept in
  sync by hand (both currently `8`) — if a future change ever needs to raise
  the cap, both must be updated together; nothing enforces this
  automatically today (a deliberate, documented trade-off, not an oversight).
- Every existing single-format `Pipeline`/`GpuResourceFactory::CreatePipeline()`/
  `Renderer::CreatePipeline()` call site is confirmed compiling and behaving
  identically (see Verification above) — PHASE4 can build its new G-buffer
  validation pass entirely additively, with zero risk to any existing pass.
