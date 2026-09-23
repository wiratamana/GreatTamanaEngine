# `render-pass-5` — Campaign Completion Report: GPU-Driven Frustum Culling + Indirect Draw

Parent: `PHASE0_MASTER_STRATEGY.md`. This is the final phase
(`PHASE7_DOCS_FULL_BUILD_AND_CAMPAIGN_COMPLETION.md`) of the seven-phase
`render-pass-5` campaign — the whole campaign's final integration checkpoint:
documentation, the campaign's one and only full build + full `ctest`
regression pass (per PHASE0's Locked Design Decision 9), and a final live,
running-engine, HTTP-driven proof tying every earlier phase's claims together.

## Status: DONE

`PHASE1_COMPLETION_REPORT.md` through `PHASE6_COMPLETION_REPORT.md`, plus
`PHASE3_STRATEGY_DOUBLE_CHECK_REPORT.md` and `PHASE5_STRATEGY_DOUBLE_CHECK_REPORT.md`,
were all read in full before starting, per this phase's own prerequisite (and
the top-level task instruction). Every phase's own recorded decision/deviation
was carried forward correctly into this final phase — see "Per-phase outcome
summary" below. Confirmed via `git_status` before starting: the repository's
current branch is `feature/render-pass-impl`, with a clean working tree
(PHASE1-6's own commits already present) — stayed on it, per the task's own
explicit instruction.

## Per-phase outcome summary

- **PHASE1 (Foundations: Bounds, Indirect Types, and Render-Graph Vocabulary)** —
  `src/Renderer/Culling/CullingTypes.h/.cpp` (`AABB`, `ComputeLocalAABB()`,
  `Plane`/`ExtractFrustumPlanes()`, `AABBIntersectsFrustum()`,
  `GpuCullingInstanceInput` (112 bytes) + `PackCullingInstanceInput()`,
  `TransformAABB()`), `src/Renderer/IndirectDrawTypes.h`
  (`IndirectDrawCommand`, 20 bytes, `static_assert`-verified byte-for-byte
  against `VkDrawIndexedIndirectCommand`), and a new
  `ResourceAccess::VertexShaderStorageRead` enumerator (full
  `IsWriteAccess()`/`ToString()`/`RequiredStateFor()` wiring plus the two
  missing hand-simulated buffer-side barrier regression tests). Confirmed
  `Math/Mat4.h` is column-major and GPU-upload-ready as-is (no transpose
  needed anywhere in this campaign). 52/52 targeted tests passed. Zero
  behavior change to anything existing.
- **PHASE2 (The Instanced Draw Primitive + Indirect Submit)** —
  `Shaders/MeshInstanced.vert` (reuses `Shaders/Mesh.frag` unmodified) +
  `VertexLayout::PositionNormalInstanced`; `Renderer::SubmitIndirect()` +
  `Renderer::SupportsDrawIndirectCount()` (both the real
  `vkCmdDrawIndexedIndirectCount` and the degenerate-padded
  `vkCmdDrawIndexedIndirect` fallback genuinely implemented and manually
  GPU-verified — this development machine's own device reported
  `drawIndirectCount = TRUE`, so both branches were actually exercised, not
  merely compiled); `GpuResourceFactory::InstanceBufferDescriptorSetLayout()`;
  `DrawStats::indirectDrawCount` + `AccumulateIndirectDrawStats()`. A
  hand-driven smoke test (3 quads, both `SubmitIndirect()` branches) was
  built, run, screenshotted, and fully deleted. Found and recorded a
  pre-existing structural fact: `RenderPasses.cpp`'s free-function
  `AddRenderOpaquePass()` is dead code — the real, live `"RenderOpaque"` is
  built inline inside `Application.cpp`'s own provider (load-bearing for
  PHASE5). 62/62 targeted tests passed.
