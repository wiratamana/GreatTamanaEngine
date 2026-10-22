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

Three doors give the same bytes for the same input and options:

1. **Import hook.** `ImportAssetFile()` takes a required `MeshImportOptions`. `Off` never
   clusters. `Always` clusters rigless meshes. `Auto` (default) needs at least
   `autoTriangleThreshold` (100000) triangles, no texture index on any material, and skips
   the tail when the fill is poor. The cheap pre-check stops right after the weld.
   Clustering never fails an import: `clusterMessage` carries the reason.
2. **Inspector.** "Build Clusters" in the mesh asset tools starts the editor's single
   `MeshClusterBuildRunner`. `EditorHost` polls it once per frame.
3. **HTTP.** `POST /build_clusters` (see [networking.md](networking.md)).

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
concatenated index buffer, validates them and uploads one device-local `MeshClusterTable`
buffer into the `Mesh` (`Mesh::HasClusters()`). Textured parts and GPU-skinned models get no
table. A table above `kMaxMeshClusters` is dropped before upload. A failed upload drops the
table. Each case logs once and the mesh draws on the CPU path.

A mesh without a table still joins the cluster path when a frame holds at least
`kMinInstancesForSynthesizedClusters` (4) instances of it. `Core::EnsureClusterTable()` builds
one cluster over the whole index range from the mesh bounds (no cone), uploads it once and keeps
it in the `Mesh`. A failed synthesis backs off 120 frames, doubling, and the mesh draws on the CPU path.

Respawn to apply new data: `InvalidateCachedMeshAsset()` only affects new spawns. The mesh
sweep (`ReleaseUnusedGpuResources`, same throttle as the material sweep) frees catalog-owned
meshes that nothing uses, including meshes held only by the GPU-skinning rig cache.

## Frame flow

1. `Core::BuildFrame` builds the candidate list once with one `CollectRenderables()` call:
   a candidate has clusters (or is synthesizable with enough instances), an index buffer, the
   untextured pipeline, is not GPU-skinned,
   has a finite world matrix, and the device caps are present. Flags (uniform scale,
   positive handedness) and max scale are computed per instance.
2. Render features read `Frame::ClusterCandidates()`. They never walk the ECS.
3. A feature calls `Frame::ClusterDraw(state, lane)` on a view frame (the Opaque stage or a
   depth-only lane) and `Plan()` / `Record()`.
4. `Record()` declares, per part, in this order: **Reset** (`FillBuffer` of the count),
   **Cull** (`ClusterCull.comp`), **Draw** (indirect draw with `MeshCluster.vert`), and an
   optional **Stats** readback (lane 0, colour target only). It stops at the first refusal.
5. Only after Reset, Cull and Draw were all declared does the verb claim the entities
   (`ClusterClaim.h`). `RenderOpaque` skips claimed entities of its own view scope. A depth-only
   chain fills an exclusion set that the CPU `SceneDraw` reads at execute time.

Rules:

- **Claim after proof.** An entity is claimed only when its whole chain was declared. A chain
  that stops part way claims nothing, so the CPU path draws the entity.
- **Capture frames never claim** for the Game view. While the Frame Debugger window is open
  and enabled, the Game view draws every entity on the CPU path, so replay steps stay exact.
- **Per view scope.** Every instance, visible, command and count buffer, parameter block and
  claimed set belongs to one (view scope, lane, part slot). Game and Scene views never share
  one. A shared-scope frame has no camera, so the verb is inert there.
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
| Chain memory per state and scope | 256 MiB (power-of-two capacity) |
| Chain memory, all states and scopes | 512 MiB (`ClusterDrawLedger`) |
| Idle release | a scope with no chain for 600 frames frees its buffers |
| Part slots / lanes | colour views 8 slots x 1 lane, depth-only users 4 lanes x 4 slots |

A part is worth a chain at 2 or more clusters, or 4 or more instances. A part over a limit,
beyond the slots, or over a budget is not claimed, logs once and draws on the CPU path.
Instance counts are never truncated.

## Cull math and cone test

`ClusterCullMath.h` is the CPU oracle for `ClusterCull.comp`. If the two disagree, the oracle
is right. A text test keeps shader constants and block names equal to the C++ values.

The frustum test transforms the cluster AABB to world space (NaN boxes are kept). The cone
test is **off by default** and is valid only together with the back-face-culling pipeline
variant: the CPU path draws both sides, so a cone test would delete visible back faces of an
open surface. The flag and the pipeline key are one decision (`ClusterPipelineKeyFor()`), and a
depth-only lane never uses the cone test. Enable it only for closed meshes.

Survivors are appended with `atomicAdd`, so their draw order changes between frames. Two
coincident triangles can swap winners. `MeshHasCoincidentTriangles()` tells whether a mesh has any.
Both vertex shaders declare `invariant gl_Position` with the same expression order as
`Mesh.vert`.

## Debug workflow (HTTP)

