# GPU-Driven Clusters (Cluster Culling + Indirect Draw)

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

A big static mesh is cut into clusters (up to 64 vertices / 124 triangles each). A compute
pass culls the clusters per view and one indirect draw renders the survivors. The CPU draw
path stays complete: a mesh without a valid cluster table, or any refusal below, draws on
the CPU path. The older per-batch path is described in
[gpu-driven-rendering.md](gpu-driven-rendering.md).

## Data format

- `MeshCluster` (`Assets/MeshCluster.h`) is a 64-byte record in mesh local space: bounding
  sphere, normal cone, AABB, `firstIndex`, `indexCount`. The file record and the GPU record
  are the same bytes.
- The mesh payload (`Assets/MeshFile.cpp`) may end with a cluster tail after the optional
  tangent tail: `'GCL1'`, `clusterCount`, the records, then a CRC-32 of count and table.
  A file without the tail is byte-identical to the old format. Old engine builds reject a
  clustered file.
- The index buffer is cluster-ordered and still a valid mesh. Clusters are metadata over
  index ranges, so every CPU path keeps working.
- The cone cutoff is the **sine** of the cone half angle. A triangle normal `n` of a cluster
  with cutoff < 1 satisfies `dot(n, axis) >= sqrt(1 - cutoff * cutoff)`.
- The table must tile `[0, indexCount)` exactly. `ValidateClusterTable()` checks this and is
  called by the builder, the decoder and the catalog.
- A damaged tail (truncated, bad count, bad CRC, failed validation, trailing bytes) never
  rejects the mesh. The decoder returns the mesh without clusters and a rejection text, and
  the catalog logs it once. Damage to the vertex, index or tangent data still rejects the file.
- `EncodeMeshDataToBytes()` returns an empty result (and nothing is written) for mismatched
  array lengths, counts above 32 bits, out-of-range indices or an invalid cluster table.

Mesh payload byte layout (little-endian, IEEE 754 floats, each array one contiguous block):

| Block | Size |
|---|---|
| magic `GTEMESH1` | 8 bytes |
| `vertexCount`, `indexCount` | 4 + 4 bytes |
| positions (xyz) | `vertexCount * 12` |
| normals (xyz) | `vertexCount * 12` |
| uvs (xy) | `vertexCount * 8` |
| indices (uint32) | `indexCount * 4` |
| optional tangent tail | 4 bytes `tangentCount` (== `vertexCount`), then `tangentCount * 16` (xyzw) |
| optional cluster tail | `GCL1`, 4 bytes `clusterCount` (>= 1), `clusterCount * 64` records, 4 bytes CRC-32 of count + table |

Nothing may follow the last block.

## Building clusters

`BuildMeshClusters()` (`Assets/MeshClustering.h`) validates, welds, clusters per material
slice with meshoptimizer, reorders vertices and validates the result. It never throws and
leaves the mesh unchanged on failure.

| Weld | Effect |
|---|---|
| `Exact` | Merges identical vertices only. Lossless. A mesh of unshared triangles stays poorly filled. |
| `PositionOnly` | Merges by position and recomputes smooth normals. Refuses meshes with a texture or non-zero uvs. |

Meshes with skin weights, bones, morphs or physics are refused: welding reorders vertices.
`report.poorFill` is set when the average fill is below 50% of the triangle limit.

Four doors create a cluster table:

1. **Import hook.** `ImportAssetFile()` takes a required `MeshImportOptions`. `Off` never
   clusters. `Always` clusters rigless meshes. `Auto` (default) needs at least
   `autoTriangleThreshold` (100000) triangles and skips the tail when the fill is poor. A mesh with a textured or PBR
   material (`MaterialDrawsTextured()`) is clustered by `Auto` only with the `Exact` weld, which keeps the uvs; with any
   other weld it is skipped ("needs the Exact weld"). The cheap pre-check stops right after the weld.
   Clustering never fails an import: `clusterMessage` carries the reason.
2. **Inspector.** "Build Clusters" in the mesh asset tools starts the editor's single
   `MeshClusterBuildRunner`. `EditorHost` polls it once per frame.
