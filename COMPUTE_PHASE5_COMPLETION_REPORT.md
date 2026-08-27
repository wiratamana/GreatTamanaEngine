# COMPUTE_PHASE5_COMPLETION_REPORT.md

Session report for **Phase 5 — Barriers: GPU traffic lights** (the
"Synchronization" phase) of the compute-shader campaign described in
`COMPUTE_SHADER_MASTER_STRATEGY_v2.md`. Scope was taken directly from
`COMPUTE_PHASE5_SYNCHRONIZATION_STRATEGY_v2.md` — including its own "Step 6:
V2 Revision Notes" (the buffer-only-write-can-be-silently-culled regression
test). Nothing beyond that document's own "Step 3: The Plan" was
implemented, per its own "Step 4: What We Will NOT Do".

Work was done on the pre-existing branch `feature/compute-shader-impl`,
picking up immediately after Phase 4 (`COMPUTE_PHASE4_COMPLETION_REPORT.md`).

## What shipped

Every change is purely additive to already-shipped `RenderGraphTypes`/
`RenderGraphBarrierPlanner`/`RenderGraph` code — no existing call site's
behavior changed (every new enumerator is additive, and the one small
refactor described below is confirmed behavior-preserving by the full test
suite), and no descriptor/dispatch/render-graph-builder-API work was
touched at all (that's Phase 6's job).

### `ResourceAccess` — three new enumerators

- **`src/Renderer/RenderGraph/RenderGraphTypes.h`** — `ResourceAccess`
  gained `ComputeShaderRead`, `ComputeShaderWrite`, and `IndirectCommandRead`,
  alongside the five pre-existing graphics-only values. Per the strategy
  document's own Step 2 ("whichever document's implementation lands FIRST
  is where the enum values are actually added — the other should treat
  them as already-satisfied prerequisites"), all three were added here
  since the companion `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`
  document (which also names these same three values, scoped purely
  against buffers) has not been implemented in this repository yet.
- **`RenderGraphTypes.cpp`** — `IsWriteAccess()`/`ToString()` extended to
  cover all three new values, preserving the "no `default:` case" rule
  (a future enumerator added without updating these two switches fails to
  compile, exactly as documented). `ComputeShaderRead`/`IndirectCommandRead`
  are not writes; `ComputeShaderWrite` is.

### `RequiredStateFor()` — texture-side (and buffer-side) handling for all three

- **`src/Renderer/RenderGraph/RenderGraphBarrierPlanner.cpp`** —
  `RequiredStateFor()` extended with three new `case` branches:
  - `ComputeShaderRead` → `{ VK_IMAGE_LAYOUT_GENERAL,
    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
    VK_ACCESS_2_SHADER_STORAGE_READ_BIT }`
  - `ComputeShaderWrite` → the same layout/stage, `VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT`
  - `IndirectCommandRead` → `{ VK_IMAGE_LAYOUT_UNDEFINED (unused for a
    buffer-only access), VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
    VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT }`

  Confirmed — per the strategy document's own prediction — that
  `ComputeShaderRead`/`ComputeShaderWrite` produce the exact SAME
  `ResourceState` value regardless of whether the caller applies them to a
  texture (a storage image) or a buffer (a `StructuredBuffer`/
  `RWStructuredBuffer`); a buffer barrier simply never reads the `layout`
  field, mirroring `TransferSrc`/`TransferDst`'s own existing dual
  applicability.

### `TargetsDepthState()` — a new, small, extracted, Tier-1-testable helper