- `GET /feature_debug/state?name=GpuDrivenScene`: per view (`game.` / `scene.` prefix)
  `claimedEntities`, `parts`, `testedClusters` (instances x clusters given to the cull, not the
  survivors), `inertReason` (none, device caps, shared scope, capture armed, toggled off),
  `lastRefusalReason`, `ledgerBytes`.
- **Visible count.** `visibleClusters` (survivors, 2 frames old, with `visibleFrame`) needs
  `collectStats=true`, which adds three passes per part slot. With stats off, or when the readback
  is older than 8 frames, `visibleValid` is false and `statsCollected` says why. Stats are off by default.
- `GET /feature_debug/set?name=GpuDrivenScene&frustum=...&cone=...&freezeCulling=true&collectStats=true&debugView=ClusterId`.
- **Freeze culling** keeps the cull camera of the last live frame while the view camera moves.
  Move the camera, then fetch `GET /get_game_view` to see what was culled. A resize or a
  disabled feature clears it.
- **`debugView=ClusterId`** colours each cluster by a hash of its id (`MeshClusterTint.frag`).
  Culled clusters are not visible by design: combine it with freeze culling.
- `GET /render_graph`: per-pass `gpu_timing_milliseconds`, `draw_call_count`,
  `indirect_draw_count`, plus `indirect_draw` (device caps) and `gpu_memory`
  (`total_bytes`, `mesh_count`, `by_name`). Indirect draws never add to `triangle_count`.
  Read `gpu_memory` before and after a series of cluster rebuilds: totals and `mesh_count` must
  return to base once the mesh sweep ran. `gpu_memory` is rebuilt every 30 frames, so wait about
  one second after a change before reading it.
- `POST /build_clusters` rebuilds a table. Read `GET /get_logs` for the `MeshClustering` lines.
- Pass stats are keyed by pass name, not by view. Measure with only the Game view active.
- **Validation.** Start the exe with `--vulkan-validation` to turn the Vulkan validation layer on in any
  build type (default: on in Debug, off in Release). Read `GET /get_logs?category=Vulkan`. The log sink is
  installed first in `main()`, so init-time validation messages are kept. The state line says
  `validation enabled`, `validation disabled` (Info), `validation requested but layer not available` or
  `validation layer loaded but debug messenger failed` (Warning).
- **Move the Game camera.** `POST /set_entity_trs {"name":"Camera","translation":{"x":..,"y":..,"z":..}}`.
- Every cluster pass and pyramid level has a GPU timing slot in `/render_graph`
  (`RenderGraph::kSynchronousTimingSlotBudget` is the worst-case sum). Capture replay steps get a slot only up
  to `kReplayTimingNames` (64); the rest log once and read as absent.

## Known limits

- Textured and PBR meshes are not clustered at runtime (set 0 is used by the material set).
- The cluster path needs the synchronous offscreen graph: `GpuParams` and the host-written
  instance buffer are rewritten on the CPU at declare time. A pipelined regime makes the verb
  inert (CPU path).
- One part dispatch is capped at 4,194,240 work items (65535 groups of 64).
- A build cannot be cancelled inside one meshoptimizer call; closing the app waits for the step.
- With one job worker or no job system, a build runs inline and stalls the editor.
- The rename of the atomic write fails when another handle holds the target open. It is atomic
  against a crash, not durable against a power cut.
- Hi-Z occlusion uses last frame's depth. It tests only boxes that were fully on screen last frame.
  Boxes with a corner behind the eye, or a rect above level 5 (64 depth texels), are kept.
- Two-phase occlusion (draw last frame's survivors, rebuild the pyramid, test the rest) is not implemented.
  Fast camera motion can pop a cluster in for one frame.

## Hi-Z occlusion

- `fx::HizPyramid` builds one `R32_Float` atlas per view in a PostOpaque callback: level 0 is half the scene
  depth size and levels 1..5 are packed to its right. Each level is one dispatch (`HizBuild.comp`) that reads
  only the scene depth (farthest value of a block) and writes only its own region. Two atlases swap by frame
  parity, so the cull of frame N reads the atlas built in frame N-1.
- `ClusterCull.comp` runs the test after frustum and cone: the box is projected with last frame's
  view-projection, the rect picks a level, four texels give the farthest depth, and the box is culled
  when its nearest depth is farther. `ClusterOccluded` in `ClusterCullMath.h` is the CPU oracle.
- The pyramid is unusable (occlusion off) for the first frame, a resize, a camera cut, a missed build and
  two builds after each of those. The persistent target resizes one frame late, so a resize also holds the
  next build back. `HizValidity` in `ClusterCullMath.h` is the pure state machine.
- A moved instance is never occlusion-culled. Core tracks last frame's world matrices only while a pyramid
  is being built (`Core::RequestOcclusionTracking`); a new entity counts as moved.
- Depth-only lanes never use the pyramid.
- Debug: `/feature_debug/set?name=GpuDrivenScene&occlusion=true`; read `game.occlusionUsable`.
  Combine with `freezeCulling` and `/get_game_view` to see the effect.
