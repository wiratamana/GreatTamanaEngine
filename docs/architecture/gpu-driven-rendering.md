# GPU-driven rendering
> How the engine culls clusters on the GPU and draws the survivors with one indirect call. For anyone who touches the cluster path, culling or Hi-Z.
> Verified against code: 2026-10-30

## TL;DR
- A big mesh is cut into clusters. A compute pass culls them per view. One counted indirect draw renders the survivors.
- The CPU draw path always stays complete. Any refusal means the CPU draws the entity.
- The path is a Feature Kit verb (`ClusterDraw`). The `GpuDrivenScene` feature and the Shadow cascades use it.
- One setting, `mode` (`NoCull`, `Frustum`, `HiZ`), decides culling on both paths and in both views.
- Debug it with `/feature_debug/state?name=GpuDrivenScene`. See the [HTTP cheat sheet](../debugging/http-cheat-sheet.md).

## Diagram
```text
 ECS renderables
      |  Core::BuildFrame: SelectClusterCandidates (once per frame)
      v
 Frame::ClusterCandidates()          Frame::CullSceneDraw(vp, pad)
      |                                      |
      v  GpuDrivenScene.Opaque               v  GpuDrivenScene.SceneCull
 ClusterDrawChain (per view, per part)      SceneCullRequest -> CPU SceneDraw
      |                                      (frustum cull of the rest)
      |-- round 1: Reset -> Cull -> Draw      (last frame's survivors)
      |-- HizPyramid: Base -> Top             (depth of round 1)
      '-- round 2: Reset -> Cull -> Draw      (everything else, with Hi-Z)
      |
      v  claim entities, only after the whole chain is declared
 RenderOpaque skips claimed entities (CPU draws the rest)
```

## How it works

### Eligibility (decided once per frame)
`Core::BuildFrame` calls `SelectClusterCandidates()`. Features read `Frame::ClusterCandidates()` and never walk the ECS.

| Rule | A renderable stays on the CPU path when... | Counter (`cpuPath.*`) |
|---|---|---|
| Device | a device feature is missing (see Limits) | `inertReason = device caps` |
| Layout | the layout is not `PositionNormal`, `PositionNormalUv` or `PositionNormalUvTangent` | `unsupportedLayout` |
| Skinning | the model is GPU-skinned | `skinned` |
| Index buffer | the mesh has none | `noIndexBuffer` |
| Matrix | the world matrix is not finite | `nonFinite` |
| Table | no cluster table, and the mesh is textured or has no bounds | `noClusterTable` |
| Synthesis | an untextured mesh has no table and fewer than 4 instances this frame | `tooFewInstances` |
| Slot | the part found no free slot (12 per colour view, 4 for depth-only) | `noSlot` |

- A mesh without a table gets one cluster over its whole index range (`Core::EnsureClusterTable`). A failed try backs off 120 frames, doubling.
- Alpha-tested slices get no table, so they stay on the CPU path.
- Build and file format of cluster tables: [Mesh clusters](mesh-clusters.md).

### The chain
A part is one (mesh, material) pair. A colour view has 12 part slots and 2 lanes. Depth-only users have 4 slots and 4 lanes.

1. The feature calls `Frame::ClusterDraw(state, lane)`, sets instances, cameras and flags, then `Record()`.
2. A part that is not worth a chain is dropped. Worth = at least 2 clusters or at least 4 instances.
3. A part fully outside the cull frustum gets no pass and no slot. Its entities are still claimed (`skippedParts`).
4. For every other part `Record()` declares, in this order: **Reset** (`FillBuffer`), **Cull** (`ClusterCull.comp`), **Draw** (indirect).
5. With `collectStats` it adds three passes per part slot: stats, stats clear, stats copy.
6. Descriptor sets: 0 = material, 1 = scene services, 2 = cluster buffers. Lit parts reuse `TexturedMesh.frag` and `PbrMesh.frag`.
7. Only after every pass of a part was declared, the verb claims its entities.

### Culling modes
`GpuCullMode` is one setting for both views. Default is `Frustum`. The editor panel is named "Culling".

| Mode | Cluster path (GPU) | CPU path |
|---|---|---|
| `NoCull` | No tests. Baseline for comparison. | No cull request. |
| `Frustum` | Frustum test. Cone test only if `cone` is on. | Frustum test on padded world bounds. |
| `HiZ` | Frustum plus two-round Hi-Z occlusion. | Same as `Frustum`. Hi-Z cannot test CPU-drawn meshes. |

- **Cull camera.** It is the active `Camera` entity. The primary view culls with its own camera (`cullCamera=own`).
- **Secondary view.** It culls with the primary camera (`primary`), never its own. Fly the Scene camera away and you see the world cut at the Game frustum.
- **No camera.** No active camera, or a matrix that is not finite, gives `cullCamera=none`. Nothing is culled.
- **Cone test.** Off by default. It is correct for closed meshes only, because the CPU path draws both sides of a face.
- **Depth-only lanes** never use the cone test or the pyramid.

### Two-round Hi-Z occlusion
Occlusion is exact for the current frame. Only the primary view builds a pyramid.