- **PHASE3 (The Real GPU Frustum-Culling Compute Shader) ⚠️ HIGH-RISK,
  DEDICATED DOUBLE-CHECK PHASE** — `Shaders/FrustumCull.comp` (one thread per
  instance, a flat manually-indexed `uint values[]` output buffer — NEVER a
  GLSL struct array, since `std430` would otherwise silently misalign a
  20-byte `IndirectDrawCommand` to a 32-byte stride — both a
  compacted/atomic-append mode and a degenerate-padding fallback mode,
  push-constant-selected) + `src/Renderer/Culling/CullingPipelines.h/.cpp`
  (mirrors `GpuSkinningPipelines` exactly). A real GPU-vs-CPU-oracle
  verification harness (built, run, then fully deleted) proved: the exact
  5-instance/2-culled worked example from the phase doc; the count-buffer
  MUST be reset via `vkCmdFillBuffer` before every dispatch (empirically
  reproduced the bug of NOT doing so — count silently doubles); and the
  ghost-geometry hazard if the CPU-side draw count ever uses the cache's
  buffer capacity instead of the current frame's real instance count. Also
  flagged a harmless documentation-only typo (`VK_BUFFER_USAGE_INDIRECT_COMMAND_BIT`
  should read `VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT`). This phase's own
  **pre-implementation** double-check (`PHASE3_STRATEGY_DOUBLE_CHECK_REPORT.md`)
  found and fixed three real, load-bearing gaps in the strategy document
  itself before any code was written (see that report's own "What was found"
  for the full itemized list). No new automated test file — verified via the
  GPU harness, matching the phase document's own chosen verification
  strategy.
- **PHASE4 (RenderSystem Batching + Per-Batch GPU Resource Management)** —
  `src/Game/DrawCommand.h` (extracted), `src/Game/RenderBatching.h/.cpp`
  (`GroupDrawCommandsByMeshAndPipeline()`, `kMinInstancesForGpuDrivenBatch = 4`,
  `IsGpuDrivenEligible()`), `src/Renderer/Culling/GpuDrivenBatchCache.h/.cpp`
  (mirrors `GpuSkinningRigCache`'s per-model resource-cache precedent; kept
  deliberately Renderer-layer-clean, never touching `RenderBatchGroup`/
  `Registry`/`Entity` directly — a reasoned deviation from the phase doc's
  own literal pseudocode). Two genuine `ask_questions` detours: (1) real mesh
  bounds computation was wired into `MeshAssetGpuCatalog.cpp`'s existing
  submesh creation call sites (project owner confirmed, since no phase
  document actually assigned this real-content wiring job to anyone); (2) the
  GPU-skinning exclusion mechanism is a caller-supplied
  `std::unordered_set<VkBuffer>` (`RenderSystem::CollectGpuDrivenBatches()`),
  cross-referenced against `Mesh::VertexBuffer()` inside `RenderSystem`,
  built from `Game::CollectGpuSkinningDispatchRequests()` outside it (a Clean
  Architecture requirement). 14 new `RenderBatchingTest` cases, 26/26 +
  38/38 targeted tests passed (zero regression). `RenderSystem::Draw()`
  itself remained completely unmodified.
