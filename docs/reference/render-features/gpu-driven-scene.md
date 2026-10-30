# GpuDrivenScene feature
> Settings, pass names and debug keys of the culling feature. The editor panel is named "Culling". For anyone tuning or debugging culling.
> Verified against code: 2026-10-30

## TL;DR
- Culls the scene in both views. Clusters are culled on the GPU. Other meshes get a CPU frustum cull.
- One `mode` setting: `NoCull`, `Frustum` (default) or `HiZ`.
- Draws the survivors of big clustered meshes with one indirect call per part.
- Has no own shaders. It uses `ClusterCull.comp`, `HizBuild*.comp` and `MeshCluster*.vert`.
- How the path works: [GPU-driven rendering](../../architecture/gpu-driven-rendering.md).

## Where in the frame
Both registrations are in the `Opaque` stage. Files: `src/Features/GpuDrivenScene/`.

| Registration | Priority | Gate | What it does |
|---|---|---|---|
| `GpuDrivenScene.SceneCull` | -10 | `Always` | Plans the cull camera. Sends the CPU draw cull request. Owns liveness and CPU counts. |
| `GpuDrivenScene.Opaque` | 0 | Any of its pass names | Records the cluster chain. Claims entities. Writes the GPU numbers. |

| Pass name pattern | Toggle effect |
|---|---|
| `GpuDrivenScene.L<lane>.P<slot>.Reset` / `.Cull` / `.Draw` | All three of a part must be on. If one is off, the part is not declared. |
| `GpuDrivenScene.L<lane>.P<slot>.Stats` and its clear and copy passes | Off = no counts. The claim stays. |
| `GpuDrivenScene.Hiz.Base`, `GpuDrivenScene.Hiz.Top` | Off = no pyramid. The chain falls back to `Frustum`. |

- Colour views: lanes `L0` (first round) and `L1` (second round), slots `P0..P11`.
- `SceneCull` is not a pass. It ignores `enabled` and the pass toggles. The CPU cull follows `mode` alone.
- Setup fails if the draw state or the pyramid is invalid.
- Both views run it. Defaults for the secondary view: on.

## Settings
Set them in the panel or with `/feature_debug/set`. A bad value changes nothing and returns a `400`.

| Key | Default | Allowed | Meaning |
|---|---|---|---|
| `enabled` | `true` | `true`, `false` | GPU cluster path on or off. Never changes the CPU cull. |
| `mode` | `Frustum` | `NoCull`, `Frustum`, `HiZ` | What is culled, in both views, on both paths. |
| `cone` | `false` | `true`, `false` | Back-face cone test. Correct for closed meshes only. Never used by depth-only lanes. |
| `collectStats` | `false` | `true`, `false` | Reads visible count and reason counters back. Adds three passes per part slot. |
| `debugView` | `Off` | `Off`, `ClusterId`, `ClusterPhase` | `ClusterId`: one colour per cluster. `ClusterPhase`: round 1 red, round 2 green, single round gray. |

- Action: `reset_defaults` (no arguments) resets all five.

## Debug keys
`GET /feature_debug/state?name=GpuDrivenScene` returns the global keys and the per-view keys. Per-view keys start with `game.` (primary) or `scene.` (secondary). They are read-only.

| Key (after the prefix) | Meaning |
|---|---|
| `rendered`, `frameIndex`, `cameraValid` | Did the view run lately. Is the cull camera usable. |
| `cullCamera` | `own`, `primary` or `none`. |
| `occlusion` | `NotRequested`, `Applied`, `Unusable`, `NoCullCamera`. |
| `hizReason` | `None`, `Unavailable`, `TooLarge`, `NothingInView`. |
| `hizSource` | `own`, `primary` or `none`. `primary` = occlusion inherited. |
| `sharedSurvivors` | The view drew the primary view's survivors. |
| `inertReason` | `none`, `device caps`, `shared scope`, `capture armed`, `toggled off`, `invalid state`. |
| `lastRefusalReason` | Why the chain drew nothing, in words. Empty when fine. |
| `claimedEntities`, `parts`, `skippedParts`, `skippedInstances` | Entities taken from the CPU draw. Parts with a chain. Parts fully outside the frustum. |
| `testedClusters` | Instances x clusters given to the cull. Not the survivors. |
| `visibleClusters`, `visibleFrame`, `visibleAge`, `visibleValid` | Survivors, two frames old. Needs `collectStats=true`. |
| `testedAtVisibleFrame`, `hidden`, `hiddenValid` | Tested at the frame of the visible count. `hidden` = tested - visible. |
| `statsCollected` | Was the readback on. If false, the visible numbers are not valid. |
| `reasons.valid`, `reasons.frustum`, `.cone`, `.pyramid`, `.history`, `.kept` | Items by outcome. Only with `collectStats`. |
| `cpuTested`, `cpuCulled`, `cpuUnbounded`, `cpuCullState` | CPU cull counts (one frame old). State: `active`, `off`, `suppressed: frame capture`, `no camera`. |
| `cpuPath.*` | Why renderables stayed on the CPU path: `skinned`, `noIndexBuffer`, `unsupportedLayout`, `noClusterTable`, `tooFewInstances`, `nonFinite`, `noSlot`. |
| `ledgerBytes` | Chain memory held by this feature for the view. |

Global keys: `enabled`, `mode`, `cone`, `collectStats`, `debugView`, and read-only `supported` (device caps ok).

- Two-phase `reasons.*` are not a partition. Round 1 counts only `history`. Round 2 counts the final outcome of every item.
- The profiler does not carry cluster counts. Read them here.

## Quick check
```cmd
curl.exe "http://127.0.0.1:8080/feature_debug/state?name=GpuDrivenScene"
curl.exe "http://127.0.0.1:8080/feature_debug/set?name=GpuDrivenScene&mode=HiZ&collectStats=true"
curl.exe -o scene.png "http://127.0.0.1:8080/scene_camera/image"
```
- The last call is an image route. It returns the Scene view as PNG. Recipes: [HTTP cheat sheet](../../debugging/http-cheat-sheet.md).
- Compare modes with `draw_call_count` of `RenderOpaque` in `/render_graph`. Compare milliseconds, not FPS. Start the editor with `--no-vsync` for benchmarks.

## Limits
- Hi-Z works on the cluster path of the primary view only. Primitives, alpha-tested, skinned and transparent meshes get frustum culling only.
- The cone test is wrong for open meshes. It can delete visible back faces.
- With `mode=HiZ` the Scene view inherits the primary result. If the primary built no pyramid, it reports `hizReason=Unavailable` and acts as `Frustum`.
- A frame capture turns off claims and the CPU cull for the primary view (`capture armed`).
- A feature turned off with `set_feature_enabled` keeps its chain memory until it runs again. See [GPU-driven rendering](../../architecture/gpu-driven-rendering.md).
- Without an active camera nothing is culled (`cullCamera=none`).

## Related
- [Render features index](README.md) · [Feature Kit reference](../feature-kit.md)
- [GPU-driven rendering](../../architecture/gpu-driven-rendering.md) · [Mesh clusters](../../architecture/mesh-clusters.md)
- [HTTP cheat sheet](../../debugging/http-cheat-sheet.md) · [Debug routes](../../debugging/http-routes-debug.md)
