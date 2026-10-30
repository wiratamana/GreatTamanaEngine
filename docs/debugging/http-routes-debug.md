# HTTP routes: debug tools
> Route reference for profiler, fault injection, feature debug, frame debugger, render graph control and project assembly. For anyone who calls the editor's HTTP server.
> Verified against code: 2026-10-31

## TL;DR
- Server: `127.0.0.1:8080`, editor only. All JSON examples below are built from code, not from a live run.
- Almost every route is `GET`. Exceptions: `/profiler/set`, `/profiler/export`, `/project_assembly/*` writes.
- `503` = another request holds the bridge. `504` = main thread did not answer in time. The change may still apply.
- Profiler numbers are metric objects. Read `status` before `value`. `null` is never `0`.
- Capture, logs, scene, assets and camera routes: see [HTTP routes](http-routes.md).
- Start with the [HTTP cheat sheet](http-cheat-sheet.md) if you only need a quick recipe.

## Common rules
| Rule | Detail |
|---|---|
| Error body | `{"success":false,"error":"<text>"}`. Feature debug adds `"key"`. |
| Parameters | Query string. `POST` routes here also read the query. |
| Booleans | Exactly `true` or `false`. Exceptions: `/profiler/set` and feature debug values also accept `1` and `0`. |
| Enum values | Exact case. |
| Bridge | One pending request per bridge. A second one gets `503`. |
| Main thread | Drains one command per bridge per frame. A stalled editor gives `504`. |
| Default wait | `3000` ms. Asset import and cluster build: `120000` ms. Hot reload: `600000` ms. Load project: `5000` ms. |
| After `504` | The change may still apply later. Read the state before you repeat a set. |
| Restart | All toggles and settings are in memory. A restart resets them. |

Routes that need no bridge answer even when the main thread is blocked:
- `/debug/fault`
- all `/profiler*` reads
- `/project_assembly/hot_reload/status`
- `/project_assembly/debug/ledger`, `loaded_assemblies`, `component_types`
- `/project_assembly/list_projects`

Details on threads and bridges: [HTTP server](../architecture/http-server.md).

### Browser guard
Only four calls are guarded: `POST /exit`, `POST /profiler/set`, `POST /profiler/export` and `GET /debug/fault` when arming a fault.

| Refused when | Result |
|---|---|
| `Sec-Fetch-Site` is present and not `none` or `same-origin` | `403` |
| `Origin` host is not `127.0.0.1` or `localhost` | `403` |
| `Host` is not exactly `127.0.0.1:<port>` or `localhost:<port>` | `403` |

`curl.exe` and scripts send none of these headers, so they pass. A proxy or `-H "Host: name"` triggers `403`.

## Profiler
Source: `src/Network/NetworkProfilerRoutes.cpp`. If the build has `GTE_ENABLE_PROFILER` off, every `/profiler*` route returns `200` with `"state":"compiled_out"`.

### Reading a metric
Every number is an object: `{"value","unit","kind","status","age_frames","reason"}`.

| Field | Values |
|---|---|
| `status` | `Present`, `Stale`, `Partial`, `Absent`, `Unsupported`, `Error`, `Inactive` |
| `value` | A number only for `Present`, `Partial`, `Stale`. Otherwise `null`. |
| `unit` | `ms`, `count`, `bytes`, `percent` |
| `kind` | `measured`, `derived`, `estimated` |
| `reason` | `none`, `capture_off`, `no_target`, `folded`, `not_ready`, `device_unsupported`, `compiled_out`, `dropped`, `other` |

Plain numbers (not metrics): `schema_version`, `frame_index`, `thread_slot`, `depth`.

### Snapshot rules
- A read waits up to 500 ms for a snapshot.
- The main thread publishes only when a reader asked in the last 2 s, when a spike froze, and every 30 frames.
- The first call after a quiet period can be old. Call twice.
- No snapshot yet: `200` with `"state":"no_snapshot"` and `live_block`. Retry. It is not an error.