3. **HTTP.** `POST /build_clusters` (see [networking.md](networking.md)).
4. **Clone.** "Create Clustered Copy" in the Inspector or `POST /clone_clusters` writes `<stem>_clustered.gta` next to the
   source with a new GUID and never changes the source. Weld `Auto` (default) tries `Exact`, then `PositionOnly` when the fill
   is poor. `AtomicFile` has a create-only mode, so an existing file is never replaced.

Doors 1 to 3 rewrite or create the asset in place and give the same bytes for the same input and options.

A mesh only joins the GPU path when it **has a cluster table** (or a frame holds at least 4 instances of it, see
"Runtime table"). A big mesh without a table stays on the CPU path. `Auto` import skips meshes of unshared triangles
(typical for STL: "Clusters skipped: expected cluster fill is poor"). Use "Create Clustered Copy" (Auto weld) or the
`PositionOnly` weld for them. `PositionOnly` recomputes smooth normals, which is why the clone leaves the original untouched.

The write is atomic (`AtomicFile.h`): a temp file next to the target, an identity check
(size + last write time) before the rename, and one writer per target. A failed or
cancelled build leaves the old file byte-identical.

Stale temp files are swept once at startup. The project root is the executable directory plus
`Project` (`ResolveProjectRootDirectory()`) and never changes while the editor runs, so opening a
project assembly needs no second sweep.

An import (Project panel or `POST /import_asset`) and a cluster build of the same `*.gta` have
no separate guard. The atomic-write rules are the guard. A build records the file identity when
it starts, so an import that replaced the file meanwhile makes the build fail with
"changed on disk". Two writers at the same moment: the second fails with "busy". Neither side
ever leaves a half-written file.

## Runtime table

`MeshAssetGpuCatalog::EnsureMeshAsset()` rebases the clusters onto the untextured part's
concatenated index buffer and, for every opaque static textured or PBR slice, onto that slice's own index buffer. It validates
them and uploads one device-local `MeshClusterTable` buffer into each `Mesh` (`Mesh::HasClusters()`). Alpha-tested slices and
GPU-skinned models get no table. A table above `kMaxMeshClusters` is dropped before upload. A failed upload drops the
table. Each case logs once and the mesh draws on the CPU path.

An untextured mesh without a table still joins the cluster path when a frame holds at least
`kMinInstancesForSynthesizedClusters` (4) instances of it. `Core::EnsureClusterTable()` builds
one cluster over the whole index range from the mesh bounds (no cone), uploads it once and keeps
it in the `Mesh`. A failed synthesis backs off 120 frames, doubling, and the mesh draws on the CPU path.

Respawn to apply new data: `InvalidateCachedMeshAsset()` only affects new spawns. The mesh
sweep (`ReleaseUnusedGpuResources`, same throttle as the material sweep) frees catalog-owned
meshes that nothing uses, including meshes held only by the GPU-skinning rig cache.

## Frame flow

1. `Core::BuildFrame` builds the candidate list once with one `CollectRenderables()` call
   (`SelectClusterCandidates`): a candidate has clusters (or is an untextured mesh synthesizable with enough instances), an index
   buffer, a vertex layout of `PositionNormal`, `PositionNormalUv` or `PositionNormalUvTangent` (`ClusterMaterialKind`
   `None`, `Textured`, `Pbr`), a material that resolves for the textured kinds, is opaque (not alpha-tested), is not
   GPU-skinned, has a finite world matrix, and the device caps are present. Every rejected renderable is counted by reason
   (`Frame::ClusterRejections()`). Flags (uniform scale, positive handedness) and max scale are computed per instance.
2. Render features read `Frame::ClusterCandidates()`. They never walk the ECS. A part is one (mesh, material) pair.
3. A feature calls `Frame::ClusterDraw(state, lane)` on a view frame (the Opaque stage or a
   depth-only lane) and `Plan()` / `Record()`.