1. **Round 1** (lane 0, `PhaseFirst`). Draws items that were visible last frame and still pass frustum and cone.
2. **Pyramid.** `fx::HizPyramid` builds one `R32_Float` atlas from the opaque depth. Two dispatches: `GpuDrivenScene.Hiz.Base` (levels 0..4) and `GpuDrivenScene.Hiz.Top` (level 5 and up).
3. **Round 2** (lane 1, `PhaseSecond`). Tests every item against this frame's pyramid. Draws what round 1 did not draw. Writes the new history.

| Detail | Value |
|---|---|
| Pyramid level 0 | Half the scene depth size. Each level keeps the farthest depth of a block. |
| Levels | Down to 1x1. At most 14 (`kHizMaxLevels`). Scene side above 16384 px = no pyramid. |
| Atlas size | Rounded up to a multiple of 256 texels. Frame-local, not kept between frames. |
| History | One `uint32` per work item: bit 0 = visible last frame, bit 1 = drawn in round 1. |
| History key | `ClusterHistoryKey`: mesh token, cluster count, entity ids in order, buffer generation. |
| Depth compare | Perspective: pyramid depth is turned into view distance (`HizDepthMapOf`). Ortho or skewed: raw compare. |
| Safe cases | Clear depth never occludes. A corner behind the eye keeps the box. |

- A wrong or stale history only costs time. Round 1 draws nothing and round 2 draws everything that passes.
- If the pyramid cannot run, the chain falls back to a single `Frustum` chain. Causes: view too large, a pass toggled off, shader not ready, depth not sampleable, cull camera differs from draw camera.
- A round-1 draw that round 2 finds occluded stays drawn for that frame. It costs time only.

### Shared survivors (Scene view)
The secondary view culls with the primary camera, so the primary result is also its answer.

- The primary colour chain publishes its finished parts (`ClusterSurvivorTable.h`): buffers, lane count, cull pose, flags.
- A secondary chain with `ReuseSurvivors()` declares only the indirect draws, with its own camera. No Reset, no Cull, no pyramid.
- It reuses them only if the table is from this frame, flags and pose are equal, and every planned part was published.
- Otherwise it records its own `Frustum` chain with the primary cull camera.

### CPU path culling
Entities not on the cluster path are drawn by `RenderSystem::Draw`. It culls them when it gets a `SceneCullRequest`.

1. `GpuDrivenScene.SceneCull` (Opaque, priority -10, always on) calls `Frame::CullSceneDraw(viewProjection, 0.05)`.
2. `Core::SetSceneDrawCull` keeps one request per view scope. The last write of the frame wins.
3. `RenderOpaque` reads it at execute time. The test uses the world box of the local bounds, padded 5% of its largest extent.

- A mesh without local bounds is always kept. Skinned parts carry no bounds, so they are always kept too.
- A mesh whose vertices change after upload must not set local bounds.
- `CullSceneDraw` returns false for the shared scope, for an unusable matrix, and while the Frame Debugger records the primary view.
- Counts (`cpuTested`, `cpuCulled`, `cpuUnbounded`) are one frame old. They read 0 when the view did not run.
- Only the main scene draw gets the request. Project and feature scene draws are not culled unless they ask.

### Cluster Viewer
A floating editor window that shows the clusters of one mesh asset. It checks frustum, cone and Hi-Z culling with the real shaders.

- It reads every number and image back from the GPU. It has no CPU copy of the cull tests.
- Draw modes: `Normal`, `Clusters` (one hue per cluster id), `Cull result`. Culled clusters are `Hidden` or `Dim`.
- A debug camera with a frustum gizmo decides what is culled. The viewer image always comes from the orbit camera.
- Reason split: `visible + byFrustum + byCone + byOcclusion == total`. A failed nesting check shows a red banner.
- **Verify** draws every cluster with a flat id colour and reads pixels back. `falseCull` = culled but seen. `conservative` = kept but not seen.
- **Capture Hi-Z** builds a pyramid from the debug camera. Occlusion is applied only when the debug camera pose equals the capture pose.
- Limits: `MaxClusters()` = min(200000, device limits). One identity instance only. Every run blocks the CPU.
- Open it from the Inspector or with `GET /cluster_viewer/open?path=...`. Routes: [HTTP routes](../debugging/http-routes.md).

## Limits and budgets
Constants live in `src/Renderer/Culling/ClusterCullMath.h` and `ClusterPassCounts.h`. All math is 64-bit.

| Limit | Value |
|---|---|
| Device caps needed | `drawIndirectCount`, `multiDrawIndirect`, `drawIndirectFirstInstance`, max count above 0 |
| Work items per part (instances x clusters) | min(65535 x 64, device max indirect count, 128 MiB / 20, 128 MiB / 8) |
| Instances per part | 128 MiB / 80 bytes |
| Clusters per table | 128 MiB / 64 bytes (`kMaxMeshClusters`) |
| One storage binding | 128 MiB |
| Chain memory per state and scope | 256 MiB (power-of-two buffer sizes) |
| Chain memory, whole process | 512 MiB (`ClusterDrawLedger`) |
| Idle release | A scope with no chain for 600 frames frees its buffers |
| Slots and lanes | Colour view: 12 slots x 2 lanes. Depth-only: 4 slots x 4 lanes |
| Pipelines | 18 (`kClusterPipelineKeyCount`) = 3 variants x 3 material kinds x 2 cull modes |

