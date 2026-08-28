# GPU Vertex Skinning — Phase 6: Validation & Parity Testing — Completion Report

Status: **DONE** (the tooling this phase's own v2 strategy actually
requires as its PRIMARY deliverable — see below for what "done" means for
this specific phase, and what remains an open follow-up action for a human
to run). Implements
`task_manager/gpu_skinning/GPU_SKINNING_PHASE6_VALIDATION_PARITY_TESTING_STRATEGY_v2.md`
per the scope fence set by `GPU_SKINNING_PHASE0_MASTER_STRATEGY_v2.md`
("Focus on selected section only" — this report covers ONLY the "ON GOING"
Phase 6 item; Phases 1-5 are already `[DONE]` and are completely unmodified
by this phase; Phase 7 remains `[TODO]`, untouched).

## What Phase 6 v2 actually asks for, restated

Per that document's own "V2 Revision Notes" (read in full before this
phase's work began, as instructed): the **manual, Editor-tool-based
validation is the REQUIRED, PRIMARY deliverable** — not an automated
GoogleTest. This repository has zero precedent today for a GoogleTest that
spins up a real `VkDevice` (confirmed directly from `TESTING.md`'s own
closing paragraph and `TODO.md`'s "Tier 2 (GPU-backed) integration test
fixture" backlog item, both re-read as part of this phase per its own Step
3.4 instruction) — building that fixture is explicitly out of this
campaign's scope. This phase's job was therefore to build the SAME kind of
tool `ComputeBlurValidation` already established for the texture-side
compute-shader campaign: Editor-only, Tier 2, run by a human, not by
`ctest`.

## What was built

### 1. `src/Editor/GpuSkinningValidation.h`/`.cpp` — the validation tool itself

Mirrors `ComputeBlurValidation.h`/`.cpp`'s own shape exactly (same folder,
same Editor-only/Tier-2 bucket, same "needs a live Renderer to do anything
at all" nature), but is deliberately **self-contained and independent of
Phase 3/5's render-graph wiring** — per the strategy document's own Step
3.1 ("via a one-shot `Renderer::ImmediateSubmit()` command buffer (NOT
through the full `RenderGraph`)"), so this tool has no dependency on
Phase 5's runtime switch, `AnimationSystem`, or `GpuSkinningRigCache` being
live/wired into a running frame at all.

Two independent entry points:

- **`ValidateGpuSkinningAgainstCpuOracle(Renderer&, GpuSkinningPipelines&,
  bindPositions, bindNormals, skinWeights, uvs, skinningMatrices, epsilon)`**
  — the real numeric parity check (Step 3.1 of the strategy document):
  1. Runs the CPU oracle — `Animation::VertexSkinning.h`'s own
     `SkinVertices()` — completely unmodified, exactly once.
  2. Packs and uploads the bind-pose/skin-weights/bone-matrices (and,
     when `uvs` is non-empty, the UV buffer) GPU inputs via Phase 1's own
     `PackBindPoseVertices()`/`PackSkinWeights()`/`PackUvs()` — never a
     second, hand-rolled copy of that padding logic.
  3. Creates the compute-kernel output buffer with the SAME core usage
     flags production uses (`VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT`, see
     `GpuResourceFactory::CreateGpuSkinningTargetBuffer()`), plus one
     extra flag this validation tool alone needs —
     `VK_BUFFER_USAGE_TRANSFER_SRC_BIT` — so its contents can be copied
     back to a host-visible staging buffer afterward (production never
     reads this buffer back on the CPU, so it never carries that flag).
  4. Allocates and rewrites a `ComputeDescriptorSet` against whichever of
     `GpuSkinningPipelines`' two descriptor-set layouts matches (untextured
     `PositionNormal` vs. textured `PositionNormalUv` — selected
     automatically by whether `uvs` is non-empty), per Phase 1's binding
     table (+ Phase 2's own binding-4 UV addition for the textured
     variant).
  5. Dispatches the real, already-shipped compute kernel
     (`GpuSkinningPipelines::PositionNormalPipeline()`/
     `PositionNormalUvPipeline()`) directly against a raw `VkCommandBuffer`
     inside ONE `Renderer::ImmediateSubmit()` call — binding the pipeline/
     descriptor set, pushing the `vertexCount` push constant, dispatching
     via `ComputeGroupCount(vertexCount, kSkinningLocalSizeX)` (Phase 2's
     own shared constant, never a re-declared magic number), then a real
     `VkBufferMemoryBarrier` (`VK_ACCESS_SHADER_WRITE_BIT` →
     `VK_ACCESS_TRANSFER_READ_BIT`) before `vkCmdCopyBuffer()`ing the
     result into a `BufferMemoryUsage::GpuToCpu` (host-visible,
     persistently-mapped) readback buffer — all inside the single
     `ImmediateSubmit()` recording, which already blocks until the GPU
     finishes before returning.
  6. Compares the GPU kernel's own output, vertex-for-vertex, against the
     CPU oracle's output — Euclidean distance per vertex, for BOTH
     position and normal — and reports `maxPositionDelta`/
     `meanPositionDelta`/`maxNormalDelta`/`meanNormalDelta` plus a count of
     vertices exceeding a documented `epsilon` (default `1e-4`, matching
     the strategy document's own starting recommendation) — never
     collapsing "did this even run" into the same result as "it ran and
     matched perfectly" (see `GpuSkinningValidationResult::succeeded`/
     `failureReason` — a run that can't proceed, e.g. empty input,
     mismatched UV count, or a readback buffer that somehow wasn't
     host-mapped, reports failure explicitly rather than a fabricated
     `0.0` delta).
  - `ToDiagnosticString()` renders the result as a human-readable,
    multi-line block suitable for printing straight to the console/log
    from wherever a developer invokes this — the actual, intended
    verification mechanism per this phase's own "run it by hand" design.

