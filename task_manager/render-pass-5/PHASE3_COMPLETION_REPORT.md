# PHASE3 — Completion Report: The Real GPU Frustum-Culling Compute Shader ⚠️ HIGH-RISK / DEDICATED DOUBLE-CHECK PHASE

## Status: DONE

Implemented exactly as scoped in the (already pre-implementation-corrected)
`PHASE3_FRUSTUM_CULL_COMPUTE_SHADER_AND_PIPELINE.md`, per every "⚠️
CORRECTED" marker in that document (see
`PHASE3_STRATEGY_DOUBLE_CHECK_REPORT.md` for the full pre-check writeup).
Read `readme.md`, `AGENTS.md`, `PHASE0_MASTER_STRATEGY.md`,
`PHASE1_COMPLETION_REPORT.md`, and `PHASE2_COMPLETION_REPORT.md` in full
before starting, per this campaign's own working agreement — neither prior
report recorded a decision that changed this phase's plan; PHASE2's own note
about `Application.cpp`'s real vs. dead-code `"RenderOpaque"` provider was
noted but not needed here (this phase's own verification harness never went
through the render-graph/`RenderSystem` path at all — see below).

## What was built

### New files (production code — kept, committed)

- **`src/Shaders/FrustumCull.comp`** — the real culling compute shader,
  written as a byte-for-byte-faithful implementation of the phase document's
  own pinned-down GLSL (section 3.1/3.2), including:
  - Binding 0 (`InstanceInputBuffer`) — a plain GLSL `struct CullingInstance`
    array-of-structs (safe under `std430`, per the phase doc's own reasoning
    — every member is already 16-byte-aligned/sized).
  - Binding 1 (`IndirectCommandOutputBuffer`) — a **flat, manually-indexed
    `uint values[]` array** (`values[i*5 + 0..4]`), NEVER a GLSL `struct`
    array — the phase document's own single most load-bearing correctness
    requirement (a struct array here would silently misalign to a 32-byte
    stride instead of the required tightly-packed 20 bytes).
  - Binding 2 (`VisibleCountBuffer`) — a single atomic `uint`, incremented
    via `atomicAdd` in the `useCompaction == 1` branch only. **This shader
    never resets this buffer itself anywhere in its source** — confirmed by
    direct re-read of the final committed file before writing this report;
    there is no `visibleCount.value = 0` assignment or equivalent anywhere.
  - The 104-byte push-constant block (`vec4 planes[6]` + `uint instanceCount`
    + `uint useCompaction`), matching the phase doc's own pinned-down
    byte-budget table exactly.
  - `AabbIntersectsFrustum()` — the exact p-vertex test from section 3.2,
    verbatim.
  - Both `useCompaction` branches (compacted/atomic-append, and
    degenerate-padding), both genuinely implemented (not stubbed), matching
    section 3.2's exact worked-example semantics — in particular,
    `firstInstance` is ALWAYS the instance's own original row index `i`
    regardless of branch or output slot, in both branches.
- **`src/Renderer/Culling/CullingPipelines.h`/`.cpp`** — mirrors
  `src/Renderer/GpuSkinning/GpuSkinningPipelines.h`/`.cpp` exactly (confirmed
  by direct side-by-side comparison while writing this): one shared
  `VkDescriptorSetLayout` (3 storage buffers, all `VK_SHADER_STAGE_COMPUTE_BIT`),
  one shared `ComputePipeline` (`shaders/FrustumCull.comp.spv`), lazy
  idempotent `EnsureInitialized(Renderer&)`, `IsInitialized()`, and const
  accessors (`DescriptorSetLayout()`/`Pipeline()`). Also exposes
  `kCullingLocalSizeX = 256` (matching the shader's own
  `layout(local_size_x = 256) in;`) and a new `kCullingPushConstantSize = 104`
  constant (not present in `GpuSkinningPipelines.h`, added here since this
  shader's push-constant size needed a single named source of truth for
  `VkPushConstantRange::size` and for this phase's own verification harness's
  `static_assert`).

### Modified files (production code)

- **`CMakeLists.txt`** — registered `src/Shaders/FrustumCull.comp` via
  `gte_add_shader()` (unconditional — a real gameplay/runtime feature, not a
  debug/Editor-only tool, mirroring `SkinVerticesPositionNormal.comp`'s own
  placement precedent) and added
  `src/Renderer/Culling/CullingPipelines.h`/`.cpp` to `gte_core`'s
  `target_sources()` list, immediately after `CullingTypes.h`/`.cpp`.

### Temporary files (built, run, manually verified, then FULLY DELETED before this commit — see "Manual verification" below)

