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
   `autoTriangleThreshold` (100000) triangles, no material that counts as textured, and skips
   the tail when the fill is poor. A material counts as textured when it has any texture index or
   uses PBR shading (`MaterialDrawsTextured()`): the runtime draws every PBR slice on the textured
   path, which never reads clusters. So `Auto` skips glTF meshes; use `Always` for them. `Always`
   ignores this rule. The cheap pre-check stops right after the weld.
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
- **Capture frames never claim or cull** for the primary view. While the Frame Debugger window is open
  and enabled, the primary view draws every entity on the CPU path with no cull, so replay steps stay exact.
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
is right. A text test keeps shader constants and block names equal to the C++ values. The Cluster Viewer does not use
the CPU copy of the tests; it reads the GPU result instead.

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

- Textured and PBR meshes are not clustered at runtime (set 0 is used by the material set).
- The cluster path needs the synchronous offscreen graph: `GpuParams` and the host-written
  instance buffer are rewritten on the CPU at declare time. A pipelined regime makes the verb
  inert (CPU path).
- One part dispatch is capped at 4,194,240 work items (65535 groups of 64).
- A build cannot be cancelled inside one meshoptimizer call; closing the app waits for the step.
- With one job worker or no job system, a build runs inline and stalls the editor.
- The rename of the atomic write fails when another handle holds the target open. It is atomic
  against a crash, not durable against a power cut.
- Hi-Z occlusion exists only on the cluster path. Cubes and other primitives, textured and PBR meshes and skinned models are
  CPU-drawn and get frustum culling only.
- Hi-Z occlusion uses last frame's depth. It tests only boxes that were fully on screen last frame. Boxes with a corner
  behind the eye are kept. A box whose rect is wider than 32 level-0 texels (about 64 scene pixels) is kept, so only small
  occludees are removed. A large occluder works fine.
- A moved instance is never occlusion-culled, and the pyramid needs two clean builds after a start, resize or camera cut.
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
- Debug: `/feature_debug/set?name=GpuDrivenScene&mode=HiZ`; read `game.occlusion` (`Applied`, `Unusable`,
  `NoCullCamera`, `NotRequested`) and `game.hizReason`. The secondary view never uses a pyramid (see "Culling mode").
  To see what the game camera keeps, look at the secondary view with `mode=Frustum` or `mode=HiZ`.

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
- **Hi-Z owner.** Only the primary view builds a pyramid. In `HiZ` mode the secondary view reports `occlusion=Unusable`,
  `hizReason=Unavailable` ("shared pyramid not available") and acts as `Frustum`. `hizSource` is `own` for the primary view
  and `none` for the secondary view. `occlusion=NoCullCamera` means no cull camera was available.
- **Counts.** `tested` is from the current frame, `visible` is two frames old. The feature keeps the tested value of the
  last 4 frames and pairs each visible count with the tested value of its own frame (`testedAtVisibleFrame`).
  `hidden = testedAtVisibleFrame - visible` is computed once, in the feature, and it is empty (`hiddenValid=false`) when
  visible is larger.
- **State.** `/feature_debug/state?name=GpuDrivenScene` gives `enabled`, `mode`, `cone`, `collectStats`, `debugView`, `supported`
  and per view (`game.` primary, `scene.` secondary) the cluster numbers, `occlusion`, `hizReason`, `cameraValid`,
  `hizTrackingRequested`, `cullCamera`, `hizSource`, `cpuTested`, `cpuCulled`, `cpuUnbounded` and `cpuCullState`
  (`active`, `off`, `suppressed: frame capture`, `no camera`). The panel prints the same numbers.
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
- **Hi-Z.** CPU-drawn meshes (primitives such as cubes, textured and PBR meshes) get frustum culling only. If no mesh is on the
  cluster path, the panel says so.

## Cluster Viewer

A floating editor window that shows the clusters of one mesh asset and checks frustum, back-face cone and Hi-Z culling.
The viewer reads **every number and image back from the GPU**. The real `ClusterCull.comp`, one indirect-count draw
and the real `HizBuild.comp` do the work. The viewer has no CPU copy of the cull tests, classifies no cluster on the CPU and
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
  `HizBuild.comp`. It stays on the GPU. Without a capture, or after a camera cut (`HizCameraCut`), the occlusion flag is
  dropped for that run and the panel says so. One capture counts as valid; the engine needs two clean builds. The viewer
  depth holds the whole mesh, the engine depth holds last frame's survivors.
- **Limits.** `MaxClusters()` = min(200000, device limits): above it the cluster modes are off. The device needs
  `ClusterPathSupported()`, else a banner and no GPU work. One identity instance: the moved-instance rule and the work-id mapping
  of several instances are not exercised. The cone test is correct for closed meshes only, so open meshes can show false culls.
  Ground truth sees only what is at least one pixel big. Every run blocks the CPU, so runs happen only when the frame key changes
  or on a button. Targets use explicit RGBA8_UNorm, never the swapchain format.
- **HTTP.** `/cluster_viewer/open|state|set|pick|verify|capture_hiz|image`, `POST /clone_clusters`, see
  [networking.md](networking.md). `GET /cluster_viewer/image` returns only the viewer image (`target=view|debug`,
  optional `overlay=true`), never a full screenshot.
- **Scene camera picking.** In the Scene panel every Camera entity shows a body, and a left click within 14 px of an apex or
  6 px of an edge selects that entity. The selected camera shows its frustum in the Scene panel and, read-only, in the viewer.
  A pick never starts while a gizmo is hot. `GET /scene_camera/state|pick|select|image` give the same to HTTP.