- **PHASE5 (Render-Graph Pass Wiring + Production Cutover) ⚠️ HIGH-RISK,
  DEDICATED DOUBLE-CHECK PHASE** — this phase's own **pre-implementation**
  double-check (`PHASE5_STRATEGY_DOUBLE_CHECK_REPORT.md`) found the original
  plan was written against a STALE model of how this engine declares
  production passes today (the free-function `AddRenderOpaquePass()`/
  `AddGpuSkinningPasses()` call style was already replaced by a generic
  `rg::RenderPipeline`/`rg::RenderPassProvider` layer in the already-shipped
  `render-pass-3` campaign) — had PHASE5 been implemented as originally
  written, the whole feature would have been wired into a dead/rarely-run
  path and drawn zero times in ordinary Editor use. That same pre-check also
  confirmed via `ask_questions`, for the first time anywhere in this
  campaign's own documents, that **this whole cutover is GAME VIEW ONLY**
  (now the campaign's Locked Design Decision 11) — Scene View and the rare
  direct-render-to-swapchain fallback keep drawing every entity through the
  fully unmodified per-entity path, forever. The corrected implementation:
  a new `"GpuDrivenBatches"` `rg::RenderPassProvider`
  (`Application::RegisterOffscreenRenderPipelineProviders()`), registered
  textually strictly between `"RenderOpaque"` and `"DrawSkyBackground"`'s own
  `Register()` calls (the real, load-bearing ordering guarantee —
  `DetectRenderPassEventContradictions()` cannot catch this specific
  write-after-write hazard, confirmed by direct re-read of
  `RenderGraphCompiler.cpp`). Per eligible batch: a real
  `"<batch> ResetCount"` pass (`vkCmdFillBuffer`), a real
  `"<batch> Culling"` compute pass, and a real `"<batch> IndirectDraw"`
  graphics pass, always passing THIS FRAME'S real instance count (never the
  cache's own monotonically-growing capacity) as the fallback draw count.
  Live-verified against a real running engine: correct pass order, Game View
  rendering the batch correctly, Scene View rendering all entities
  unbatched, and culling responding to camera rotation (a batch fully
  outside the frustum correctly shows only sky, `visibleCount == 0`, zero
  warnings). 155/155 targeted tests passed (zero regression;
  `RenderGraphCompiler.cpp`/`RenderGraphBarrierPlanner.cpp` needed zero
  changes, confirmed not merely assumed).
- **PHASE6 (Editor Tooling + Live Validation)** — the "instances culled this
  frame" readout (`GpuDrivenBatchCache::ReadLastKnownVisibleCount()`, a new
  `PassContext::recordIndirectDraw`, a new "GPU-Driven Batches" section at
  the top of the Editor's "Render Graph" panel) and a real validation-content
  spawn mechanism (`POST /spawn_gpu_driven_test_batch` + a "Create GPU-Driven
  Test Batch" Editor menu action, both `GTE_ENABLE_EDITOR`-only, confirmed
  via `ask_questions`: both an HTTP endpoint AND an Editor menu action,
  default instance count 6). Found and fixed a real bug during this phase:
  the first implementation declared the count-buffer readback as a 4th
  render-graph pass, which was silently, permanently culled every frame
  (`RenderGraphCompiler::Compile()`'s reachability rule: a pure `Buffer`
  write is never a culling root) — fixed by issuing the readback copy as a
  raw command directly against the offscreen command buffer instead, outside
  the declarative render-graph system entirely. Live-verified end-to-end:
  the readout correctly tracked `6/6` then `3/6` (3 culled) as entities moved
  out of frustum via `POST /set_entity_trs`, and recovered to `6/6` when
  moved back. 312/312 targeted tests passed. Also recorded, but explicitly
  did NOT act on (per its own out-of-scope note), a real process
  discrepancy: `PHASE5_COMPLETION_REPORT.md` claimed a dedicated
  post-implementation double-check report would exist
  (`PHASE5_DEDICATED_DOUBLE_CHECK_REPORT.md`) but no such file was ever
  actually produced — see "Known process deviation" below for how this
  phase (PHASE7) resolved that question.
- **PHASE7 (this phase)** — documentation (`AGENTS.md`, `README.md`,
  `docs/conventions/gpu-driven-rendering.md`,
  `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`,
  `RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md`,
  `COMPUTE_SHADER_FEATURES_DELIBERATELY_NOT_IMPLEMENTED.md`), the campaign's
  one full build + full `ctest` pass, a final live smoke test against the
  fully-built binary, the known process deviation call-out, and this report.

## Known process deviation: the missing PHASE5 dedicated post-implementation double-check

`PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision 10 required PHASE3 and
PHASE5 to each get their OWN dedicated **post-implementation**
`delegate_task` double-check pass (reviewing the actual committed code diff),
immediately after each phase landed, BEFORE the single whole-campaign
second-iteration review. **What actually happened**: only PRE-implementation
strategy-document double-checks were performed for both phases
(`PHASE3_STRATEGY_DOUBLE_CHECK_REPORT.md` / `PHASE5_STRATEGY_DOUBLE_CHECK_REPORT.md`
— both real, both genuinely valuable, both finding real issues in the
strategy documents before any code was written), never the separate
post-implementation code-diff review PHASE0 also called for.
`PHASE5_COMPLETION_REPORT.md`'s own final paragraph claims such a
post-implementation review was spawned ("a separate, dedicated
`delegate_task` review of ONLY this phase's committed diff was spawned
immediately after this report and its accompanying code were committed"),
but no `PHASE5_DEDICATED_DOUBLE_CHECK_REPORT.md` file (or any file of that
shape) exists anywhere in this repository — confirmed via `browse_dir`
before this report was written. PHASE6 independently confirmed and recorded
this same gap, explicitly declining to act on it (correctly, since it was
not one of PHASE6's own two dedicated-double-check phases) and deferring the
decision to this phase's own final review.

**Resolution (confirmed with the project owner via `ask_questions` during
this phase)**: no new double-check work was spawned to retroactively close
this gap. This is recorded here, honestly and permanently, as a real
process deviation from PHASE0's own Locked Design Decision 10 — not silently
dropped. Mitigating factors, for the record: (a) both phases DID receive a
real, substantive pre-implementation review that caught and fixed genuine,
load-bearing defects in their strategy documents before implementation
started (three gaps closed for PHASE3, five for PHASE5 — see each phase's
own `_STRATEGY_DOUBLE_CHECK_REPORT.md`); (b) both phases' actual shipped
code was independently, empirically verified via real GPU/live-engine
harnesses at implementation time (PHASE3's four-run culling-shader harness;
PHASE5's live HTTP-driven Game View/Scene View/camera-rotation smoke test);
(c) this PHASE7 campaign-closing pass performed its own full build, full
`ctest` regression, and independent live smoke test against the final,
fully-integrated binary (see below), which re-confirms PHASE3/PHASE5's own
shipped behavior end-to-end one more time, from scratch, on top of
everything above.

## What PHASE7 itself did

### 3.1 — Documentation updates

- **`AGENTS.md`** gained a new "GPU-Driven Rendering (Frustum Culling +
  Indirect Draw)" section (placed directly after "GPU Vertex Skinning",
  before "Atmosphere Scattering" — the closest precedent this campaign
  itself named as its own literal template), summarizing the batching
  eligibility rule, the new `ResourceAccess::VertexShaderStorageRead`
  enumerator, the new `VertexLayout::PositionNormalInstanced`, the
  Game-View-only cutover, and a pointer to
  `task_manager/render-pass-5/PHASE0_MASTER_STRATEGY.md`/
  `CAMPAIGN_COMPLETION_REPORT.md`.
- **`docs/conventions/gpu-driven-rendering.md`** (new file) — mirrors
  `docs/conventions/gpu-vertex-skinning.md`'s own shape (a "follow these
  rules whenever touching this feature" bullet list capturing every
  load-bearing gotcha this campaign's own reports recorded: the fixed
  four-condition eligibility rule, the Game-View-only scope, the mandatory
  count-buffer reset, the drawCount-must-equal-current-instanceCount
  invariant, the flat-array output-buffer requirement, why the readback is
  a raw command and not a 4th render-graph pass, the dual-branch capability
  probe, and the accepted Frame Debugger attribution gap) — this campaign's
  own scope (7 phases, two dedicated high-risk reviews, several
  campaign-wide Locked Design Decisions) clearly warranted a dedicated
  convention file, matching GPU Vertex Skinning's own precedent rather than
  staying purely inline like the smaller `render-pass-2`-sized campaigns.
  `docs/README.md`'s own documentation index was also updated with a
  matching entry.
- **`README.md`**'s "Status" section gained one new bullet at the very top
  (most recent first), written in the same voice/detail level as the
  existing entries, cross-checked against every `PHASEn_COMPLETION_REPORT.md`
  (not this campaign's own original PHASE0 plan) for accuracy wherever the
  two could have disagreed — an honest, as-built description, including the
  Game-View-only scope decision and the explicit restated Non-Goals.
- **`task_manager/GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`**'s
  own "Step 0: Status Update" section gained a final closing note that Phase
  C onward is now fully closed by this campaign, mirroring how that same
  document's Phase A/B were marked "ALREADY SHIPPED" once the compute-shader
  campaign closed them.
- **`task_manager/RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md`**
  — Section B.2 (the buffer-side cross-pass read), C.2 (indirect draw
  support), and C.3 (GPU-side culling) all marked ✅ DONE in place (struck
  through, with a full description of what shipped and a pointer to this
  campaign), mirroring the exact editing convention already used throughout
  that file for the texture-side equivalents. Section D's own prioritized
  list was updated to reflect all of this — only async compute/bindless
  descriptors (item 6) remain genuinely open there.
- **`task_manager/COMPUTE_SHADER_FEATURES_DELIBERATELY_NOT_IMPLEMENTED.md`**
  — Section C's item 1 (buffer-side validation) marked ✅ DONE in place, the
  same way.

### 3.2 — Full build + full regression

```
cmake --build build
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```

- `cmake --build build` — **succeeded** cleanly (zero errors/warnings).
- `ctest -C Debug --output-on-failure` — **100% tests passed out of 1717**
  (1 pre-existing, environment-gated skip:
  `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`, the
  same one every recent campaign's own full-suite run reports as skipped on
  this machine — not a regression, not caused by this campaign). **Up from
  `mrt-1`'s own 1683 baseline** (the most recent prior campaign's own final
  full-suite count, confirmed by reading `task_manager/mrt-1/CAMPAIGN_COMPLETION_REPORT.md`) —
  **+34 new tests**, matching this campaign's own added-test count exactly
  (PHASE1: 17 new `CullingTypes`/`CullingTypesAABBIntersectsFrustumTest`/
  `RenderGraphResourceAccessTest`/`RenderGraphBarrierPlannerTest` cases;
  PHASE2: 3 new `DrawStatsTest` cases; PHASE4: 14 new `RenderBatchingTest`
  cases; PHASE3/PHASE5/PHASE6 added zero new automated test files, verified
  instead via live GPU/HTTP harnesses per their own documented verification
  strategy). **No failure of any kind was found anywhere in the full
  suite** — no diagnosis/fix/delegated regression task was needed, since the
  PHASE7 strategy document's own contingency plan for that case (a genuine
  defect requiring a dedicated `delegate_task` follow-up, per this
  campaign's Note 4 working agreement) never triggered.

## 3.3 — Final live smoke test

Performed against the FINAL, fully-integrated, fully-built binary (the exact
same `build/GreatTamanaEngine.exe` the full `ctest` pass above was run
alongside), launched via `run_app_background` and driven entirely over HTTP
(`gte_send_request`):

1. **Boot / default-behavior check**: `GET /get_logs?min_level=Warning`
   returned `count: 0`; `GET /get_game_view` showed the engine's completely
   normal, unmodified sky-background Game View with zero mesh entities —
   confirming this campaign's whole "no default behavior change" promise
   holds in the final, fully-integrated build.
2. **`POST /spawn_gpu_driven_test_batch` spawns a real, qualifying batch**:
   `{"instance_count":6,"success":true}` — 6 new `GpuDrivenTestBatch*`
   entities appeared in the Hierarchy, all sharing one
   `(MeshHandle, PipelineHandle)` pair (`6 >= kMinInstancesForGpuDrivenBatch`
   = 4).
3. **`GET /get_game_view` shows 6 real quads**, rendered through the new
   indirect path.
4. **The Editor's "Render Graph" panel shows the new compute pass and the
   new indirect graphics pass, in the correct order, neither culled** —
   confirmed via `GET /activate_tab?name=Render%20Graph` +
   `GET /get_swapchain`: `RenderOpaque` → `GpuDrivenBatch0 ResetCount` →
   `GpuDrivenBatch0 Culling` → `GpuDrivenBatch0 IndirectDraw` →
   `DrawSkyBackground`, exactly the order PHASE5/PHASE6 already established
   and locked, with real read/write buffer names shown for each pass
   (`GpuDrivenBatch0.Input` / `GpuDrivenBatch0.IndirectCommands,
   GpuDrivenBatch0.VisibleCount`).
5. **The "instances culled this frame" readout is real and correct**:
   `"GpuDrivenBatch0: 6 / 6 instances visible (0 culled)"` while all 6 quads
   are in view.
6. **Culling genuinely responds to movement, and the readout visibly drops
   and recovers**: moved 3 of the 6 spawned entities to `x=1000` via
   `POST /set_entity_trs` (the engine's own default Camera carries no
   `Name` component, so this smoke test moved the OBJECTS out of the
   frustum instead of the camera, exactly mirroring PHASE6's own identical,
   API-constrained approach). `GET /get_game_view` showed only 3 quads; the
   Render Graph panel's own readout simultaneously showed
   `"GpuDrivenBatch0: 3 / 6 instances visible (3 culled)"` — the actual,
   visible proof culling is happening, not just compiling. Moved all 3
   back into view (distinct, non-overlapping positions); the readout
   recovered to `"6 / 6 instances visible (0 culled)"` and `GET /get_game_view`
   showed all 6 quads again.
7. **Screenshot comparison, Game View (new GPU-driven path) vs. Scene View
   (the fully unmodified per-entity path, Locked Design Decision 11)**: with
   the same 6 entities live, `GET /activate_tab?name=Scene` +
   `GET /get_swapchain` showed the same 6 quads, and the Render Graph
   panel's own `RenderOpaque` row for that frame showed **`Draws: 6,
   Tris: 12`** (6 quads × 2 triangles), confirming Scene View drew every
   entity through the unmodified per-entity path — and the "GPU-Driven
   Batches" section correctly reported `"No GPU-driven-eligible batch is
   live this frame (Game View only - see PHASE0_MASTER_STRATEGY.md's Locked
   Design Decision 11/7)."` for that same frame — direct, live proof of
   Locked Design Decision 11, re-confirmed against the fully-integrated,
   fully-tested final build.
8. **Zero new validation-layer/engine warnings across the entire session**:
   `GET /get_logs?min_level=Warning` returned `count: 0` at every checkpoint
   above. Validation layers themselves remain unavailable on this
   development machine (a pre-existing environment fact, already documented
   identically by every prior phase in this campaign) — verification relied
   on the complete absence of Warning/Error Logger entries plus the
   visually-correct screenshots above, exactly like every prior phase.
9. **Cleanup**: the app was cleanly terminated via `stop_app_background`.

## Definition of Done for the whole campaign — final re-confirmation

Every item in `PHASE0_MASTER_STRATEGY.md`'s own "Definition of Done for the
whole campaign" section, re-confirmed true here against the final build:

- `cmake --build build` succeeds (default configuration, `GTE_ENABLE_EDITOR`
  ON) — **confirmed** (3.2 above).
- `ctest` passes, including every new test this campaign added, with zero
  regressions against the baseline recorded at the start of PHASE7 —
  **confirmed**, 1717/1717 (100%), 1 pre-existing environment-gated skip, up
  from `mrt-1`'s own 1683 baseline (+34 new tests, 3.2 above).
- A live scene containing a qualifying batch renders through exactly one
  compute culling pass + one indirect draw call per group, visually
  identical to the pre-cutover per-entity rendering — **confirmed** (3.3
  above, and already established live in PHASE5/PHASE6).
- Rotating/moving objects so part of a batch leaves the frustum measurably
  reduces that batch's own indirect draw count, visible via the "instances
  culled this frame" readout — **confirmed** (3.3 above).
- Validation layers report zero new warnings/errors — **confirmed** to the
  extent possible on this development machine (validation layers themselves
  are unavailable here, a pre-existing environment fact) — zero
  Warning/Error Logger entries at any point.
- Every entity/group NOT eligible for batching renders exactly as it did
  before this campaign, through the byte-for-byte-unchanged per-entity
  `Renderer::Submit()` path — **confirmed** (PHASE4/PHASE5's own diff
  reviews, re-confirmed by this phase's Scene View check in 3.3).
- Scene View and the rare direct-render-to-swapchain fallback both keep
  rendering every entity through the unmodified per-entity path,
  completely unaffected by this campaign — **confirmed** (3.3 above).
- `AGENTS.md` documents the new capability; `README.md`'s "Status" section
  has a new bullet — **confirmed** (3.1 above).

## What remains genuinely open (every PHASE0 Non-Goal, restated honestly)

Per `PHASE0_MASTER_STRATEGY.md`'s own "Non-Goals (explicitly out of scope
for `render-pass-5`)" section — every one of these remains true after this
campaign closes, not silently dropped:

- **No occlusion culling** — frustum culling only.
- **No hierarchical/two-phase (occlusion-aware) culling.**
- **No LOD selection** inside the culling shader.
- **No bindless/descriptor-indexing infrastructure**, and therefore **no
  multi-material indirect batch** — one pipeline/one untextured mesh per
  batch, exactly matching how `RenderSystem::Draw()` already submits
  per-pipeline today.
- **No async compute / dedicated compute queue** — stays on the single
  existing graphics queue.
- **No primitive-shape (`VertexLayout::PositionColor`) batching** — every
  `PrimitiveMeshGenerator` shape is non-indexed, and this campaign's
  mechanism is indexed-draw-only.
- **No GPU-skinned-mesh batching** — a GPU-skinned model's own per-frame
  vertex buffer/pose is fundamentally per-entity, not shareable across
  "instances" in the sense this campaign means.
- **No removal of the existing per-entity `Renderer::Submit()` path** — it
  remains correct and necessary for every entity/group that doesn't qualify
  for batching, and for Scene View/the direct-render-to-swapchain fallback,
  forever (Locked Design Decision 11).
- **No change to `RenderGraphCompiler.cpp`/`RenderGraphSnapshot.cpp`** —
  both already generalized correctly over an arbitrary buffer read/write,
  confirmed empirically (not merely assumed) by every phase's own targeted
  test runs.
- **No Editor-authored/inspector-editable bounds override UI** — bounds are
  always computed automatically from mesh geometry at load time.
- **Textured (`PositionNormalUv`) batches are explicitly out of scope** —
  would need bindless/descriptor-indexing or a same-texture-only sub-batch
  split, both real, separate design problems, never attempted here.
- **A batched entity's Frame Debugger per-entity attribution is an accepted,
  narrow, cosmetic-introspection-only regression** (PHASE5) — the entity's
  geometry still renders correctly and still appears correctly in every
  Frame Debugger replay-step preview image; only its own live
  `"RenderOpaque"` pass attribution row is skipped.
- **The PHASE0 Locked Design Decision 10 dedicated post-implementation
  double-check for PHASE3/PHASE5 was never actually performed** — see
  "Known process deviation" above; a real, permanent, honestly-recorded gap
  in this campaign's own process compliance, not a code/feature gap.

## Files changed in this phase

- `AGENTS.md` (new "GPU-Driven Rendering (Frustum Culling + Indirect Draw)"
  section)
- `README.md` (new "Status" bullet, at the top)
- `docs/README.md` (new documentation-index entry)
- `docs/conventions/gpu-driven-rendering.md` (new file)
- `task_manager/GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md` (Step 0
  status update closing note)
- `task_manager/RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md`
  (Sections B.2/C.2/C.3/D updated to ✅ DONE)
- `task_manager/COMPUTE_SHADER_FEATURES_DELIBERATELY_NOT_IMPLEMENTED.md`
  (Section C item 1 updated to ✅ DONE)
- `task_manager/render-pass-5/CAMPAIGN_COMPLETION_REPORT.md` (this file)

No production source code was changed in this phase — the full build/`ctest`
run found zero regressions, so no dedicated regression-fix delegation or
commit was needed (per this phase's own "no scope additions of any kind"
rule).

## Final visual-proof evidence (summary)

See "3.3 — Final live smoke test" above for the full narrative. In short,
against the FINAL, fully-integrated, fully-built binary: the engine boots
identically to every prior campaign; spawning a real 6-instance batch over
HTTP renders it correctly through the new compute-cull + indirect-draw path,
the "Render Graph" panel shows the new passes in the correct order with a
live, correct "instances culled this frame" readout that visibly tracks
objects moving in and out of the frustum; and Scene View keeps rendering
every entity through the fully unmodified per-entity path throughout,
exactly as Locked Design Decision 11 requires — with zero warnings/errors at
any point in the session.

## Git

PHASE1-6's own work was already staged and committed in prior sessions. This
phase's documentation updates and this completion report are staged and
committed together in this phase's own commit — see the repository history
for the exact commit.
