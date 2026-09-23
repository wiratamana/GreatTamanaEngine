# PHASE6 — Completion Report: Editor Tooling + Live Validation

## Status: DONE

Implemented exactly as scoped in `PHASE6_EDITOR_TOOLING_AND_LIVE_VALIDATION.md`.
Read `readme.md`, `AGENTS.md`, `PHASE0_MASTER_STRATEGY.md`, and `PHASE1`–`PHASE5`'s
own completion reports (plus `PHASE3_STRATEGY_DOUBLE_CHECK_REPORT.md`/
`PHASE5_STRATEGY_DOUBLE_CHECK_REPORT.md`) in full before starting. This phase is
NOT one of the two dedicated-double-check phases (PHASE3/PHASE5 only), so no
extra double-check delegation was spawned here, per PHASE0's own instructions.

## A pre-existing discrepancy found and NOT acted on (per this phase's own scope)

`PHASE5_COMPLETION_REPORT.md`'s own final paragraph states: *"a separate,
dedicated `delegate_task` review of ONLY this phase's committed diff was spawned
immediately after this report and its accompanying code were committed — see
`PHASE5_DEDICATED_DOUBLE_CHECK_REPORT.md`, this same folder, for its own
findings once it completes."* No file by that name exists anywhere in this
repository (confirmed via `browse_dir`/`search_in_dir` before starting this
phase) — that promised report was apparently never actually produced in a prior
session. Per this task's own explicit instructions ("This phase is NOT one of
the two dedicated-double-check phases... do not spawn any extra double-check
delegation from this phase"), this gap was **not** resolved here (no new
double-check delegation was spawned) — it is simply recorded here as a factual
observation for whoever runs PHASE7's own final campaign-wide review.

## Step 2 — Demo-scene inventory (done first, per the phase doc's own instruction)