- `src/Application/FrustumCullVerificationHarness.h`/`.cpp` — a throwaway
  verification harness, mirroring PHASE2's own "build a hand-driven smoke
  test, verify it manually, then delete it entirely" discipline (see
  `PHASE2_COMPLETION_REPORT.md`'s own "Manual smoke test" section). Called
  once, synchronously, from `Application`'s constructor (immediately after
  the existing `GTE_LOG_INFO("Application", "GreatTamanaEngine started.")`
  call, since `m_renderer` is already fully constructed by that point) —
  this call site and its supporting `#include` were both fully removed
  afterward, confirmed via `git diff` showing **zero** net change to
  `Application.cpp`/`CMakeLists.txt` beyond the two intentional, permanent
  additions listed above.
  - **Deviation from the phase doc's own suggested harness placement**: the
    phase document doesn't specify a call site; this harness needed no
    render-graph/`RenderSystem` involvement at all (unlike PHASE2's harness,
    which needed to visually render something into the Game View). It only
    needed a live `Renderer&` to call `Renderer::ImmediateSubmit()`/
    `Dispatch()`/`AllocateComputeDescriptorSet()`/`CreateStructuredBuffer()`
    against, all of which are usable the instant `Renderer`'s own
    constructor returns — so `Application`'s own constructor (right after
    `m_renderer(m_window)` finishes and logging is already proven reachable)
    was the simplest, lowest-risk call site, entirely independent of
    `Application.cpp`'s dead-code-vs-real-`"RenderOpaque"`-provider
    distinction PHASE2 flagged (that distinction only matters for a harness
    that needs to actually **render** something end-to-end into a real pass;
    this harness only dispatches a compute shader and reads back plain
    buffers via `ImmediateSubmit`, with zero render-graph pass involved).

## Manual verification (section 3.4) — full results

### Camera / frustum used

Identical convention to PHASE1's own hand-verified test
(`ExtractFrustumPlanesFromOrthographicProjectionMatchesHandComputedValues`):
identity view (camera at the origin, looking down +Z), `viewProjection =
OrthographicLH_ZO(-1, 1, -2, 2, 0.5, 10)`. Resulting 6 planes (re-derived by
hand before writing the harness, matching `PHASE1_COMPLETION_REPORT.md`
exactly):

| Plane | Normal | Distance | Inside condition |
|---|---|---|---|
| Left | (1,0,0) | 1 | x ≥ -1 |
| Right | (-1,0,0) | 1 | x ≤ 1 |
| Bottom | (0,1,0) | 2 | y ≥ -2 |
| Top | (0,-1,0) | 2 | y ≤ 2 |
| Near | (0,0,1) | -0.5 | z ≥ 0.5 |
| Far | (0,0,-1) | 10 | z ≤ 10 |

### Scene 1 — the exact 5-instance / 2-culled worked example from the phase document's own section 3.2

Every instance shares a local AABB of `[-0.1,-0.1,-0.1]..[0.1,0.1,0.1]` and a
fake shared mesh (`firstIndex=100, indexCount=6, vertexOffset=0`), placed by
a pure translation (`worldMatrix = Translation(position)`), so its
world-space AABB is simply `position ± 0.1` on every axis.

| Instance `i` | World position | World AABB | Hand-derived verdict (p-vertex test against each plane) | CPU-oracle (`AABBIntersectsFrustum()`) result | GPU (`FrustumCull.comp`) result |
|---|---|---|---|---|---|
| 0 | (0, 0, 5) | [-0.1,0.1]×[-0.1,0.1]×[4.9,5.1] | All 6 planes pass (e.g. Far: pVertex.z=min=4.9, dot=-4.9+10=5.1≥0) | **visible** | **visible** |
| 1 | (5, 0, 5) | [4.9,5.1]×[-0.1,0.1]×[4.9,5.1] | Right plane fails: pVertex.x=min=4.9 (normal=(-1,0,0)), dot=-4.9+1=**-3.9<0** | **culled** | **culled** |
| 2 | (-0.5, 0, 3) | [-0.6,-0.4]×[-0.1,0.1]×[2.9,3.1] | All 6 planes pass (e.g. Left: pVertex.x=max=-0.4, dot=-0.4+1=0.6≥0) | **visible** | **visible** |
| 3 | (0, 0, 15) | [-0.1,0.1]×[-0.1,0.1]×[14.9,15.1] | Far plane fails: pVertex.z=min=14.9 (normal=(0,0,-1)), dot=-14.9+10=**-4.9<0** | **culled** | **culled** |
| 4 | (0.5, 1, 8) | [0.4,0.6]×[0.9,1.1]×[7.9,8.1] | All 6 planes pass (e.g. Top: pVertex.y=min=0.9, dot=-0.9+2=1.1≥0) | **visible** | **visible** |