4. `Record()` declares, per part, in this order: **Reset** (`FillBuffer` of the count),
   **Cull** (`ClusterCull.comp`), **Draw** (indirect draw with `MeshCluster.vert`, `MeshClusterUv.vert` or `MeshClusterTangent.vert`),
   and an optional **Stats** readback (colour target only). A two-phase chain records this for every part
   (phase 1), then the pyramid build, then again for every part (phase 2). It stops at the first refusal.
   Descriptor sets: 0 = material, 1 = scene services, 2 = cluster buffers. A lit textured or PBR part binds its material set
   at set 0 and reuses `TexturedMesh.frag` / `PbrMesh.frag`. Depth-only (cascade) chains need no material.
5. Only after every step of a part was declared does the verb claim the entities
   (`ClusterClaim.h`). `RenderOpaque` skips claimed entities of its own view scope. A depth-only
   chain fills an exclusion set that the CPU `SceneDraw` reads at execute time.

Rules:

- **Claim after proof.** An entity is claimed only when its whole chain was declared. A chain
  that stops part way claims nothing, so the CPU path draws the entity.
- **Capture frames never claim or cull** for the primary view. While the Frame Debugger window is open
  and enabled, the primary view draws every entity on the CPU path with no cull, so replay steps stay exact.
- **Per view scope.** Every instance, visible, command, count and history buffer, parameter block and
  claimed set belongs to one (view scope, lane, part slot). Game and Scene views never write the same one; the secondary
  view only reads the primary view's published survivors (see "Shared survivors"). A shared-scope frame has no camera, so the
  verb is inert there.
- **All-or-none toggles.** If the Reset, Cull or Draw pass of a part is toggled off, none of
  them is declared and nothing is claimed.
- One entity is drawn by exactly one path per view and frame.

## Capability matrix

The cluster path needs `drawIndirectCount`, `multiDrawIndirect` and
`drawIndirectFirstInstance` (`ClusterPathSupported()`). If one is missing the verb is inert,
logs once, and the CPU path draws. There is no padded fallback.

## Limits and budgets

All math is 64-bit. Constants live in `Renderer/Culling/ClusterCullMath.h`; lane, slot and pass-name counts in `ClusterPassCounts.h`.

| Limit | Value |
|---|---|
| Work items per part (instances x clusters) | min(65535 x 64, device max indirect count, buffer cap / 20, buffer cap / 8) |
| Buffer cap (one binding) | 128 MiB |
| Clusters per table | buffer cap / 64 |
| Chain memory per state and scope | 256 MiB (power-of-two capacity). A two-phase part holds two lanes of visible, command and count buffers plus 4 bytes of history per work item (`ClusterChainBytes`). A count buffer is 36 bytes: the survivor count and 8 outcome counters. |
| Chain memory, all states and scopes | 512 MiB (`ClusterDrawLedger`) |
| Idle release | a scope with no chain for 600 frames frees its buffers |
| Part slots / lanes | colour views 12 slots x 2 lanes (phase 1, phase 2), depth-only users 4 lanes x 4 slots |

A part is worth a chain at 2 or more clusters, or 4 or more instances. A part over a limit,
beyond the slots, or over a budget is not claimed, logs once and draws on the CPU path.
Instance counts are never truncated.

## Cull math and cone test

`ClusterCullMath.h` is the CPU oracle for `ClusterCull.comp`. If the two disagree, the oracle
is right. Shader constants are generated from `ClusterCullConstants.h`, so they cannot drift; a text test keeps block names and
bindings equal. The Cluster Viewer does not use the CPU copy of the tests; it reads the GPU result instead.

The frustum test transforms the cluster AABB to world space (NaN boxes are kept). The cone
test is **off by default** and is valid only together with the back-face-culling pipeline
variant: the CPU path draws both sides, so a cone test would delete visible back faces of an
open surface. The flag and the pipeline key are one decision (`ClusterPipelineKeyFor()`), and a
depth-only lane never uses the cone test. Enable it only for closed meshes.

Survivors are appended with `atomicAdd`, so their draw order changes between frames. Two
coincident triangles can swap winners. `MeshHasCoincidentTriangles()` tells whether a mesh has any.
The cluster vertex shaders (`MeshCluster.vert`, `MeshClusterUv.vert`, `MeshClusterTangent.vert`) declare `invariant gl_Position`
with the same expression order as the CPU-path vertex shader of their layout. The pipeline key is (variant, material kind, cull
mode): 18 pipelines (`kClusterPipelineKeyCount`).