- **`RenderGraphBarrierPlanner.h`/`.cpp`** — added `bool
  TargetsDepthState(ResourceAccess access) noexcept`, returning `true` only
  for `DepthStencilAttachmentReadWrite`. This is the exact decision
  `RenderGraph.cpp`'s `ApplyUsageBarrierIfNeeded()` used to make inline
  (`usage.access == ResourceAccess::DepthStencilAttachmentReadWrite`) —
  extracted into its own free function specifically so the strategy
  document's own instruction ("write a targeted unit test proving it rather
  than trusting it by inspection alone") could actually be honored: the
  inline version lived inside `RenderGraph.cpp`, which needs a live
  `VkDevice`/`Renderer` to exercise at all (Tier 2), so there was no way to
  unit-test that exact line without this extraction. `RenderGraph.cpp` was
  updated to call `TargetsDepthState(usage.access)` instead of repeating the
  comparison inline — a pure, behavior-preserving refactor, confirmed via
  the full test suite before/after.

### Tests

- **`tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp`** — one
  `IsWriteAccess()` case and one `ToString()` array/assertion entry per new
  enumerator (`ComputeShaderReadIsNotAWrite`, `ComputeShaderWriteIsAWrite`,
  `IndirectCommandReadIsNotAWrite`, plus the `ToString` coverage/distinct-
  names tests extended to include all three).
- **`tests/Renderer/RenderGraph/RenderGraphBarrierPlannerTests.cpp`** —
  - `RequiredStateForComputeShaderRead`/`RequiredStateForComputeShaderWrite`/
    `RequiredStateForIndirectCommandRead` — one case per new enumerator,
    mirroring the file's existing per-enumerator pattern.
  - `ComputeShaderReadAndWriteAreSymmetricApartFromAccessDirection` —
    confirms the "same `ResourceState` regardless of resource kind" claim
    directly (identical layout/stage, differing access mask only).
  - `TargetsDepthStateIsTrueOnlyForDepthStencilAttachmentReadWrite` — one
    assertion per `ResourceAccess` enumerator (all eight), directly proving
    the "everything routes to `colorState` except
    `DepthStencilAttachmentReadWrite`" rule the strategy document asked to
    have proven rather than merely trusted by inspection.
  - `ComputeShaderWriteFollowedByShaderReadEmitsExactlyOneCorrectBarrier` —
    the texture-side hand-simulated sequence the strategy document itself
    calls for: `ComputeShaderWrite` (an `RWTexture`) → `ShaderRead` (the
    same texture, sampled normally by a later graphics pass), confirming
    exactly one barrier with `VK_IMAGE_LAYOUT_GENERAL →
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` and the matching stage/access
    masks.
  - `RequiresBarrierIsFalseForTwoConsecutiveComputeShaderWrites` — the
    "already-known-safe redundant transition is skipped" optimization,
    confirmed for the new enumerator too.