Confirmed by the harness's own logged CPU-oracle line (`GET /get_logs`,
`category=FrustumCullHarness`, entry id 4): `"Scene1 CPU-oracle
(AABBIntersectsFrustum) visibility: 0=visible 1=culled 2=visible 3=culled
4=visible"` — matching the by-hand table above exactly, confirmed BEFORE the
GPU dispatch even ran.

#### RUN A — `useCompaction = 1` (compacted/atomic-append mode)

Dispatched against the real `CullingPipelines`/`Shaders/FrustumCull.comp.spv`
on this development machine's real GPU/driver, count buffer explicitly
reset to `0` via `vkCmdFillBuffer` immediately before the dispatch (a
`ResourceAccess::TransferDst` → `ResourceAccess::ComputeShaderWrite` barrier
in between, mirroring the exact vocabulary PHASE5 will use in the real
render-graph pass). Read back via a `BufferMemoryUsage::GpuToCpu` staging
buffer + `vkCmdCopyBuffer` + an explicit `HOST_READ` visibility barrier
(mirrors `Renderer::CaptureImagePixels()`'s own established image-readback
pattern, applied to a plain buffer instead of an image).

**Actual GPU output** (log entry id 5): `visibleCount = 3` (expected 3 —
instances 0, 2, 4), with the 3 written compacted slots being:

```
slot 0: {indexCount=6, instanceCount=1, firstIndex=100, vertexOffset=0, firstInstance=0}
slot 1: {indexCount=6, instanceCount=1, firstIndex=100, vertexOffset=0, firstInstance=2}
slot 2: {indexCount=6, instanceCount=1, firstIndex=100, vertexOffset=0, firstInstance=4}
```

Every field matches the shared mesh values exactly; the set of
`firstInstance` values is exactly `{0, 2, 4}` (each appearing exactly once,
in whatever order the GPU's own `atomicAdd` calls happened to complete in —
per the phase document's own explicit note, compacted-array ORDER is not a
correctness requirement, only that every visible instance's own original
row index appears exactly once). **RUN A: PASS** (log entry id 6).

#### RUN B — `useCompaction = 0` (degenerate-padding/fallback mode)

Same scene, a fresh indirect-command buffer and count buffer (count buffer
unused by this branch), dispatched with no reset (not needed — this branch
never touches the count buffer at all).

**Actual GPU output** (log entry id 7), full 5-slot array:

```
slot 0: {indexCount=6, instanceCount=1, firstIndex=100, vertexOffset=0, firstInstance=0}  <- real (visible)
slot 1: {indexCount=0, instanceCount=0, firstIndex=0,   vertexOffset=0, firstInstance=1}  <- degenerate (culled)
slot 2: {indexCount=6, instanceCount=1, firstIndex=100, vertexOffset=0, firstInstance=2}  <- real (visible)
slot 3: {indexCount=0, instanceCount=0, firstIndex=0,   vertexOffset=0, firstInstance=3}  <- degenerate (culled)
slot 4: {indexCount=6, instanceCount=1, firstIndex=100, vertexOffset=0, firstInstance=4}  <- real (visible)
```

Matches the phase document's own worked-example table in section 3.2
EXACTLY, field-for-field, slot-for-slot. **RUN B: PASS** (log entry id 8).

### Sub-check 1 — the count-buffer reset invariant (RUN C)

Per section 3.2's "⚠️ CORRECTED" reset requirement and section 3.4's own
mandatory sub-check: using the same Scene 1 / `useCompaction=1` setup,

1. Reset the count buffer once, then dispatch the shader **twice in a row
   with no reset in between** (two separate `ImmediateSubmit` calls, each
   one fully completing on the GPU before the next begins — confirmed via
   `vkWaitForFences` inside `GpuResourceFactory::ImmediateSubmit()` — so this
   is a genuine two-dispatch sequence, not a same-submission race). **Actual
   result** (log entry id 9): `count = 6` — exactly double the oracle's `3`,
   confirming the count buffer accumulated across both dispatches instead of
   restarting from `0` the second time. **This empirically PROVES the reset
   requirement is real and load-bearing**, exactly as the phase document
   predicted, not merely a theoretical worry.
2. Reset the count buffer before **each** of two further dispatches (log
   entry id 10): `first = 3, second = 3` — both correct, confirming a
   properly-reset dispatch always agrees with the CPU oracle, repeatably.