## Debug workflow (HTTP)

- `GET /feature_debug/state?name=GpuDrivenScene`: per view (`game.` / `scene.` prefix)
  `claimedEntities`, `parts`, `testedClusters` (instances x clusters given to the cull, not the
  survivors), `inertReason` (none, device caps, shared scope, capture armed, toggled off),
  `lastRefusalReason`, `ledgerBytes`.
- **Visible count.** `visibleClusters` (survivors, 2 frames old, with `visibleFrame`) needs
  `collectStats=true`, which adds three passes per part slot. With stats off, or when the tested value of the
  count's frame is gone, `visibleValid` is false and `statsCollected` says why. Stats are off by default.
  The age of a visible count is exactly `frame - visibleFrame`. The profiler does not carry cluster counts: read them from
  here, never as triangles, and take timing from a run with stats off (see [Profiling](profiling.md)).
- `GET /feature_debug/set?name=GpuDrivenScene&mode=Frustum&cone=false&collectStats=true&debugView=ClusterId`.
  `mode` is `NoCull`, `Frustum` or `HiZ`. `GET /feature_debug/invoke?name=GpuDrivenScene&action=reset_defaults` resets all.
- **CPU path numbers.** `cpuTested`, `cpuCulled`, `cpuUnbounded` and `cpuCullState` per view (see "CPU path culling"). Compare
  `draw_call_count` of `RenderOpaque` in `GET /render_graph` between `NoCull` and `Frustum`. Compare milliseconds, not FPS: the
  swapchain presents with VSync.
- **See the cull.** In `Frustum` or `HiZ` mode the secondary view shows exactly what the game camera keeps. Move the secondary
  camera with `GET /scene_camera/set_pose` and fetch `GET /scene_camera/image`.
- **`debugView=ClusterId`** colours each cluster by a hash of its id (`MeshClusterTint.frag`).
  Culled clusters are not visible by design: look at the secondary view from outside the game frustum.
- **`debugView=ClusterPhase`** tints phase 1 draws red and phase 2 draws green (a single-phase chain gray). The phase travels as a
  per-draw tag in the first word of the push-constant model block.
- **`GET /render_graph`** also gives `gpu_barrier_milliseconds` and `gpu_work_milliseconds` per pass (see [networking.md](networking.md)).
  Start the exe with `--no-vsync` for benchmarks. `tools/culling_bench/` has the benchmark and image-equality scripts.
- `GET /render_graph`: per-pass `gpu_timing_milliseconds`, `draw_call_count`,
  `indirect_draw_count`, plus `indirect_draw` (device caps) and `gpu_memory`
  (`total_bytes`, `mesh_count`, `by_name`). Indirect draws never add to `triangle_count`.
  Read `gpu_memory` before and after a series of cluster rebuilds: totals and `mesh_count` must
  return to base once the mesh sweep ran. `gpu_memory` is rebuilt every 30 frames, so wait about
  one second after a change before reading it.
- `POST /build_clusters` rebuilds a table. Read `GET /get_logs` for the `MeshClustering` lines.
- Pass stats belong to each executed pass in `/render_graph`, so the same pass name in two views has two rows.
- **Validation.** Start the exe with `--vulkan-validation` to turn the Vulkan validation layer on in any
  build type (default: on in Debug, off in Release). Read `GET /get_logs?category=Vulkan`. The log sink is
  installed first in `main()`, so init-time validation messages are kept. The state line says
  `validation enabled`, `validation disabled` (Info), `validation requested but layer not available` or
  `validation layer loaded but debug messenger failed` (Warning).
- **Move the Game camera.** `POST /set_entity_trs {"name":"Camera","translation":{"x":..,"y":..,"z":..}}`.
- Every executed pass gets a GPU record in `/render_graph`, by execution order, with a status word. Passes past
  the record budget fold into one overflow record, counted by `gpu_folded_passes`.

## Known limits