- **`tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp`** —
  `BufferOnlyWriteSurvivesCullingOnlyWhenAReaderReachesATextureFinalOutput`,
  the buffer-side reachability regression test called for by the strategy
  document's own "Step 6: V2 Revision Notes": a three-pass hand-built
  graph — `PassA` writes buffer `B` (`ComputeShaderWrite`, no texture
  output of its own), `PassB` reads `B` (`ComputeShaderRead`) and writes a
  real `finalOutputs` texture root, `PassC` ALSO writes `B` but has no
  reader and no path to `finalOutputs` at all — confirms `PassA`/`PassB`
  both survive (in that execution order, `PassA`'s buffer-only write kept
  alive transitively through `PassB`'s own real texture output) and `PassC`
  is correctly culled. This is the concrete, executable proof of the "a
  buffer write alone never keeps a pass alive — it needs an in-graph
  reader whose own chain reaches a real texture final output" constraint
  documented (but left untested) by earlier phases.

## Build system changes

- No `CMakeLists.txt`/`tests/CMakeLists.txt` changes — every change this
  phase touches an already-listed source/test file; no new files were
  added.

## Verification performed

- Built `GreatTamanaEngineTests.exe` incrementally — compiled with zero
  warnings/errors introduced by the changed files.
- Ran the full `*RenderGraph*` test filter (116 tests) — all pass,
  including all 11 new tests this phase added.
- Ran the **entire** existing test suite: **644 of 645 tests passed**, **1
  skipped** (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`,
  the same pre-existing machine-gated smoke test every prior phase's own
  report has noted — unrelated to this change). **Zero regressions** — 11
  new tests added on top of Phase 4's own 633 passing + 1 skipped baseline.
- Built the full project (`GreatTamanaEngine.exe` **and**
  `GreatTamanaEngineTests.exe`) — both link successfully; shaders staged
  correctly next to the executable.
- Launched the real `GreatTamanaEngine.exe` (which constructs a real
  `Renderer`/`gte::rg::RenderGraph`/live `VkDevice` on startup, and
  exercises `RenderGraph::ApplyUsageBarrierIfNeeded()`'s newly-refactored
  `TargetsDepthState()` call on every real frame's Game/Scene/Present
  passes) and confirmed it stayed running (no crash/exception at startup,
  confirmed via `tasklist` a few seconds after launch) before stopping it —
  worth confirming directly since `RenderGraph.cpp` itself changed, even
  though the change is a pure refactor with no intended behavior
  difference (see "Verification performed" in every prior compute-shader
  phase's own completion report for this same discipline).
- No validation-layer run was possible on this development machine — as
  already noted in every prior compute-shader phase's own completion
  report, `VK_LAYER_KHRONOS_validation` is not installed here (a
  pre-existing environment limitation). This phase adds no new descriptor/
  dispatch *usage* of the new `ResourceAccess` values (no `.comp` shader or
  render-graph pass declares `ComputeShaderRead`/`ComputeShaderWrite`/
  `IndirectCommandRead` yet — that begins with Phase 6/7's real workloads),
  so there is nothing live to validate beyond the pure decision logic
  already covered by the Tier-1 tests above.

## Acceptance criteria check (against the strategy doc's own "Step 3: The Plan")

- ✅ `RequiredStateFor()` extended so `ComputeShaderRead`/`ComputeShaderWrite`
  return a valid `ResourceState` for the TEXTURE case (and confirmed
  identical for the buffer case) — `VK_IMAGE_LAYOUT_GENERAL`,
  `VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT`, and the matching
  read/write storage-access mask.
- ✅ `RenderGraph.cpp`'s `ApplyUsageBarrierIfNeeded()` confirmed (via a
  direct unit test against the newly-extracted `TargetsDepthState()`, not
  merely by inspection) to correctly route every new enumerator to a
  texture's `colorState`, never its `depthState`.
- ✅ Regression tests added to both `RenderGraphTypesTests.cpp` (one case
  per new enumerator) and `RenderGraphBarrierPlannerTests.cpp` (the
  texture-specific `ComputeShaderWrite` → `ShaderRead` hand-simulated
  sequence), exactly as specified.
- ✅ The intra-pass hazard caveat (a pass whose `execute` callback issues
  more than one dispatch with a real read-after-write dependency between
  them is responsible for its own manual barrier — the render graph only
  sees a pass's AGGREGATE reads/writes) and the buffer-reachability caveat
  (Step 6) are both documented directly in this report and were already
  present in `RenderGraphTypes.h`'s own doc comments from Phase 1 onward;
  this phase adds the concrete, executable regression test for the latter
  (see `RenderGraphCompilerTests.cpp` above) that no earlier phase had.
- ✅ Coordinated with the (unimplemented, in this repository) companion
  `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md` document by adding
  its own three named `ResourceAccess` values here, as the single shared
  source of truth, exactly as that document's own Phase B anticipates.

## What was deliberately NOT done (per the strategy doc's own "Step 4")

- No automatic detection or warning for a missed intra-pass barrier — this
  remains entirely the pass author's responsibility, undetected by any
  tooling in this engine.
- No cross-resource barrier batching — unaffected by this phase, still
  Phase 9 (Render Graph campaign) backlog.
- No new "read-write in one" `ResourceAccess` value (e.g. a combined
  `ComputeShaderReadWrite`) — a true read-modify-write `RWTexture`/
  `RWStructuredBuffer` declares BOTH a `ComputeShaderRead` usage and a
  `ComputeShaderWrite` usage on the same handle, exactly as
  `ReadBuffer()`/`WriteBuffer()` already work today for buffers.
- No `RenderGraphBuilder`/`PassBuilder` API changes (`WriteTexture()`,
  `AddComputePass()`, `ctx.resolveBuffer()`/`resolveTexture()`) — that is
  entirely Phase 6's job (`COMPUTE_PHASE6_RENDERGRAPH_INTEGRATION_STRATEGY_v2.md`).
  Nothing in this phase declares a real `ComputeShaderRead`/
  `ComputeShaderWrite`/`IndirectCommandRead` usage from an actual pass yet
  — this phase only makes the barrier-planning machinery correctly able to
  handle one, once Phase 6 gives pass authors a way to declare it.
- No `BufferHandle`-flavored `finalOutputs`/`ImportBuffer()` — the
  buffer-reachability constraint this phase's own new compiler regression
  test proves is a real, existing limitation is explicitly NOT lifted here
  (see the strategy document's own Step 6: "Explicitly NOT this phase's
  job to fix... that is a follow-up for whoever owns the Render Graph
  campaign's own backlog, not this compute campaign").

## Handoff notes for whoever picks up Phase 6

- Phase 6 (`COMPUTE_PHASE6_RENDERGRAPH_INTEGRATION_STRATEGY_v2.md`) is the
  next unit of work — adding `PassBuilder::WriteTexture(TextureHandle,
  ResourceAccess)` (a general, non-attachment write declaration, distinct
  from `WriteColorAttachment()`/`WriteDepthStencilAttachment()`), and the
  new `PassContext::resolveBuffer()`/`resolveTexture()` primitives a
  compute pass's `execute` callback will use to rewrite its own
  `ComputeDescriptorSet` (Phase 3) before calling `Renderer::Dispatch()`
  (Phase 4).
- `RequiredStateFor()`/`TargetsDepthState()`/`IsWriteAccess()`/`ToString()`
  are all already fully ready for `WriteTexture(handle, ComputeShaderWrite)`/
  `ReadTexture(handle, ComputeShaderRead)`/`ReadBuffer(handle,
  ComputeShaderRead)`/`WriteBuffer(handle, ComputeShaderWrite)` usages —
  Phase 6 needs no further changes to this phase's own deliverables, only
  to actually START declaring them from a real pass.
- Confirm, when building Phase 6's own first real compute-pass smoke test
  (per that document's own "Step 5: Their Role" — a `WriteTexture()`-only
  pass, preferred specifically because it sidesteps the buffer-
  reachability culling gotcha this phase's own new compiler test makes
  concrete), that a `WriteTexture(handle, ComputeShaderWrite)` usage is
  correctly EXCLUDED from `RenderGraph::Execute()`'s `hasColorWrite`/
  `hasDepthWrite` scan (it only checks for `ColorAttachmentWrite`/
  `DepthStencilAttachmentReadWrite` specifically today) — this should
  already be true with zero further code changes, per this phase's own
  `TargetsDepthState()` regression test, but Phase 6's own strategy
  document explicitly asks for it to be verified again at that layer too.
- `IndirectCommandRead` has no real consumer anywhere in this repository
  yet (the companion GPU-driven-rendering document's own culling→indirect-
  draw workload is what will actually declare a `ReadBuffer(indirectBuffer,
  IndirectCommandRead)` usage) — this phase's own tests only exercise it
  in isolation (`RequiredStateForIndirectCommandRead`). Whoever implements
  that companion document's Phase B/D should treat this enumerator (and
  its `RequiredStateFor()` handling) as an already-satisfied prerequisite,
  per this document's own Step 2.