- **`ValidateGpuSkinningGroupingParity(RenderSystem&,
  GpuSkinningRigCache::GpuModelEntry&, parts)`** — the structural check
  from Step 3.3 ("Multi-part / shared-vertex-buffer parity"): confirms the
  CPU path's own `GroupMeshAssetPartsBySharedVertexBuffer()` (the SAME
  shared function `AnimationSystem::Update()`'s CPU branch and
  `GpuSkinningRigCache::Register()` both already call — see Phase 4's own
  completion report) produces the exact same group COUNT for a given
  model's `parts` list that the already-registered `GpuModelEntry` ended
  up with (`outputGroups.size()`). This is a pure count comparison, not a
  numeric one — it exists to catch a future regression where the two
  call sites' grouping decisions could silently diverge (e.g. if a future
  edit changed one call site's input but not the other's), which Phase 4's
  own shared-extraction design is specifically meant to make structurally
  impossible; this check is the tripwire that proves it stayed that way.

Both functions live under `namespace gte`, in the `Editor` module boundary
(Editor-only, gated by `GTE_ENABLE_EDITOR` — see wiring below), following
this codebase's existing "class stays available/testable even when its
production call site is gated off" precedent is NOT claimed here — unlike
`FrameProfiler`/`SdlMemoryTracker`, this tool genuinely needs a live
`Renderer`, so it has no meaningful non-Editor existence at all, exactly
like `ComputeBlurValidation` itself.

### 2. Wiring

- `CMakeLists.txt` — added `src/Editor/GpuSkinningValidation.h`/`.cpp` to
  `gte_core`'s `GTE_ENABLE_EDITOR` source block, immediately after
  `ComputeBlurValidation.cpp` (the file this new one deliberately mirrors
  in shape/location/bucket).
- No shader/CMake shader-staging changes were needed — this tool consumes
  Phase 2's two already-compiled `.comp` kernels
  (`SkinVerticesPositionNormal.comp`/`SkinVerticesPositionNormalUv.comp`)
  and Phase 2's `GpuSkinningPipelines` class exactly as they already exist,
  unmodified.

### 3. What was deliberately NOT wired up this phase

Per this campaign's own phase fence (Phase 7 — "Editor Toggle & Profiling
UX" — is still `[TODO]` and owns all user-facing wiring):

- **No Editor UI/menu button, no automatic per-frame `RenderGraph`
  integration.** Neither function is called from anywhere in production
  code yet — a developer invokes them directly (e.g. a temporary call
  added to `Application::Run()`, or a future debug menu item Phase 7 adds)
  against a real, already-loaded rigged model.