- Alpha-tested materials stay on the CPU path: the depth-only cascade pass would need the texture.
- The cluster path needs the synchronous offscreen graph: `GpuParams` and the host-written
  instance buffer are rewritten on the CPU at declare time. A pipelined regime makes the verb
  inert (CPU path).
- One part dispatch is capped at 4,194,240 work items (65535 groups of 64).
- A build cannot be cancelled inside one meshoptimizer call; closing the app waits for the step.
- With one job worker or no job system, a build runs inline and stalls the editor.
- The rename of the atomic write fails when another handle holds the target open. It is atomic
  against a crash, not durable against a power cut.
- Hi-Z occlusion exists only on the cluster path of the primary view. Cubes and other primitives, alpha-tested, skinned and
  transparent meshes are CPU-drawn and get frustum culling only.
- Hi-Z occlusion tests axis-aligned cluster boxes only. A scene side above 16384 pixels has no pyramid (`hizReason=TooLarge`) and the
  chain falls back to `Frustum`.
- A clustered mesh part needs a free slot: 12 per colour view, 4 for depth-only users. More parts draw on the CPU path.
- A phase 1 draw that phase 2 finds occluded stays drawn for that frame. It only costs time and leaves the history next frame.

## Hi-Z occlusion

Occlusion is two-phase and exact for the current frame.

- **Phase 1.** Per part: Reset, Cull and Draw on lane 0 (`ClusterCull.comp` with `PhaseFirst`). It draws the items that were
  visible last frame and still pass frustum and cone. The history is one `uint32` per work item in a persistent buffer per
  (view scope, part slot): bit 0 = visible last frame, bit 1 = drawn in phase 1 this frame. Phase 1 writes bit 1 of every word.
- **Pyramid.** After phase 1, `fx::HizPyramid` builds one `R32_Float` atlas from the opaque-stage depth
  (`OpaqueFrame::SceneDepth()`): level 0 is half the scene depth size, level k is the farthest value of a block, levels go down
  to 1x1 (at most 14 levels, `kHizMaxLevels`). Two dispatches: `<base>.Base` (`HizBuildBase.comp`, levels 0..4 in shared memory)
  and `<base>.Top` (`HizBuildTop.comp`, one group, levels 5 and up), e.g. `GpuDrivenScene.Hiz.Base` and `GpuDrivenScene.Hiz.Top`.
  The layout (`HizPyramidLayout.glsl`) is shared with `ClusterCull.comp`. The atlas is a frame-local target per view, rounded up
  to a multiple of 256 texels. No atlas stays allocated between frames.
- **Phase 2.** Per part: Reset, Cull and Draw on lane 1 (`PhaseSecond`). Every item is tested with frustum, cone and the pyramid of
  this frame. The screen rect is clamped to the screen. A corner behind the eye keeps the box. It draws what phase 1 did not draw
  and writes every history word (bit 0 = visible now). Phase 1 and phase 2 never share output buffers.
- **Depth test.** For a perspective matrix the pyramid depth is turned back into view distance (`HizDepthMapOf`, `depthMap` in
  `ClusterCullParams`, 224 bytes) and compared with a relative margin (`HizBehind`). An orthographic or skewed matrix uses the raw
  rule (`kClusterCullFlagRawDepth`). Clear depth never occludes. Constants live in `Renderer/Culling/ClusterCullConstants.h` and are
  generated into `build/shaders/gen/ClusterCullConstants.glsl` (`cmake/GenerateGlslConstants.cmake`), which the shaders include.
- **History key.** `ClusterHistoryKey` hashes the mesh token, the cluster count, the entity ids in instance order and the buffer
  generation (`PersistentBuffer::Generation`). If the key differs from last frame's, the slot did not run last frame or the buffer
  was recreated, the history is invalid: phase 1 draws nothing and phase 2 draws everything that passes. A wrong history only costs
  time.
- **Oracle.** `ClusterOccluded`, `ClusterPhaseFirst` and `ClusterPhaseSecond` in `ClusterCullMath.h` are the CPU oracle.
  `tests/Renderer/Culling/ClusterGpuParityTests.cpp` runs the real shaders against it and reports SKIPPED without a device.