- A part over a limit, beyond the slots, or over a budget is not claimed. It logs once and draws on the CPU path.
- Instance counts are never cut.

## Key files
| File | Role |
|---|---|
| `src/Core/ClusterCandidates.*` | `SelectClusterCandidates`: eligibility, reject counters |
| `src/Core/ClusterClaim.*` | Claim rules: which entities the chain may take from the CPU draw |
| `src/Core/FeatureKit/ClusterDrawVerb.cpp` | `Plan()` / `Record()`: parts, slots, passes, claims |
| `src/Core/FeatureKit/ClusterDrawState.*` | Per-feature buffers, ledger reservation, idle release |
| `src/Core/FeatureKit/HizPyramid.*` | Pyramid atlas and its two build passes |
| `src/Renderer/Culling/ClusterCullMath.h` | GPU record layouts, limits, CPU oracle of the cull shader |
| `src/Renderer/Culling/ClusterCullConstants.h` | Constants shared with GLSL (generated into `ClusterCullConstants.glsl`) |
| `src/Shaders/ClusterCull.comp`, `HizBuild*.comp`, `MeshCluster*.vert` | Cull, pyramid, cluster vertex shaders |
| `src/Features/GpuDrivenScene/` | The feature, settings, debug endpoint, "Culling" panel |
| `src/Renderer/ClusterViewRenderer*.cpp`, `src/Editor/ClusterViewer*` | Cluster Viewer |

## Rules
1. **Claim after proof.** An entity is claimed only when its whole chain was declared. A chain that stops part way claims nothing.
2. **One path per entity.** An entity is drawn by exactly one path per view and frame.
3. **Capture frames never claim or cull.** While the Frame Debugger records the primary view, it draws everything on the CPU path.
4. **All-or-none toggles.** If Reset, Cull or Draw of a part is toggled off, none of them is declared.
5. **Per-view buffers.** Every buffer, parameter block and history is keyed by view scope, lane and part slot. Do not add a cache without a view dimension.
6. **Real fill for Reset.** Clear the visible count with a transfer fill before Cull. A shader cannot reset it safely.
7. **Flat command buffer.** Declare indirect commands as `uint values[]` (5 per command, 20 bytes). A struct array breaks the stride under `std430`.
8. **Resolve twice.** Resolve mesh, pipeline and set in `Plan()`. Look the mesh up again at execute time and null-check it.
9. **Stats never revoke.** A stats refusal never removes a claim.
10. **The oracle wins.** `ClusterCullMath.h` is the CPU truth for `ClusterCull.comp`. A GPU parity test runs the real shaders against it.
11. **No second batching path.** Do not add a parallel batching mechanism. Everything else uses `Renderer::Submit()`.

## Gotchas
- **Draw order changes.** Survivors are appended with `atomicAdd`. Two coincident triangles can swap winners (`MeshHasCoincidentTriangles`).
- **Synchronous graph only.** `GpuParams` and the instance buffer are written on the CPU at declare time. A pipelined regime makes the verb inert.
- **Turned-off features keep memory.** A feature disabled with `set_feature_enabled` gets no callback, so `TickIdle` never runs. Its buffers stay in the ledger.
- **Ledger can refuse a big chain.** After many cluster lanes (for example Shadow at 100000 instances), re-enable the feature for 600 frames or restart before a big Hi-Z test.
- **Hi-Z scope.** Primary view only. Primitives, alpha-tested, skinned and transparent meshes get frustum culling only.
- **Box test only.** Hi-Z tests axis-aligned cluster boxes.
- **No cancel inside a build.** A cluster build cannot stop inside one meshoptimizer call. With one job worker it runs inline and stalls the editor.
- **Hi-Z reasons.** `hizReason` is `None`, `Unavailable`, `TooLarge` or `NothingInView`. `NothingInView` means every part was outside the frustum.
- **`inertReason` values.** `none`, `device caps`, `shared scope`, `capture armed`, `toggled off`, `invalid state`.
- **Cone test and the secondary camera.** With a cull camera different from the view camera, the cone test uses the cull eye. Back faces seen from the Scene camera can vanish.
- **Open items.** LOD selection, bindless batching, alpha-tested meshes and GPU-skinned meshes on the cluster path, and async compute are not done.

## Related
- [Mesh clusters](mesh-clusters.md) · [Rendering](rendering.md) · [Render graph](render-graph.md)
- [GpuDrivenScene feature page](../reference/render-features/gpu-driven-scene.md) · [Feature Kit reference](../reference/feature-kit.md)
- [HTTP cheat sheet](../debugging/http-cheat-sheet.md) · [Debug routes](../debugging/http-routes-debug.md) · [Frame Debugger](../debugging/frame-debugger.md) · [Profiling](../debugging/profiling.md)
- Benchmarks: `tools/culling_bench/`