### Routes
| Route | Params | Reply and use |
|---|---|---|
| `GET /profiler` | none | Overview. Start here for "why is the frame slow". |
| `GET /profiler/frames` | `count` whole number, default 30, clamped to 1..120 | Newest frames, oldest first. Each row: `frame_index`, `cpu`, `cpu_busy` (CPU frame ms minus Blocked ms), `gpu_span`, `gpu_primary`, `gpu_secondary`. Use for A/B stats. |
| `GET /profiler/spikes` | none | Frozen slow frames, newest first. |
| `GET /profiler/spike` | `index` = `frame_index` of a frozen spike (required) | One frozen frame with its scope tree. `400` bad index. `404` never frozen or evicted. |
| `GET /profiler/scope` | `name` = exact scope name, 1..256 bytes (required) | Newest frame only. Rows per depth: `{depth,calls,total,self}`. `404` scope did not run. No history: poll and take the median. |
| `POST /profiler/set` | see below | Change capture, pause, pins, spike threshold. Browser guarded. |
| `GET /profiler/timeline_image` | `frame` (spike index, default newest), `width` 200..2400 (default 1200) | PNG. See [Images](#timeline-image). |
| `GET /profiler/graph_image` | `width` 200..2400, `height` 100..800, `frames` 30..120 | PNG of the Frame, CPU busy and GPU lines. See [Graph image](#graph-image). |
| `GET /profiler/table` | `view` tree or flat, `lane` all, main, workers or gpu, `limit` 1..256 | Calls and ms per name with plain-English text. See [Table](#table). |
| `POST /profiler/export` | `frames` 1..120, default 30 | One JSON: `frames`, `overview`, `pinned_spikes[]`. Writes no file. `503` before the first snapshot. Browser guarded. |

`/profiler/scope` and `/profiler/timeline_image` without `frame` also make the main thread keep publishing.

### `POST /profiler/set`
Params come from the query or from a JSON body. The query wins. At least one is required.

| Param | Type | Meaning |
|---|---|---|
| `capture` | bool | Turn profiler capture on or off. Off = all metrics `Absent`, `capture_off`. |
| `display_pause` | bool | Freeze what the panel shows. |
| `pin` | frame index | Keep a spike. Max 8 pinned. |
| `unpin` | frame index | Release a pinned spike. |
| `spike_threshold_ms` | number 0..10000 | Freeze frames slower than this. `0` = median rule. |

Order: unpin, pin, then the switches. A refused pin changes nothing.

| Status | Cause |
|---|---|
| `400` | nothing given, or a bad value |
| `404` | unknown frame |
| `409` | the 9th pin |

```text
curl.exe -X POST "http://127.0.0.1:8080/profiler/set?pin=1234&spike_threshold_ms=20"
```

### `GET /profiler` keys
| Key | Meaning |
|---|---|
| `state` | `ok`, `no_snapshot`, `compiled_out` |
| `has_frame`, `frame_index`, `snapshot_age` | Is the data real, and how old (frames) |
| `capture`, `display_paused` | Runtime switches |
| `cpu_frame`, `untracked` | CPU frame ms. Ms outside every scope. |
| `blocked{gpu,workers,other}` | CPU wait time by class |
| `gpu.views{shared,primary,secondary}`, `gpu.span` | GPU ms per view and for the whole span. `primary` = Game view, `secondary` = Scene view. |
| `gpu.indirect_draws` | Indirect draw counts per view. Never triangles. |
| `gpu.coverage{declared,executed,recorded,disabled_by_toggle}` | Pass accounting. `recorded < executed` = folded passes. GPU numbers become `Partial`, `reason:folded`. |
| `memory.total` | Total tracked bytes. There is no `/memory` route. |
| `scopes[]` | Top-level scopes, largest first, max 32. `scope_rows_dropped` shows overflow. |
| `nested[]` | Depth 1+ scopes of the newest frame, max 64. `nested_rows_dropped` shows overflow. |
| `checks[]` | Self-checks: `{id,status,count,text}`. `status` is `ok`, `warn` or `fail`. |
| `drops{...}` | Lost data counters: `late_scopes`, `foreign_scopes`, `lost_worker_events`, `dropped_scopes`, `dropped_gpu_frames`. |
| `device_state{...}` | `device_lost`, `present_policy`, `consecutive_frame_timeouts`, `abandoned_objects`. |
| `live_block[]` | Waits in progress now: `{thread_slot,site,detail,elapsed}`. Best field for a frozen editor. |
| `verdict{...}` | Medians over the newest 30 frames (minimum 3). |
| `overhead{...}` | Profiler's own cost (estimated). `warn` is true above 3% of the frame. |
| `timeline{aligned,reason}` | `aligned:false` = no calibrated GPU timestamps. |

Check ids: `capture_on`, `scopes_dropped`, `late_scopes`, `foreign_scopes`, `worker_events`, `reconciliation`, `scope_nesting`. If a check fails, do not trust the numbers.

`present_policy` is `capped` (vsync) or `uncapped` (editor started with `--no-vsync`). Measure only when `uncapped`.

### Verdict kinds
Rules run in this order. The first match wins.

| `kind` | Rule | Next step |
|---|---|---|
| `unknown` | No data, capture off, or fewer than 3 frames | Turn capture on. Wait. |
| `unreliable` | Untracked time above 10%, or a self-check fails | Read `checks[]`. Ignore other numbers. |
| `stalled` | Other waits above 20% (not GPU, not present) | Read `stall_site` and `live_block`. |
| `present_limited` | Present waits at 30% or more | Vsync or compositor. Restart with `--no-vsync`. |
| `gpu_limited` | GPU waits at 40% or more | Read `gpu_timing_milliseconds` per pass in `/render_graph`. |
| `cpu_limited` | CPU work at 60% or more | Read `scopes`, `nested`, `/profiler/spikes`. |
| `serial` | Anything else. CPU and GPU run one after the other. | Look at both. |

### Spikes
- A frame freezes when it is above 2x the median of recent frames (needs 8 frames of history). Or when it is above `spike_threshold_ms`, if set.
- The store has 16 slots. The oldest unpinned spike is evicted. Max 8 pinned.
- The GPU span of the present submit attaches up to 2 frames later (`gpu_attached`).
- Row fields: `frame_index`, `cpu`, `median`, `excess`, `dominant_self`, `gpu_span`, `classification`, `cause`, `marker_present`, `pinned`, `gpu_attached`.
- `classification` is `explained` or `unexplained`. Explained = a marker sits on the frame, or one scope covers at least 50% of the excess.
- Markers exist only for periodic work: memory summary rebuild, release sweeps, cluster table synthesis, swapchain recreation, host captures, hot reload, code unload.
- `/profiler/spike` scope rows: `{name,kind,depth,start,duration,self}` in time order. `kind` is `work`, `blocked` or `marker`. `start` is ms from frame start.

### Timeline image
- Drawn on the CPU. It never reads GPU pixels. It has no text.
- Lanes, top to bottom: CPU main thread (one row per depth), waits, worker jobs, GPU per view.
- The GPU lane is not time-aligned to the CPU lanes. It starts at the left edge.
- `400` bad `width` or `frame`. `404` frame gone. `503` no data yet.
- View it with `gte_send_request`, not `http_request`. `curl.exe -o timeline.png` works for humans.

```text
curl.exe -o timeline.png "http://127.0.0.1:8080/profiler/timeline_image?frame=1234&width=1600"
```

### Graph image
- Drawn on the CPU. No text. Three lines: Frame time (grey), CPU busy (blue), GPU span (orange). Guide lines at 16.7 ms and 33.3 ms when they are below the top of the axis.
- A frame without a `Present` value is a gap. Nothing is drawn for it. Rows ticked in the panel are not drawn here.
- Params: `width` 200..2400 (default 1200), `height` 100..800 (default 300), `frames` 30..120 (default 120, newest frames).
- `400` for a bad value (not a whole number, or outside its limits). `503` when there is no snapshot or no frame yet.
- View it with `gte_send_request`, not `http_request`.

```text
curl.exe -o graph.png "http://127.0.0.1:8080/profiler/graph_image?width=1200&height=300&frames=120"
```

### Table
Source of the data: the same totals the Profiler panel shows. Calls and times come from per-frame totals, so a hot scope called thousands of times still has the right `calls`.

| Param | Values | Default |
|---|---|---|
| `view` | `tree` (merged call tree) or `flat` (one row per name) | `tree` |
| `lane` | `all`, `main`, `workers`, `gpu` | `all` |
| `limit` | 1..256 (a number above 256 is clamped to 256) | 128 |

`400` for a bad `view`, `lane` or a `limit` that is not a number. No snapshot: `200` with `"state":"no_snapshot"`.

Body:
```text
{ "schema_version":1, "frame_index":N, "state":"ok", "view":"tree",
  "rows":[ { "lane":"main", "name":"...", "title":"...", "explanation":"...", "hint":"...",
             "category":"Waiting", "origin":"engine", "source":"File.cpp:123", "feature":"",
             "kind":"work", "waiting":false, "has_children":true, "depth":2,
             "calls":{...}, "total":{...}, "self":{...}, "max":{...}, "percent_of_frame":{...} } ],
  "rows_dropped":{...}, "tree_truncated":false }
```
- Rows are in pre-order. The parent of a row is the nearest earlier row of the same lane with `depth - 1`.
- `lane` is `main`, `workers` or `gpu`. `origin` is `engine`, `project` or `unknown`. `feature` is set for GPU rows.
- `title`, `explanation`, `hint` and `category` are plain English from the catalog. `waiting` is true for pure waits.
- `calls`, `total`, `self`, `max`, `percent_of_frame` and `rows_dropped` are metric objects. `depth` is a plain number.
- A GPU row that was not measured has `total`, `self` and `max` with `value:null` and the real `status`. `percent_of_frame` is `null` when its base is unknown.
- `tree_truncated:true` means the event buffer was full. The tree is incomplete. The `flat` view is still exact.
- `rows_dropped` counts rows beyond the 256 row limit of the snapshot plus rows cut by `limit`.

```text
curl.exe "http://127.0.0.1:8080/profiler/table?view=flat&lane=main&limit=40"
curl.exe "http://127.0.0.1:8080/profiler/scope?name=profile_probe::BuildTintPass"
```

### Profiler recipes
| Goal | Calls |
|---|---|
| Find a hitch | `/profiler` (verdict, checks, `live_block`) → `/profiler/spikes` (pick `unexplained` or biggest `excess`) → `POST /profiler/set?pin=N` → `/profiler/spike?index=N` (largest `self`, `kind:"blocked"` = waiting) → `/profiler/timeline_image?frame=N` |
| Editor looks frozen | `/profiler`. Read `live_block[].site` and `snapshot_age`. `/debug/fault` also proves the server is alive. |
| One scope over time | Poll `/profiler/scope?name=Render.Prepare`. Take the median. |
| One-shot report | `POST /profiler/export?frames=120` |
| A/B compare a setting | See below. |

A/B compare (the same steps as `tools/profiler_ab/run_ab.ps1`):
1. Start the editor with `--no-vsync`. Check `present_policy == "uncapped"`. Fix camera and window size.
2. Set the option: `/feature_debug/set?name=GpuDrivenScene&collectStats=false`.
3. Warm up: poll `/profiler` until `frame_index` grows by 120.
4. Read `/profiler/frames?count=120` repeatedly. Dedupe by `frame_index`. Collect 300 frames. Keep only `Present` metrics.
5. Report median and p95. Do not report average or FPS.
6. Restore the setting.

Known limits: no route turns GPU stamps off. The GPU lane is not aligned to CPU.

## Fault injection
`GET /debug/fault` arms a one-shot failure. It proves recovery paths on a live editor. It needs no bridge. It exists only when the build has `GTE_ENABLE_NETWORK` on (default).

| Call | Result |
|---|---|
| `GET /debug/fault` | `{"success":true,"armed":{"offscreen_throw":0,"present_throw":0,"device_lost":0,"main_thread_stall":0}}`. Values are remaining counts. |
| `GET /debug/fault?kind=<kind>&count=N` | `{"success":true,"armed":"<kind>","count":N}`. `count` is 1..1000, default 1. Arming replaces the count. |
| Bad `kind` or `count` | `400`. The message lists valid values. |
| From a browser | `403` (arming only). |

| `kind` | Effect when it fires | Use |
|---|---|---|
| `offscreen_throw` | Throws `std::runtime_error` inside Game or Scene view rendering | The frame must survive |
| `present_throw` | Throws after the swapchain image is acquired | The image must be handed back |
| `device_lost` | Marks the GPU device lost. Final. The engine stops. | Restart the editor after the test |
| `main_thread_stall` | Main thread sleeps 3 s at frame start, once per count | Every bridge route gives `504`. Profiler keeps answering. |

```text
curl.exe "http://127.0.0.1:8080/debug/fault?kind=main_thread_stall&count=2"
curl.exe "http://127.0.0.1:8080/profiler"
```
Expect `verdict.kind` = `stalled` and a `live_block` entry. `/feature_debug/list` should return `504`.

## Feature debug
Read and write settings of one render feature at run time. Four routes, all `GET` (even `set` and `invoke`). They run on the main thread before the frame is built. A change shows in the next frame.

| Route | Query | Reply |
|---|---|---|
| `/feature_debug/list` | none | `{"features":["Atmosphere","GpuDrivenScene",...]}`. Only features that have an endpoint. |
| `/feature_debug/state` | `name` (required). Any other key gives `400`. | `{"feature":"X","fields":{...}}` |
| `/feature_debug/set` | `name` plus `key=value` pairs | The new state, same shape. All or nothing. |
| `/feature_debug/invoke` | `name`, `action`, extra keys as args | State after the action |

```text
curl.exe "http://127.0.0.1:8080/feature_debug/set?name=Shadow&debugView=CascadeColors"
curl.exe "http://127.0.0.1:8080/get_game_view"
```

### Limits and errors
| Rule | Detail |
|---|---|
| Max args | 64. Key up to 64 bytes. Value up to 256 bytes. |
| Duplicate key | `400` |
| Empty `name` | `400 missing required query parameter: name` |
| Bool value | `true`, `false`, `1`, `0` |
| Unsigned int | Digits only. No `+` or `-`. |
| Float | Finite. No `+` prefix. |
| Wrong type | `value does not fit this key`, or `<key> must be <allowed>` for enums |
| Unknown or read-only key | `400` with `"key"` in the body |
| Unknown feature | `404 no loaded feature named 'X' has a debug endpoint` |
| Bridge missing or busy | `503` |
| Timeout | `504`. The change may still apply. |

- Values are clamped by the feature after `set`. They are not rejected. Always read the returned `fields`.
- Keys that start with `game.` or `scene.`, and `supported`, are read-only.
- `game.` = Game view (primary). `scene.` = Scene view (secondary).
- Non-finite numbers are written as `null`.
- Every feature except Atmosphere supports `action=reset_defaults` (no args). Atmosphere has no actions.

### Writable keys
Ranges and defaults live in each feature page. Read the returned state to see what was applied.

| `name=` | Writable keys | Feature page |
|---|---|---|
| `GpuDrivenScene` | `enabled`, `mode` (`NoCull`, `Frustum`, `HiZ`), `cone`, `collectStats`, `debugView` (`Off`, `ClusterId`, `ClusterPhase`) | [GPU driven scene](../reference/render-features/gpu-driven-scene.md) |
| `Shadow` | `cascadeCount`, `maxMapResolution`, `maxDistance`, `splitLambda`, `cascadeBlendFraction`, `distanceFadeFraction`, `casterRange`, `cullCasters`, `casterCullPaddingFraction`, `constBiasTexels`, `slopeBiasTexels`, `normalOffsetTexels`, `filterRadiusTexels`, `filterSamples`, `strength`, `applyInMaterials`, `stabilizeCascades`, `useSdsm`, `sdsmPaddingFraction`, `sdsmShrinkSeconds`, `sdsmQuantizeSteps`, `debugView` (`Off`, `CascadeColors`, `Mask`) | [Shadow](../reference/render-features/shadow.md) |
| `Gtao` | `radius`, `falloffFraction`, `sliceCount`, `stepCount`, `sampleDistributionPower`, `thinOccluderCompensation`, `finalPower`, `minVisibility`, `strength`, `maxDistance`, `distanceFadeFraction`, `resolutionDivisor`, `spatialDenoise`, `temporal`, `historyWeight`, `depthSigma`, `debugView` (`Off`, `AoOnly`) | [GTAO](../reference/render-features/gtao.md) |
| `Ssgi` | `intensity`, `maxRayDistance`, `thickness`, `depthBias`, `rayCount`, `stepCount`, `stepDistributionPower`, `endFadeFraction`, `maxRadiance`, `albedoLuma`, `albedoSaturation`, `maxDistance`, `distanceFadeFraction`, `resolutionDivisor`, `spatialDenoise`, `temporal`, `historyWeight`, `depthSigma`, `debugView` (`Off`, `GiOnly`, `Normals`) | [SSGI](../reference/render-features/ssgi.md) |
| `VolumetricLight` | `enabled`, `density`, `albedo`, `anisotropyG`, `heightDensity`, `heightBase`, `heightFalloff`, `noiseAmount`, `noiseScale`, `windX`, `windZ`, `maxDistance`, `startDistance`, `depthExponent`, `sunIntensity`, `ambientR`, `ambientG`, `ambientB`, `ambientIntensity`, `ambientShadowedFraction`, `shadowSamples`, `shadowFilterTexels`, `shadowBiasTexels`, `temporal`, `jitter`, `historyBlend`, `strength`, `skyDistanceFraction`, `debugView` (`Off`, `InScatterOnly`, `Transmittance`) | [Volumetric light](../reference/render-features/volumetric-light.md) |
| `ToneMapping` | `exposure`, `saturation`, `dither`, `mode` (`Clamp`, `Aces`, `PbrNeutral`) | [Tone mapping](../reference/render-features/tone-mapping.md) |
| `SceneLighting` | `skyAmbientR/G/B`, `groundAmbientR/G/B`, `ambientIntensity`, `materialDebugView` (integer 0..8) | [Scene lighting](../reference/render-features/scene-lighting.md) |
| `Atmosphere` | `sceneLightFollowsAtmosphere`, `autoScale`, `aerialPerspectiveStrength`, `aerialPerspectiveMaxDistanceKm`, `aerialPerspectiveDepthExponent`, `aerialPerspectiveSamplesPerSlice`, `aerialPerspectiveScatteringExaggeration` | [Atmosphere](../reference/render-features/atmosphere.md) |

### Read-only state per feature
| `name=` | What `state` also returns |
|---|---|
| `Gtao` | Per view: `status` (`NotRendered`, `Ok`, `NoValidCamera`, `ShadersNotReady`, `TargetFailed`, `PassDisabled`), `frameIndex`, `aoExtent`, `divisor`, `historyUsed`, `screenRadiusAt5m`, `textureBytes` |
| `Shadow`, `Ssgi`, `VolumetricLight`, `ToneMapping` | A per-view snapshot of what was applied last frame |
| `SceneLighting` | `game.` and `scene.` applied values |
| `Atmosphere` | Per view: `active`, `sunTintR/G/B`, `ambientScale`, `frameIndex`, `aerial.{maxDistanceKm,exaggeration,shadowBlend,cascadeCount,receiver,scaleHeld,frameIndex}`. `receiver` is `NotChecked`, `Used`, `Missing`, `Rejected` or `Mismatch`. |
| `GpuDrivenScene` | See next section |

### GpuDrivenScene state
Top level: `enabled`, `mode`, `cone`, `collectStats`, `debugView`, `supported`. Then the keys below, once per view with prefix `game.` and `scene.`.

| Key | Meaning |
|---|---|
| `rendered`, `frameIndex`, `cameraValid` | The view rendered. The camera is valid. |
| `occlusion` | `NotRequested`, `Applied`, `Unusable`, `NoCullCamera`. Hi-Z works only when `Applied`. |
| `hizReason` | Why Hi-Z was skipped: `None`, `Unavailable`, `TooLarge`, `NothingInView` |
| `inertReason` | Why the GPU path is idle: `none`, `device caps`, `shared scope`, `capture armed`, `toggled off`, `invalid state` |
| `lastRefusalReason` | Text of the last refusal |
| `claimedEntities`, `parts`, `skippedParts`, `skippedInstances` | Work taken by the GPU path. Parts outside the cull frustum. |
| `testedClusters` | Clusters tested this frame |
| `visibleClusters`, `visibleFrame`, `visibleAge`, `testedAtVisibleFrame` | Visible count. It is about 2 frames old. |
| `hidden` | Hidden count. It is `0` while `hiddenValid` is false. |
| `statsCollected`, `visibleValid`, `hiddenValid` | Validity flags. Need `collectStats=true`. Counts are lagged. |
| `ledgerBytes` | GPU buffer bytes of the path |
| `cpuTested`, `cpuCulled`, `cpuUnbounded` | CPU draw cull counts (1 frame old) |
| `cpuCullState` | `active`, `off`, `suppressed: frame capture`, `no camera` |
| `cullCamera`, `hizSource` | `own`, `primary`, `none` |
| `sharedSurvivors` | The Scene view reused the Game view's survivors |
| `cpuPath.*` | Renderables kept on the CPU path, by reason: `skinned`, `noIndexBuffer`, `unsupportedLayout`, `noClusterTable`, `tooFewInstances`, `nonFinite`, `noSlot` |
| `reasons.*` | Cull outcome counters: `valid`, `frustum`, `cone`, `pyramid`, `history`, `kept`. Only with `collectStats`. Read `reasons.valid` first. |

`collectStats=true` adds 3 extra passes per part slot (about 0.01 ms). Counts are clusters, never triangles.

### Feature debug recipes
| Goal | Calls |
|---|---|
| Is Hi-Z culling working? | `set?name=GpuDrivenScene&mode=HiZ&collectStats=true` → wait a few frames → `state?name=GpuDrivenScene`. Want `game.occlusion==Applied` and `game.visibleValid==true`. Compare `visibleClusters` with `testedClusters`. |
| Why is the GPU path off? | `state?name=GpuDrivenScene`, read `game.inertReason` |
| Show shadow cascades | `set?name=Shadow&debugView=CascadeColors`, then `/get_game_view` |
| Back to defaults | `invoke?name=Shadow&action=reset_defaults` |

## Frame debugger
Captures one frame of the Game view and lets you inspect each draw. All routes are `GET`. There is no history. Only one frame is kept.

| Route | Params | Effect |
|---|---|---|
| `/frame_debugger/open` | none | Opens the window. It is pinned once to the main ImGui viewport so `/get_swapchain` can see it. |
| `/frame_debugger/enable` | `value=true` or `false` | Same as the checkbox. Enable pauses playback and arms a capture. Disable clears the capture. It does not resume playback. |
| `/frame_debugger/capture` | none | Arms a capture for the next frame. `409` while not enabled. |
| `/frame_debugger/select_event` | `index` integer. `-1` deselects. Clamped to the count. | Selects a tree event. |
| `/frame_debugger/set_channel` | `value=all`, `r`, `g`, `b` or `a` | Channel isolation. Other value = `400`. |
| `/frame_debugger/set_levels` | `black=<f>&white=<f>` (both required) | Levels. A wrong pair is corrected (`white > black + 0.001`). |
| `/frame_debugger/state` | none | Flat JSON, no `success` field. |
| `/frame_debugger/get_event_texture` | `index` (per-draw index), `format=json` for base64 JSON | PNG of one draw's attachment. |

- `state` returns: `enabled`, `windowOpen`, `hasCapturedFrame`, `totalEventCount`, `selectedEventIndex`, `channel`, `levelsBlack`, `levelsWhite`, `perDrawEventCount`.
- Other routes reply `{"success":b,"state":{...}}`. A failure is `409` with `"state"` inside, so you can see why.
- `perDrawEventCount` has its own index space. It is not the tree `eventIndex`.
- Only the armed per-draw index is kept (VRAM bound). A new index re-arms and returns `404` first. Poll until `200`.
- `400` bad param. `503` bridge missing or busy. `504` timeout.
- Pass toggles (`/render_graph/set_pass_enabled`) re-arm the capture while the debugger is enabled.
- Each capture runs 3 checks. Read results with `/get_logs?category=RenderPassHonesty`, `FrameDebuggerSideChannel`, `FrameDebuggerCoverage`.

Capture flow:
1. `GET /frame_debugger/open`, then `GET /frame_debugger/enable?value=true`.
2. `GET /frame_debugger/capture`. `success` must be `true`.
3. Poll `GET /frame_debugger/state` until `hasCapturedFrame` is `true` and `totalEventCount > 0`.
4. `GET /frame_debugger/select_event?index=N`, then `GET /get_swapchain` to see the window. Or `GET /frame_debugger/get_event_texture?index=K` for one draw.
5. Optional: `set_channel`, `set_levels`.
6. `GET /frame_debugger/enable?value=false`. Playback stays paused. Resume it in the UI.

Gotcha: while the debugger is enabled, GpuDrivenScene reports `inertReason:"capture armed"` and `cpuCullState:"suppressed: frame capture"`. Close it before you benchmark.

More: [Frame debugger](frame-debugger.md).

## Render graph
| Route | Params | Reply |
|---|---|---|
| `GET /render_graph` | none | Big JSON. See below. Published metadata, so no wait. `503` if the capture bridge is missing. |
| `GET /render_graph/passes` | none | `{"passes":[{"name","enabled","ever_declared_this_session","owning_feature_name"}]}` sorted by name. |
| `GET /render_graph/set_pass_enabled` | `name`, `enabled=true` or `false` | `{"success":true}`. Built-in passes only. |
| `GET /render_graph/set_feature_enabled` | `name`, `enabled`, optional `view=primary` or `secondary` | `{"success":true}`. No `view` = both views. |
| `GET /render_graph/set_feature_priority` | `name`, `priority` integer (may be negative) | Reorders inside one stage only. |
| `GET /render_graph/set_display_regime` | `regime=present` or `offscreen` | `{"success":true,"window_open":b}`. `window_open:false` = the panel is closed, nothing visible changed. |

Statuses: `400` bad query. `409` name rejected. `503` bridge missing or busy. `504` timeout.

### Toggle rules
- `set_pass_enabled` deny-list: `Present` and `ClearViewTarget`. They return `409`.
- Shared passes toggle in both views: `RenderOpaque`, `DrawSkyBackground`, `RenderTransparent`, `AtmosphereComposite`.
- `set_feature_enabled` without `view` sets the global switch and both view switches. With `view` it sets only that view.
- An unknown feature name gives `409 "X" matches no loaded plugin render feature.` The name must match a feature in `render_features[]` of `/render_graph`. The culling bench scripts use it on built-in features.
- A disabled built-in pass is never declared. It disappears from the `/render_graph` pass tables. It stays visible in `/render_graph/passes`.
- Some features default to off in the Scene view: VolumetricLight, Ssgi, Gtao and the Atmosphere aerial passes.

Gotcha: a typo in `set_pass_enabled` returns `200 {"success":true}` and creates a ghost entry. The ghost has `ever_declared_this_session:false` and the log shows a warning from `RenderPassToggleRegistry`. At most 256 ghosts are kept. Always check `/render_graph/passes` after a toggle.

### `GET /render_graph` shape
Top level: `schema_version`, `offscreen_regime`, `present_regime`, `render_features[]`, `indirect_draw{}`, `gpu_wait{}`, `gpu_memory{}`.

| Object | Fields |
|---|---|
| Regime | `regime_name`, `passes[]`, `resources[]`, `gpu_frame_status`, `gpu_span_milliseconds`, `gpu_folded_passes`, `dropped_gpu_frames` |
| Pass | `name`, `is_culled`, `kind`, `category`, `draw_kind`, `view_scope`, `view` (`primary`, `secondary`, `shared`), `instance`, `owning_feature_name`, `reads[]`, `writes[]`, `draw_call_count`, `triangle_count`, `indirect_draw_count`, `gpu_timing_status`, `gpu_timing_milliseconds`, `gpu_barrier_milliseconds`, `gpu_work_milliseconds` |
| Pass ref | `name`, `kind` (`Texture`, `Buffer`, `VolumeTexture`, `TextureArray`), `binding_stage_label`, `barrier_transition_label` |
| Resource | `name`, `is_imported`, `first_use_pass_index`, `last_use_pass_index`, `first_use_pass_name`, `last_use_pass_name` |
| Feature | `name`, `stage`, `priority`, `blend_mode`, `enabled`, `enabled_primary`, `enabled_secondary`, `is_project_feature` |
| `gpu_wait` | `device_lost`, `consecutive_frame_timeouts`, `abandoned_objects` |
| `gpu_memory` | `total_bytes`, `mesh_count`, `by_name`. This is the GPU memory route. |

- `gpu_frame_status` is `Present`, `Partial`, `Absent`, `Error`, `Unsupported`, `Inactive` or `Unknown`.
- The three `gpu_*_milliseconds` fields are `null` when not measured. Barrier time exists only in fine stamp mode.
- A feature runs in a view only when `enabled` and that view's flag are both `true`.
- Use it for: GPU ms per pass, culled passes, who reads or writes a texture. Feed texture names to `/get_texture?texture_name=`.

More: [Render graph](../architecture/render-graph.md).

## Project assembly
Routes under `/project_assembly/`. A missing capability gives `503 "<name> capability not available"`.

| Route | Params | Reply and notes |
|---|---|---|
| `GET /hot_reload/status` | none | `{"phase","project_name","cycle_id","phase_elapsed_ms","last_outcome","last_error_message"}`. `phase` is `Idle` when no cycle runs. `last_outcome` is `""`, `Success`, `RolledBack` or `CriticalFailure`. It stays set after `Idle`. |
| `GET /list_projects` | none | `[{"name","tier"}]` |
| `POST /create_project` | `name` | `{"created_source_directory":"<path>"}`. Writes a 3-file scaffold, runs a CMake reconfigure, makes it active. `400` bad name or exists. |
| `POST /create_asset` | `kind` = `render_pass`, `compute_shader`, `shader_pair` or `screen_post_process_pass`; `name` | `{"created_files":[...],"reminder_message":"..."}`. `400` bad kind or failure. |
| `POST /open_project` | `name` | `{"status_message","load_attempted","load_succeeded"}`. `success` can be true for projects that are not built yet. `400` not a project. |
| `POST /debug/compile_only` | `name` | `{"started":true}`, or `{"started":false,"reason":"..."}` if a build is running. Always `200`. Async: poll status. |
| `POST /hot_reload` | `name` | Blocks up to 600 s (compile, then reload on the main thread). `200` = a status JSON after the cycle. Check `last_outcome`: it can be `RolledBack`. `503` = rejected, or your call timed out. The cycle may still run. Poll status. |
| `GET /debug/ledger` | `name` (required) | What that assembly registered: `render_pass_names`, `render_feature_names`, `panel_names`, `component_type_names` |
| `GET /debug/loaded_assemblies` | none | `{"dll_file_names":[...]}` |
| `GET /debug/component_types` | none | `{"type_names":[...]}` sorted |
| `GET /debug/scene_snapshot` | none | Scene JSON, same as Save Scene. `400` failure. `503` no editor. `504` timeout. |
| `POST /debug/set_probe_marker_value` | `value` integer | `{"success":b}`. Test only. Sets `value` on the `ProbeHotReloadMarker` component. `false` if no such entity. |

`POST` routes read the query string, not a JSON body.

```text
curl.exe -X POST "http://127.0.0.1:8080/project_assembly/hot_reload?name=MyGame"
curl.exe "http://127.0.0.1:8080/project_assembly/hot_reload/status"
```

Hot reload debug flow:
1. `GET /project_assembly/list_projects`
2. `POST /project_assembly/debug/compile_only?name=X`
3. Poll `GET /project_assembly/hot_reload/status` until `phase` is `Idle`.
4. `POST /project_assembly/hot_reload?name=X`, then read `last_outcome`.
5. Prove registration: `loaded_assemblies`, `debug/ledger?name=X`, `component_types`.
6. Prove the scene survived: `debug/scene_snapshot`.

More: [Project assembly guide](../guides/project-assembly.md).

## Related
- [HTTP cheat sheet](http-cheat-sheet.md) · [HTTP routes](http-routes.md) · [HTTP server](../architecture/http-server.md)
- [Profiling](profiling.md) · [Frame debugger](frame-debugger.md) · [Logging](logging.md)
- [Render features](../reference/render-features/README.md) · [Render graph](../architecture/render-graph.md)