- **Fallback.** `ClusterDrawChain::TwoPhase(HizPyramid&, sceneDepth)` is decided at `Plan()`. If the pyramid cannot run (view too
  large, a pass toggled off, shader not ready, depth not sampleable, cull camera differs from the draw camera) the chain is the
  single-phase `Frustum` chain with no occlusion. Colour views have 2 lanes (`kMainViewLanes`).
- Depth-only lanes never use the pyramid.
- Debug: `/feature_debug/set?name=GpuDrivenScene&mode=HiZ`; read `game.occlusion` (`Applied`, `Unusable`,
  `NoCullCamera`, `NotRequested`) and `game.hizReason` (`None`, `Unavailable`, `TooLarge`). With `collectStats=true` the `reasons.*`
  keys count items by outcome: `frustum`, `cone`, `pyramid`, `history` (drawn in phase 1) and `kept`. In two-phase mode they are not a
  partition of the tested items: phase 1 counts only `history`, phase 2 counts the final outcome of every item, so a phase-1 item is
  counted twice. A static scene draws nothing in phase 2 after the first frames. `debugView=ClusterPhase` tints phase 1 red, phase 2
  green and a single-phase chain gray.

## Buffer memory

- Each feature owns one `ClusterDrawState`. Its buffers are counted per (state, view scope) in a process-wide ledger. Limits: 256 MB per
  state, 512 MB for the process (`ClusterCullMath.h`). A chain over a limit is refused and its part uses the CPU draw (one warning per pass).
- A scope gives its buffers back after 600 frames without a chain (`TickIdle`). `TickIdle` runs inside the feature's own callback.
- A feature turned off with `set_feature_enabled` gets no callback, so it never ticks. Its buffers stay held until the feature is
  enabled again, goes idle while enabled, or is destroyed. This is by design of the callback model.
- Consequence: after a run with many cluster lanes on (for example the Shadow cascades at 100000 instances), turning them off keeps their
  bytes in the ledger and can refuse a later large `GpuDrivenScene` chain. Re-enable the feature for 600 frames, or restart the editor,
  before a 100000-instance Hi-Z measurement.

## Shared survivors

The secondary view culls with the primary camera, so the primary view's result is its answer too.

- The primary colour chain publishes its finished parts (`ClusterSurvivorTable.h`: buffer handles, lane count, cull pose and flags)
  for the frame. A secondary chain that called `ClusterDrawChain::ReuseSurvivors()` declares only the indirect draws of those parts with
  its own camera: no Reset, no Cull, no pyramid. It reuses them only if the table is of this frame and graph, the cull flags and cull
  pose are equal, and every planned part was published (mesh token and instance-set key). Otherwise it records its own frustum chain
  with the primary cull camera.
- Debug: `sharedSurvivors` per view; `scene.hizSource=primary` when the occlusion result was inherited.

## Culling mode

One setting, `mode`, decides what is culled in both views and on both draw paths. The "Culling" panel (feature
`GpuDrivenScene`) has the combo. The default is `Frustum`.

| Mode | Cluster path (GPU) | CPU path |
|---|---|---|
| `NoCull` | No culling flags. Baseline. | No cull request. |
| `Frustum` | Frustum test (and the cone test when `cone` is on). | Frustum test on padded world bounds. |
| `HiZ` | Frustum plus Hi-Z occlusion. | Same as `Frustum`. Hi-Z cannot test CPU-drawn meshes. |

- **Cull camera.** The cull camera is the active game camera (the first active `Camera`). The primary view culls with its own
  camera (`cullCamera=own`). The secondary view culls with the primary camera (`cullCamera=primary`), never with its own. So
  the secondary view shows exactly what the game camera keeps: fly the secondary camera away and the world is cut at the game
  frustum. The game frustum uses the primary aspect. If the primary view is not rendered, the last known primary aspect is used.
- **No camera.** With no active camera, or a camera whose matrix is not finite, the view uses `cullCamera=none`. Nothing is
  culled and the panel says why.