**RUN C: PASS** (log entry id 11) — both sub-results exactly as the phase
document's own corrected section 3.2/3.4 predicted.

### Sub-check 2 — the degenerate-padding drawCount/instanceCount ghost-geometry invariant (RUN D)

Per section 3.2's second "⚠️ CORRECTED" note and section 3.4's second
mandatory sub-check: a fresh Scene 2 (5 instances, ALL visible — positions
`x ∈ {-0.6, -0.3, 0, 0.3, 0.6}`, `y=0, z=5`, comfortably inside every plane;
confirmed by the harness's own CPU-oracle check, log entry id 12: `"Scene2
CPU-oracle: all 5 instances visible = true"`), a different fake shared mesh
(`firstIndex=200, indexCount=12, vertexOffset=5`), `useCompaction=0`.

1. Dispatch 1: `pc.instanceCount = N = 5` — every one of slots `0..4` gets a
   REAL (non-degenerate) command, since every instance is visible. Confirmed
   by readback (log entry id 13): `"RUN D dispatch 1 (instanceCount=N=5):
   every slot real = true"`.
2. Dispatch 2: **reusing the exact same output buffer**, `pc.instanceCount =
   M = 3 < N`. This dispatch only ever touches slots `0..2`.
3. Readback confirms slots `0..2` are correctly re-written (still real,
   matching the shared mesh values, `firstInstance` = their own slot index),
   AND — the load-bearing check — **slots `3` and `4` still contain
   dispatch 1's ORIGINAL real (non-degenerate) commands**, untouched by
   dispatch 2 (log entries id 14/15):

```
slot 3: {indexCount=12, instanceCount=1, firstIndex=200, vertexOffset=5, firstInstance=3}
slot 4: {indexCount=12, instanceCount=1, firstIndex=200, vertexOffset=5, firstInstance=4}
```

This **empirically reproduces the exact ghost-geometry hazard** the phase
document's own "⚠️ CORRECTED" note describes: if a caller (PHASE4/5) issued
`vkCmdDrawIndexedIndirect` with `drawCount` set to the buffer's own
CAPACITY (`5`, from a previous, larger frame) instead of THIS dispatch's own
`pc.instanceCount` (`3`), it would silently redraw 2 stale, real commands
for instances that may no longer even exist in the batch this frame. This
harness only reproduces the buffer-level shape of the hazard (no actual
`SubmitIndirect()`/mesh/pipeline was involved, matching the phase document's
own explicit "3.4 only proves the shader/buffer-level invariant — the real
render-graph wiring is PHASE5's job" scope) — but confirms, with real GPU
data rather than a guess, that the invariant PHASE4/5 must uphold
(`drawCount == this frame's own instanceCount`, never the cache's grown
capacity) is a genuine, reachable requirement.

**RUN D: PASS** (log entry id 16).

### Overall harness verdict

`"=== PHASE3 harness: ALL CHECKS PASSED ==="` (log entry id 17) — all four
runs (A/B/C/D) passed on the first attempt, no code changes were needed to
`Shaders/FrustumCull.comp`/`CullingPipelines.h`/`.cpp` after the initial
compile-clean version.

### Validation layers / warnings

`GET /get_logs?min_level=Warning` returned `count: 0` for the entire session
(zero `Warning`/`Error` entries at any point, including every one of the 4
runs above and the engine's own normal startup). Validation layers are
**not available on this development machine** (a pre-existing environment
fact, already documented identically in `PHASE2_COMPLETION_REPORT.md` — this
phase did not re-confirm the exact startup message string since PHASE2
already established it and nothing about this machine's environment changed
between phases) — "verified with validation layers enabled" in the strictest
sense was therefore not literally possible in this session, exactly as
PHASE2 already recorded; verification instead relied on (a) zero
`Warning`/`Error` Logger entries across the whole session, (b) the harness's
own self-checking PASS verdicts for all four runs (cross-checked field-by-
field against the CPU oracle, not merely "it didn't crash"), and (c) the
complete absence of any crash/assert across the full harness run. A future
session on a machine with the validation layer installed should re-run this
same shape of verification at least once, per PHASE2's own identical
recommendation.

## A small, harmless naming inaccuracy found in the phase document (worth recording, not a design change)

`PHASE3_FRUSTUM_CULL_COMPUTE_SHADER_AND_PIPELINE.md` (and
`PHASE3_STRATEGY_DOUBLE_CHECK_REPORT.md`) refer to the real Vulkan usage
flag needed for the indirect-command/count buffers as
`VK_BUFFER_USAGE_INDIRECT_COMMAND_BIT`. The actual Vulkan/volk header
enumerator is **`VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT`** — confirmed directly
by a compile error when the harness was first written with the doc's
literal name (`gcc` reported "did you mean
'VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT'?"), and fixed in the harness before
it was run. This is a purely COSMETIC documentation inaccuracy in the phase
doc's own prose — the underlying bit/semantics described (the usage flag
`vkCmdDrawIndexedIndirect(Count)`'s buffer argument must be created with) are
completely correct and unaffected; no production code anywhere in this
phase actually used the wrong name (`CullingPipelines.h`/`.cpp` and
`Shaders/FrustumCull.comp` never reference any `VK_BUFFER_USAGE_*` flag at
all — that only ever appears at a future buffer-creation call site, which is
PHASE4/5's job, not this phase's). Flagging this here so PHASE4/5's own
authors use the CORRECT flag name (`VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT`)
when they actually create the real indirect-command/count buffers, rather
than copying the phase document's own typo verbatim and hitting the same
compile error PHASE4/5 would otherwise discover themselves.

## Deviations from the phase document

None in the shipped production code (`Shaders/FrustumCull.comp`,
`CullingPipelines.h`/`.cpp`) — every binding, struct layout, push-constant
field, and both `useCompaction` branches match the corrected phase document
exactly, field-for-field. The one deviation is the harness's own call-site
placement (see "Temporary files" above) — a documented, reasoned choice
(this harness needed no render-graph/rendering involvement at all, unlike
PHASE2's own harness), not a shortcut around anything the phase document
required.

## What We Did NOT Do (matches the phase doc's own scope)

- No render-graph pass declaration (`AddComputePass()`/`WriteBuffer()`) —
  PHASE5's job. This phase proves the SHADER ITSELF works, via a throwaway
  harness, exactly like PHASE2 proved `SubmitIndirect()` itself before any
  render-graph involvement.
- No `RenderSystem`/ECS involvement — the harness's input data was
  hand-authored (`PackCullingInstanceInput()`/`TransformAABB()` called
  directly with hand-picked positions), not sourced from a live scene
  (PHASE4's job).
- No actual implementation of the count-buffer reset or the
  drawCount/instanceCount coupling as PRODUCTION render-graph wiring — this
  phase only PROVED (via RUN C/RUN D above) that both are real requirements;
  the real `vkCmdFillBuffer` reset pass and the real `SubmitIndirect()` call
  site threading through the frame's own `instanceCount` both remain
  PHASE5's own deliverable, exactly as scoped.
- No occlusion culling, no hierarchical/two-phase culling, no LOD selection
  in this shader (PHASE0 Non-Goals).

## Compile check (per Locked Design Decision 9 — targeted only, no full build)

- `cmake -S . -B build` (reconfigure, since new source files were added to
  the explicit `target_sources()` lists) + `cmake --build build --target
  GreatTamanaEngine --config Debug` — succeeds cleanly, zero warnings/errors,
  both with the temporary harness present (used for the manual GPU
  verification above) and after it was fully removed (final, committed
  state — confirmed via a second clean rebuild plus `git diff` showing zero
  net change to `Application.cpp`/`CMakeLists.txt` beyond the two permanent,
  intentional additions).
- No full `cmake --build build` + full `ctest` pass was run, per
  `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision 9 (reserved for
  PHASE7 only). This phase added no new Tier-1 test file (unlike PHASE1/2) —
  `Shaders/FrustumCull.comp` is GLSL, not Tier-1-testable C++, and
  `CullingPipelines.h`/`.cpp` is a thin, Vulkan-device-owning wrapper
  mirroring `GpuSkinningPipelines` (itself untested at the C++ level for the
  identical reason — see that class's own precedent) — its correctness is
  established entirely by this phase's own GPU-vs-CPU-oracle harness above,
  matching the phase document's own chosen verification strategy (section
  3.4) rather than a unit test.

## Next phase

PHASE4 (`PHASE4_PER_BATCH_RESOURCE_MANAGEMENT_AND_BATCHING.md`) can now
depend on: `Shaders/FrustumCull.comp` real and GPU-verified for both
`useCompaction` branches; `CullingPipelines` real, initializable, and
exposing `DescriptorSetLayout()`/`Pipeline()`/`kCullingLocalSizeX`/
`kCullingPushConstantSize`; the count-buffer-reset precondition and the
drawCount == instanceCount invariant both EMPIRICALLY confirmed (not just
asserted) as real, reachable requirements PHASE4's own per-batch buffer
cache and PHASE5's own render-graph wiring must uphold; and the correct
Vulkan usage-flag name (`VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT`, not the phase
document's own typo) for whichever buffer-creation call site PHASE4/5
actually add.