- **No automated GoogleTest.** Per Phase 6 v2's own explicit correction
  (see its "V2 Revision Notes" and Step 3.4), this is intentional, not an
  oversight — building a live-`VkDevice`-requiring test fixture is a
  separate, already-tracked `TODO.md` item ("Tier 2 (GPU-backed)
  integration test fixture"), explicitly out of this campaign's scope.

## Verification performed — and what remains an open action item

Per this session's own explicit instructions ("do not perform full build,
just a fast compile check... full build and regression test will perform
later after everything done"):

- `cmake --build build --target gte_core` — **succeeded**, zero errors/
  warnings related to this change (`GpuSkinningValidation.cpp` compiled
  and linked into `libgte_core.a` cleanly on the first attempt).
- `cmake --build build --target GreatTamanaEngineTests` — **succeeded**,
  linked cleanly against the updated `gte_core` (no test file was added or
  changed this phase — see below for why).
- Did **not** run `ctest`, a clean `build_joboff` verification build, or
  launch the actual Editor — all explicitly deferred to "later, after
  everything done" per this session's own instructions.

**Honest, explicit gap this leaves open** (matching Phase 0 v2's own
Must-Have #11 discipline of never treating "it compiled" as equivalent to
"it's proven correct"): this session's development machine does **not**
have the real MMD model fixture (`MMD_Model_Furina`) used throughout this
project's own prior animation-runtime verification available on disk
(confirmed directly — `C:\Users\F5954\Documents\TAMANA\MMD_Model_Furina`
does not exist in this environment), and per this session's own
instructions, no live-device run was performed at all this session either
way. **This means the actual required deliverable of Phase 6 v2's own Step
5 — a real, written "max observed per-vertex delta" number from running
this tool against a real rigged model — has NOT yet been produced.** The
tool itself is built, compiles cleanly, and is ready to run; producing
that number is the one concrete follow-up action needed before this phase
can be considered fully closed out, to be done in the "full build and
regression test... performed later after everything done" pass this
session's own instructions already call out — at that time: run the
Editor (or a small throwaway `main.cpp` debug hook) against a real rigged
`*.gta` model, call `ValidateGpuSkinningAgainstCpuOracle()` with its
resolved bind pose/skin weights/a real `EvaluateAnimatedSkinningPose()`
result, print `ToDiagnosticString()`, and record the resulting numbers
here (or in a follow-up addendum to this report).

### Why no new automated test file was added this phase

Every genuinely new piece of logic this phase adds is Tier 2 by
construction — it needs a live `VkDevice`/already-initialized `Renderer`
to do anything at all (uploading real buffers, dispatching a real compute
kernel, reading back real GPU memory). This mirrors `ComputeBlurValidation`
itself, which likewise has no automated test — see `TESTING.md`'s own
"Tier 2" bucket description. The one piece of genuinely pure, hand-checkable
logic this file introduces — `ToDiagnosticString()`'s formatting — is a
thin `std::snprintf()` wrapper with no branching logic worth a dedicated
unit test beyond what a human reading its own straightforward implementation
can already verify by inspection.

## What was deliberately NOT done (per Phase 6 v2's own "What We Will NOT Do")

- No fuzz/property-based testing across randomly generated skeletons.
- No performance benchmarking in this phase (that's Phase 7's job).
- No visual/screenshot-diff tooling.
- No attempt at bit-for-bit-identical floating point results — this tool's
  whole purpose is measuring the REAL delta (expected to be small,
  floating-point-accumulation-order noise), not asserting zero.
- No headless/windowless Vulkan device bootstrap inside `tests/` of any
  kind, under any framing — explicitly out of scope, per the strategy
  document's own Step 3.4/"What We Will NOT Do".

## Notes for future phases

- **Phase 7 (Editor Toggle & Profiling UX)** is the natural place to wire
  `ValidateGpuSkinningAgainstCpuOracle()`/`ValidateGpuSkinningGroupingParity()`
  into an actual, reachable Editor debug command (e.g. a menu item or a
  button next to the eventual CPU/GPU skinning-mode toggle in the "Jobs"
  panel — see `GPU_SKINNING_PHASE7_EDITOR_PROFILING_UX_STRATEGY_v1.md`'s
  own Step 3.1), so a developer never has to hand-edit `Application.cpp`
  to invoke it.
- **The concrete, required follow-up action**: run this tool against a
  real rigged model (the Furina fixture used throughout this project's own
  prior animation-runtime verification, or any other real, non-trivial
  `.pmx` import) once a machine with both a live Vulkan device AND that
  model fixture is available, and record the resulting
  `ToDiagnosticString()` output (max/mean position and normal deltas) as
  the actual, final evidence this campaign's Must-Have #7/#8 (a genuine
  numeric correctness proof) requires — this is the single most important
  remaining action before Phase 7 (and the campaign's own final sign-off)
  should proceed.
- **`GpuSkinningValidationResult`/`GpuSkinningGroupingParityResult`'s
  `epsilon`/count-based reporting shape** is intentionally generic enough
  that a future automated Tier-2 test (once the separate "Tier 2 GPU test
  fixture" TODO item lands) could wrap
  `ValidateGpuSkinningAgainstCpuOracle()` directly in a
  `GTEST_SKIP()`-guarded test with almost no adaptation — the actual
  comparison logic is already fully written and reusable; only the "how do
  I get a `VkDevice`" plumbing would need to change, exactly as Phase 6
  v2's own Step 3.4 anticipated.