- **Settings.** `enabled` (GPU cluster path on or off, never changes the CPU cull), `mode`, `cone`, `collectStats`, `debugView`.
  `cone` is only correct for closed meshes. When the cull camera is not the view camera, the cone test uses the cull camera eye,
  so back faces seen from the secondary camera can vanish. `reset_defaults` and the panel button reset everything.
- **Hi-Z owner.** Only the primary view builds a pyramid. The secondary view draws the primary view's survivors (see "Shared
  survivors"), so it inherits the occlusion result: `hizSource=primary`, `occlusion=Applied`. When the primary view ran no pyramid, the
  secondary view reports `hizReason=Unavailable` and acts as `Frustum`. `hizSource` is `own` for the primary view. `occlusion=NoCullCamera`
  means no cull camera was available.
- **Counts.** `tested` is from the current frame, `visible` is two frames old. The feature keeps the tested value of the
  last 4 frames and pairs each visible count with the tested value of its own frame (`testedAtVisibleFrame`).
  `hidden = testedAtVisibleFrame - visible` is computed once, in the feature, and it is empty (`hiddenValid=false`) when
  visible is larger.
- **State.** `/feature_debug/state?name=GpuDrivenScene` gives `enabled`, `mode`, `cone`, `collectStats`, `debugView`, `supported`
  and per view (`game.` primary, `scene.` secondary) the cluster numbers, `occlusion`, `hizReason`, `cameraValid`,
  `cullCamera`, `hizSource`, `sharedSurvivors`, `cpuTested`, `cpuCulled`, `cpuUnbounded`, `cpuCullState`
  (`active`, `off`, `suppressed: frame capture`, `no camera`), the `cpuPath.*` counts and, with `collectStats`, the `reasons.*` counts.
  The panel prints the same numbers.
  `GET /scene_camera/image` returns the secondary view image without a screenshot.

## CPU path culling

Entities that are not on the cluster path are drawn by `RenderSystem::Draw`. It frustum-culls them when it gets a
`SceneCullRequest`.

- **Request flow.** A feature calls `Frame::CullSceneDraw(viewProjection, padding)` in its own always-on Opaque callback
  (`GpuDrivenScene.SceneCull`). `Core::SetSceneDrawCull` stores one request per view scope (the last write of the frame wins).
  The `RenderOpaque` pass reads it at execute time and passes it to `Game::Render`. The callback is not a render pass: it ignores
  `enabled` and the cluster pass toggles. The CPU cull follows `mode` alone.
- **Refused.** `CullSceneDraw` returns false for the shared scope, with no engine context, for an unusable matrix
  (`TryExtractFrustumPlanes` is empty: non-finite entry or degenerate plane) and while the Frame Debugger records the primary view.
  An unusable matrix never culls. It is not a fallback plane.
- **Capture rule.** While the Frame Debugger records the primary view, the primary view does no CPU cull and no cluster claim
  (`SceneCullAllowed` and `ClusterClaimAllowed` share one rule), so replay steps stay exact. The secondary view is not affected.
  The state says `suppressed: frame capture`.
- **Test.** The world box is the local bounds transformed by the world matrix, padded by 5% of its largest extent
  (`IsBoundsOutsideFrustum`). A non-finite world matrix keeps the entity.
- **Counts.** `Draw` fills `SceneCullCounts` over entities that are not claimed by the cluster path and not past `maxDrawCount`:
  `tested` (has bounds, was tested), `culled` (tested and outside), `unbounded` (no local bounds, kept). The counts are one frame
  old (`Frame::LastSceneDrawCullCounts`) and read zero when the view did not run in the last frame.
- **No bounds.** A mesh without local bounds is always kept and is not cluster-synthesizable.
- **Skinned meshes.** A skinned model's parts carry no local bounds (`PartLocalBounds`), because their vertices change at runtime.
  The CPU cull, the shadow caster cull and the cluster synthesis therefore keep them.
- **Convention.** A mesh whose vertices change after upload, or whose vertex shader displaces vertices, must not set local bounds.
- **Scope.** Only the main scene draw gets the request. Project and feature scene draws draw without a cull unless they ask for one.
- **No Hi-Z.** CPU-drawn meshes (primitives such as cubes, alpha-tested, skinned and transparent meshes) get frustum culling only. If no
  mesh is on the cluster path, the panel says so and names the reasons (`cpuPath.*`).