Confirmed by direct inspection of `Game.cpp`/`MeshAssetGpuCatalog.cpp` before
writing any code: the engine's real demo scene spawns **zero** mesh entities by
default (`Game::EnsureDefaultCameraExists()` only ever creates a bare Camera —
the old hardcoded demo triangles were removed by an earlier campaign, see that
method's own doc comment); the project's one real imported asset
(`build/Project/terrain.gta`) is fully **textured** (batching-ineligible per
PHASE0's Locked Design Decision 8); and every `PrimitiveMeshGenerator` shape is
**non-indexed** (also ineligible, Locked Design Decision 6). No existing content
already produces a qualifying GPU-driven batch — confirmed, not assumed.

## `ask_questions` outcome (Step 2/3.2's own required checkpoint)

Asked the project owner three questions before writing any spawn-mechanism code:

1. **What kind of mechanism?** → *"Both a new HTTP endpoint AND an Editor menu
   action calling the same underlying Game:: method."*
2. **Gated behind a compile-time switch, or always-on?** → *"Gated behind
   `GTE_ENABLE_EDITOR` only."*
3. **Default instance count?** → *"Make the count caller-configurable (e.g. an
   optional HTTP request parameter), defaulting to 6."*

## What was built

### 3.2 — The validation spawn mechanism

- **`src/Editor/GpuDrivenBatchTestSpawner.h`/`.cpp`** (new, `GTE_ENABLE_EDITOR`-only
  file, registered in `CMakeLists.txt`'s existing Editor-only `target_sources()`
  block right after `GBufferValidation.cpp`) — `GpuDrivenBatchTestSpawner::Spawn(Game&, Renderer&, std::uint32_t instanceCount)`
  lazily builds ONE shared, hand-authored, untextured, indexed, unit quad Mesh
  (`VertexLayout::PositionNormal`, matching `MeshAssetGpuCatalog::EnsureMeshPipeline()`'s
  exact `"shaders/Mesh.vert.spv"`/`"shaders/Mesh.frag.spv"` pair so
  `GpuDrivenBatchCache::ResolveInstancedPipeline()`'s own hardcoded shader-pair
  lookup resolves it with zero further wiring) plus its matching Pipeline
  (cached as function-local statics — safe, exactly one Game/RenderSystem
  instance ever exists per process), then spawns `instanceCount` new entities
  (Transform in a simple left-to-right row in front of the default Camera +
  MeshRenderer + auto-de-duplicated Name `"GpuDrivenTestBatch"`).
- Satisfies the "GTE_ENABLE_EDITOR only" decision honestly: the entire real
  implementation lives in this one Editor-only file/translation unit — nothing
  outside `GTE_ENABLE_EDITOR` ever links it in.
- **Editor menu**: `Panels/HierarchyPanel.cpp`'s existing right-click context
  menu gained a new "Create GPU-Driven Test Batch" entry (alongside "Create 3D
  Object"/"Create Directional Light"), calling
  `GpuDrivenBatchTestSpawner::Spawn(game, renderer, 6)` **directly** — no
  `IEditorLayer` indirection needed, since that file is itself Editor-only.
- **HTTP endpoint**: `POST /spawn_gpu_driven_test_batch` (optional JSON body
  `{"count": <uint>}`, default 6) — reuses the existing `EditorUiCommandBridge`
  (mirrors `/activate_tab`'s exact shape), a new
  `EditorUiCommandKind::SpawnGpuDrivenTestBatch` +
  `SpawnGpuDrivenTestBatchCommand`/`SpawnGpuDrivenTestBatchOutcome`, a new
  `IEditorLayer::SpawnGpuDrivenTestBatch(Game&, Renderer&, std::uint32_t)` pure
  virtual (implemented in `ImGuiEditorLayer.cpp` as a thin forward into the
  spawner; `NullEditorLayer` returns `success == false` with an explanatory
  `errorMessage` — a release build reports this as HTTP 503, mirroring
  `/save_scene`'s own "editorAvailable" convention). `NetworkRoutes.h`/`.cpp`
  gained `ParseSpawnGpuDrivenTestBatchRequest()`/
  `BuildSpawnGpuDrivenTestBatchResponseJson()`; `NetworkServer.cpp` registers the
  route, parse → bridge-unavailable(503) → `SubmitAndWait()` →
  alreadyPending(503)/timedOut(504)/outcome(200 or 400/503) — the same shape
  every other bridge-backed route in this file already uses.

### 3.1 — "Instances culled this frame" readout

- **`GpuDrivenBatchCache`** gained a new per-entry `countReadbackBuffer`
  (`BufferMemoryUsage::GpuToCpu`, one `std::uint32_t`, created once and
  deliberately **preserved** across a capacity grow — sized independently of
  `capacity`) and `ReadLastKnownVisibleCount(key)` — a cheap, non-blocking,
  side-effect-free read of that buffer's already-mapped memory.
- **The count-buffer readback copy**: a genuine, load-bearing correctness issue
  was discovered and fixed during this phase (see "A real bug found and fixed"
  below) — the copy is issued as a **raw command directly against
  `offscreenCmd`**, immediately after `m_renderGraph.Execute(...)` returns and
  before `Renderer::EndOffscreenRenderGraphRecording()` (which already
  fence-waits) — **not** as a 4th render-graph pass, which is structurally
  impossible to keep alive through `RenderGraphCompiler::Compile()`'s own
  backward-reachability culling.
- **`PassContext::recordIndirectDraw`** (new field, `RenderGraph.h`/`.cpp`) —
  the indirect-draw sibling of the existing `recordDraw` — wires
  `DrawStats::indirectDrawCount` (built in PHASE2, never wired into a real
  per-pass return value since) into a real per-pass return value for the first
  time, called from `"<batch> IndirectDraw"`'s own `execute` lambda right after
  `Renderer::SubmitIndirect()`.
- **`GpuDrivenBatchDebugInfo`** (new, dependency-free struct, `EditorLayer.h`) —
  `batchName`/`instanceCount`/`visibleCount` (an `std::optional<std::uint32_t>` —
  never a fabricated 0). `IEditorLayer::BuildUI()` gained a new trailing
  `const std::vector<GpuDrivenBatchDebugInfo>&` parameter; `Application::Run()`
  builds this list fresh every frame, immediately after
  `EndOffscreenRenderGraphRecording()` returns (the exact point every batch's
  buffers this frame's culling dispatch wrote are already fence-proven
  complete — reusing that already-happening wait, never adding a new one, per
  AGENTS.md's "Profiling" section).
- **`Panels/RenderGraphPanel.h`/`.cpp`** — a new "GPU-Driven Batches (instances
  culled this frame)" section, one line per eligible batch:
  `"<name>: N / M instances visible (K culled)"` (or `"pending"` for the rare
  frame the GPU readback isn't available yet) — placed **first**, right after
  the Pause row (a deliberate placement choice, not locked by the phase doc:
  keeps it visible without scrolling past both regimes' own pass/resource
  tables, and is this panel's newest, most immediately actionable signal).

## A real bug found and fixed during this phase (worth recording)

The first implementation declared a 4th, buffer-only-write render-graph pass,
`"<batch> CopyCountForReadback"` (`ReadBuffer(countHandle, TransferSrc)` +
`vkCmdCopyBuffer` in its own `execute`), placed after `"<batch> IndirectDraw"`
in the per-batch loop. Live testing showed the readout permanently reporting
`0 / 6 instances visible (6 culled)` even though the Game View correctly
rendered all 6 quads. Direct re-read of `RenderGraphCompiler.cpp`'s Step 2
(backward-reachability culling) confirmed the root cause: **a `Buffer` write is
NEVER treated as a culling "root"** (`isRoot = false` unconditionally for
`ResourceKind::Buffer` — only `Texture`/`VolumeTexture` writes can ever be
roots), and a pass with no texture/volume-texture write of its own can only
survive by being a transitive predecessor of some OTHER kept pass — which never
happens here, since nothing downstream ever reads `countReadbackBuffer` through
the graph. The pass was therefore **silently culled every single frame**,
its `execute` callback never once invoked — an existing test,
`RenderGraphCompilerTest.BufferOnlyWriteSurvivesCullingOnlyWhenAReaderReachesATextureFinalOutput`,
already codified this exact rule; this phase just hadn't been checked against
it before implementation. Fixed by removing that 4th pass entirely and issuing
the count-buffer copy (with its own explicit `ComputeShaderWrite -> TransferRead`
`vkCmdPipelineBarrier2`, since the render graph's own automatic barrier for
`"<batch> IndirectDraw"`'s `IndirectCommandRead` access does not also grant
visibility for a different access kind/stage) as a raw command directly against
`offscreenCmd`, outside the render graph's declarative system entirely — see
"What was built" above. Re-verified live afterward: the readout correctly
tracks the real GPU-computed visible count every frame (see 3.3 below).

## 3.3 — Live validation checklist (run against a real, running engine)

All of the following were confirmed against a live, `run_app_background`-launched
`GreatTamanaEngine.exe`, driven entirely over HTTP (`gte_send_request`):

1. **`POST /spawn_gpu_driven_test_batch` spawns a real, qualifying batch.**
   `{"instance_count":6,"success":true}` — 6 new `GpuDrivenTestBatch*` entities
   appear in the Hierarchy panel, all sharing one `(MeshHandle, PipelineHandle)`
   pair, `6 >= kMinInstancesForGpuDrivenBatch (4)`.
2. **`GET /get_game_view` shows 6 real quads**, rendered through the new
   indirect path.
3. **The Editor's "Render Graph" panel shows the new compute pass and the new
   indirect graphics pass, in the correct order, neither culled**: confirmed
   via `GET /activate_tab?name=Render%20Graph` + `GET /get_swapchain` —
   `RenderOpaque` (Draws: 0 — every batched entity correctly excluded from the
   per-entity path) → `GpuDrivenBatch0 ResetCount` → `GpuDrivenBatch0 Culling`
   → `GpuDrivenBatch0 IndirectDraw` → `DrawSkyBackground`, exactly the order
   PHASE5 already established and locked.
4. **The new "instances culled this frame" readout is real and correct**:
   `"GpuDrivenBatch0: 6 / 6 instances visible (0 culled)"` while all 6 quads are
   in view.
5. **Culling genuinely responds to movement, and the readout visibly drops and
   recovers**: moved 3 of the 6 spawned entities to `x=1000` via
   `POST /set_entity_trs` (using their own auto-de-duplicated names,
   `"GpuDrivenTestBatch (3)"`/`"(4)"`/`"(5)"` — the engine's own default Camera
   carries no `Name` component and therefore cannot be addressed by
   `/set_entity_trs` at all, so this phase moved the OBJECTS out of the
   frustum instead of the camera; this is an equally valid and, for automated
   repeatability, an even more precise way to exercise the exact same
   frustum-relative-motion code path the phase doc's own checklist describes,
   given this real API constraint). `GET /get_game_view` showed only 3 quads;
   the Render Graph panel's own readout simultaneously showed
   `"GpuDrivenBatch0: 3 / 6 instances visible (3 culled)"` — the actual,
   visible proof culling is happening, not just compiling. Moved all 3 back
   in view; the readout recovered to `"6 / 6 instances visible (0 culled)"`.
6. **Screenshot comparison, Game View (new GPU-driven path) vs. Scene View (the
   fully unmodified per-entity path, Locked Design Decision 11)**: with the
   exact same 6 entities live, `GET /activate_tab?name=Scene` +
   `GET /get_swapchain` showed the **same 6 quads**, and the Render Graph
   panel's own `RenderOpaque` row for that frame showed **`Draws: 6, Tris: 12`**
   (6 quads × 2 triangles), confirming Scene View drew every entity through the
   unmodified per-entity path, and that the "GPU-Driven Batches" section
   correctly reported `"No GPU-driven-eligible batch is live this frame (Game
   View only...)"` for that same frame (Game wasn't the visible/rendered tab,
   so `m_gpuDrivenBatchesThisFrame` was correctly empty) — direct, live proof
   of Locked Design Decision 11, mirroring PHASE5's own identical verification.
7. **Zero new validation-layer/engine warnings across the entire session**:
   `GET /get_logs?min_level=Warning` returned `count: 0` at every checkpoint
   above. Validation layers themselves remain unavailable on this development
   machine (a pre-existing environment fact, already documented identically by
   PHASE2/PHASE3's own completion reports) — verification relied on the
   complete absence of Warning/Error Logger entries plus the visually-correct
   screenshots above, exactly like every prior phase in this campaign.

## Deviations from the phase document

1. **The "instances culled" readback mechanism is NOT a 4th render-graph pass**
   as might be naturally assumed from PHASE5's own 3-pass precedent — see "A
   real bug found and fixed" above for why that shape is structurally
   impossible, and what was built instead.
2. **The camera-motion half of the live checklist used object motion instead**,
   since the engine's own default Camera carries no `Name` component and
   `POST /set_entity_trs` can only address a named entity — a real, discovered
   API constraint, not a shortcut; the resulting verification is equally
   rigorous (see 3.3/5 above).
3. The "GPU-Driven Batches" readout section is placed at the TOP of the
   "Render Graph" panel (right after the Pause row), not interleaved with or
   after the two regime sections — an explicit, reasoned UX/live-verification
   convenience choice, not locked by the phase doc.

## What We Did NOT Do (matches the phase doc's own scope)

- No debug frustum-wireframe overlay, no bounding-box gizmo rendering — purely
  a numeric readout.
- No automated visual-diff/screenshot-comparison TEST — this phase's checklist
  is the accepted manual/HTTP-driven verification, not a new automated test
  suite.
- No new `Profiling::GpuPass` fixed-enum entry — surfaced entirely through the
  "Render Graph" panel's own already-generic mechanism.
- No extra `delegate_task` double-check spawned from this phase (see the
  pre-existing-discrepancy note above for why not, despite what
  `PHASE5_COMPLETION_REPORT.md` implied would exist).

## Compile check (per Locked Design Decision 9 — targeted only, no full build)

- `cmake -S . -B build` (reconfigure, new source files added to
  `target_sources()`) + `cmake --build build --target GreatTamanaEngine --config Debug`
  — succeeds cleanly, zero warnings/errors.
- `cmake --build build --target GreatTamanaEngineTests --config Debug` —
  succeeds cleanly.
- Targeted test run: `*RenderGraph*:*DrawStats*:*CullingTypes*:*RenderBatching*:*RenderSystemTest*:*EditorUiCommandBridge*:*ActivateTab*:*NetworkRoutes*`
  — **312/312 tests passed**, confirming zero regression from the
  `PassContext`/`EditorUiCommandBridge`/`IEditorLayer::BuildUI()`/
  `RenderGraphPanel::Build()` signature changes (all pre-existing test suites
  updated their own call sites correctly). No full `cmake --build build` +
  full `ctest` pass was run, per `PHASE0_MASTER_STRATEGY.md`'s Locked Design
  Decision 9 (reserved for PHASE7 only).

## Next phase

PHASE7 (`PHASE7_DOCS_FULL_BUILD_AND_CAMPAIGN_COMPLETION.md`) can now depend on:
the whole campaign being live, wired, and validated end-to-end, including a
real, repeatable, HTTP-driven way to reach a qualifying batch
(`POST /spawn_gpu_driven_test_batch`) for its own final smoke test; a working
"instances culled this frame" readout; and should note, in its own review, the
pre-existing `PHASE5_DEDICATED_DOUBLE_CHECK_REPORT.md` discrepancy recorded
above (never produced in an earlier session, not resolved by this phase per its
own explicit scope).