## Cluster Viewer

A floating editor window that shows the clusters of one mesh asset and checks frustum, back-face cone and Hi-Z culling.
The viewer reads **every number and image back from the GPU**. The real `ClusterCull.comp`, one indirect-count draw
and the real pyramid shaders (`HizBuildBase.comp`, `HizBuildTop.comp`) do the work. The viewer has no CPU copy of the cull tests, classifies no cluster on the CPU and
keeps no CPU copy of the mesh after upload. The CPU only adds, subtracts and prints. The GPU code is `ClusterViewRenderer`
(`src/Renderer/`, uses only public `Renderer` entry points and kit types); the window is `Editor/ClusterViewerWindow`.

- **Open.** Inspector "Open Cluster Viewer" on a mesh asset, the banner button after a clone, or `GET /cluster_viewer/open`.
  A mesh without a usable table opens in Normal mode with a banner. Closing the window frees all GPU memory and UI handles.
- **Draw modes.** `Normal` (clay shading), `Clusters` (one hue per cluster, the same hash as `debugView=ClusterId`, not the
  same brightness) and `Cull result` (survivors of the debug camera). Culled clusters are drawn `Hidden` or `Dim`.
  There is no wireframe.
- **Debug camera.** A camera with a frustum gizmo, picked and moved with the mouse (move or rotate) or set by number. It
  uses the engine camera math. The viewer image is always the orbit camera; the debug camera only decides what is culled.
  "Camera preview" shows what the debug camera itself draws: a wrongly culled cluster is a hole in it.
- **Reason split.** The shader runs with the enabled flags added one by one (frustum, cone, Hi-Z) and only the survivor counts
  are read back. A cluster that fails two tests counts under the first, so
  `visible + byFrustum + byCone + byOcclusion == total`. Counts that are not nested show a red banner and no per-reason numbers.
- **Verify (ID pass).** Draws every cluster from the debug camera with a flat id colour and reads the pixels back.
  `falseCull` = not kept by the GPU cull but seen in the image. `conservative` = kept but not seen (only wasted work).
  With the cone flag on, the ID pass uses back-face culling like the engine. A Hi-Z result at another pose or size shows as
  "Hi-Z stale", not as a bug. The result goes stale when the pose, flags, capture, size or mesh changes.
- **Hi-Z capture.** "Capture Hi-Z" draws a depth-only pass of every cluster from the debug camera and builds the pyramid with
  `HizBuildBase.comp` and `HizBuildTop.comp`. It stays on the GPU. The viewer applies occlusion only when the debug camera pose
  equals the capture pose (`CameraPoseEqual`); at another pose the occlusion flag is dropped for that run and the panel says so
  (`hizCut` is true then). The viewer depth holds the whole mesh, the engine depth holds the phase 1 survivors.
- **Limits.** `MaxClusters()` = min(200000, device limits): above it the cluster modes are off. The device needs
  `ClusterPathSupported()`, else a banner and no GPU work. One identity instance: the work-id mapping
  of several instances is not exercised. The cone test is correct for closed meshes only, so open meshes can show false culls.
  Ground truth sees only what is at least one pixel big. Every run blocks the CPU, so runs happen only when the frame key changes
  or on a button. Targets use explicit RGBA8_UNorm, never the swapchain format.
- **HTTP.** `/cluster_viewer/open|state|set|pick|verify|capture_hiz|image`, `POST /clone_clusters`, see
  [networking.md](networking.md). `GET /cluster_viewer/image` returns only the viewer image (`target=view|debug`,
  optional `overlay=true`), never a full screenshot.
- **Scene camera picking.** In the Scene panel every Camera entity shows a body, and a left click within 14 px of an apex or
  6 px of an edge selects that entity. The selected camera shows its frustum in the Scene panel and, read-only, in the viewer.
  A pick never starts while a gizmo is hot. `GET /scene_camera/state|pick|select|image` give the same to HTTP.
