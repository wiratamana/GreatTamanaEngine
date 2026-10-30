# HTTP debugging cheat sheet
> One-page guide to the editor's HTTP debug server. For humans and AI agents who need to see, measure or poke a running editor.
> Verified against code: 2026-10-31

## TL;DR
- The editor runs a server on `http://127.0.0.1:8080`. Loopback only. The port is fixed.
- It answers JSON and PNG. Reads are `GET`. Scene and asset commands are `POST` with a JSON body.
- Each route waits for the main thread. One request per bridge at a time. A busy bridge gives `503`.
- Route names, params and defaults here come from the code. The examples were read from the code, not run live.
- Mutating routes are marked **W**. Image routes are marked **IMG**. Restore state after a test.

## 1. Quick start
Start the editor (`--no-vsync` for performance work). `/http_hello_world` returns `hello world` when the server is up.

```cmd
build\GreatTamanaEditor.exe --no-vsync
curl.exe http://127.0.0.1:8080/http_hello_world
curl.exe http://127.0.0.1:8080/get_blocking_errors
curl.exe http://127.0.0.1:8080/render_graph -o rg.json
curl.exe http://127.0.0.1:8080/get_game_view -o game.png
curl.exe http://127.0.0.1:8080/profiler
```

| You are | Use | Why |
|---|---|---|
| Human | `curl.exe` | Works in `cmd` and PowerShell. Never plain `curl` in PowerShell (it is an alias). |
| AI agent, JSON | `http_request` | No shell quoting. Put JSON in `body`. |
| AI agent, picture | `gte_send_request` | Returns the PNG as a real image. |

"Connection refused" means the editor is down, or another editor already holds port 8080.

## 2. Rules of the road
| Rule | Detail |
|---|---|
| One request per bridge | A second request on a busy bridge gets `503 ... already in progress`. Send calls one by one. |
| `504` is not "failed" | The main thread did not answer in time. The work may still run. Read state before you retry. |
| Timeouts | `3 s` default. `120 s` asset import and cluster build. `300 s` `open_preset_scene`. `600 s` hot reload. |
| Methods | Only `GET` and `POST` exist. |
| POST body | JSON object. No `Content-Type` needed. Bad JSON gives `400`. |
| POST with query | `/project_assembly/*`, `/profiler/set` and `/profiler/export` read the query string. |
| Bools | Exactly `true` or `false`, lowercase. `/profiler/set` and `feature_debug` values also take `1` / `0`. |
| Strict params | `12abc` is rejected. Unknown keys are rejected. No silent guess. |
| Browser guard | `POST /exit`, `POST /profiler/set`, `POST /profiler/export` and arming `/debug/fault` refuse browser requests (`403`). |
| Frame stalled | Bridge routes give `504`. `/profiler*`, `/get_logs`, `/debug/fault` still answer. |

More about threads and bridges: [HTTP server](../architecture/http-server.md).

## 3. I want to...
| Goal | Call |
|---|---|
| See the 3D game picture | `GET /get_game_view` **IMG** |
| See the whole editor window | `GET /get_swapchain` **IMG** |
| See one render texture | `GET /get_texture?texture_name=<name>[&channel=depth]` **IMG** (names: `/list_textures`) |
| Is anything broken? | `GET /get_blocking_errors` |
| Read warnings and errors | `GET /get_logs?min_level=warning&limit=50` |
| Clean logs before a repro | `POST /clear_logs` **W** |
| Why is my pass missing or culled? | `GET /render_graph` (`is_culled`, `owning_feature_name`) |
| Turn a pass off | `GET /render_graph/set_pass_enabled?name=<pass>&enabled=false` **W** |
| Turn a feature off | `GET /render_graph/set_feature_enabled?name=<feature>&enabled=false` **W** |
| Why is the frame slow? | `GET /profiler` (`verdict.kind`) |
| Where is the spike? | `GET /profiler/spikes`, then `/profiler/spike?index=<frame_index>` |
| Which code is slow, with calls and ms? | `GET /profiler/table?view=flat` (plain title, explanation, calls, total, self, `origin`) |
| See the CPU and GPU frame graph | `GET /profiler/graph_image` **IMG** (grey frame, blue CPU busy, orange GPU) |
| Is my project code timed? | `GET /profiler/scope?name=<Namespace::Function>` or `/profiler/table?view=flat` (`origin:"project"`) |
| Is the editor frozen? | `GET /profiler` (`live_block[].site`) |
| Is Hi-Z culling working? | `GET /feature_debug/state?name=GpuDrivenScene` |
| Change a feature setting | `GET /feature_debug/set?name=<feature>&<key>=<value>` **W** |
| Reset a feature | `GET /feature_debug/invoke?name=<feature>&action=reset_defaults` **W** |
| Inspect one draw call | `/frame_debugger/*` (see R6) |
| Move the scene camera | `GET /scene_camera/set_pose?x=0&y=2&z=5&yaw=0&pitch=0` **W** |
| Build a repro scene | `POST /instantiate_primitive` **W** |
| Load the Hi-Z demo scene | `POST /open_preset_scene` **W** |
| Hot reload a game project | `POST /project_assembly/hot_reload?name=<project>` **W** |
| Test a recovery path | `GET /debug/fault?kind=<kind>` **W** |

## 4. Status codes
| Code | Meaning | What to do |
|---|---|---|
| `200` | OK | Read the body. Some reads have no `success` key. |
| `400` | Bad param, bad JSON, or caller error | Read `error`. It names the bad key and the allowed range. |
| `403` | Browser request refused | Call from `curl.exe` or a script. Do not send `Host` or `Origin` headers. |
| `404` | Unknown name (tab, entity, feature, event texture) | Check the name. `get_event_texture` gives `404` until captured. |
| `409` | Valid request, state does not allow it | Open the panel or view first. Read `error` or `state`. |
| `500` | I/O or scene open failure | Read `/get_logs?min_level=error`. |
| `503` | Bridge busy, missing, GPU busy or device lost | Wait and retry one call at a time. If GPU lost, restart the editor. |
| `504` | Main thread too slow | Check `/profiler`. Read state before you retry a write. |

Errors are JSON `{"success":false,"error":"..."}`. Exception: `/get_game_view` and `/get_swapchain` fail with plain text.

## 5. Route index
Full params and responses: [HTTP routes](http-routes.md) and [HTTP routes (debug)](http-routes-debug.md).
**W** = changes state. **IMG** = returns a picture. Routes are `GET` unless noted.

| Group | Route | Key params | Mark |
|---|---|---|---|
| Capture | `/get_game_view`, `/get_swapchain` | `format` | **IMG** |
| Capture | `/get_texture` | `texture_name`, `channel=color\|depth`, `format` | **IMG** |
| Capture | `/get_window` | `name` (floating ImGui window) | **IMG** |
| Capture | `/list_textures`, `/http_hello_world` | none | |
| Logs | `/get_logs` | `since_id`, `min_level`, `category`, `keyword`, `frame_min`, `frame_max`, `limit` | |
| Logs | `/get_blocking_errors` | `since_id` | |
| Logs | `POST /clear_logs` | none | **W** |
| Graph | `/render_graph`, `/render_graph/passes` | none | |
| Graph | `/render_graph/set_pass_enabled` | `name`, `enabled` | **W** |
| Graph | `/render_graph/set_feature_enabled` | `name`, `enabled`, `view=primary\|secondary` | **W** |
| Graph | `/render_graph/set_feature_priority`, `/set_display_regime` | `name`,`priority` / `regime=present\|offscreen` | **W** |
| Feature | `/feature_debug/list`, `/feature_debug/state` | `name` only for `state` | |
| Feature | `/feature_debug/set`, `/feature_debug/invoke` | `name`, `<key>=<value>` (max 64) / `action` | **W** |
| Profiler | `/profiler`, `/profiler/spikes` | none | |
| Profiler | `/profiler/frames` | `count` 1..120 (default 30) | |
| Profiler | `/profiler/spike` | `index` (spike `frame_index`) | |
| Profiler | `/profiler/scope` | `name` (exact scope name) | |
| Profiler | `/profiler/timeline_image` | `frame`, `width` 200..2400 | **IMG** |
| Profiler | `/profiler/graph_image` | `width` 200..2400, `height` 100..800, `frames` 30..120 | **IMG** |
| Profiler | `/profiler/table` | `view=tree\|flat`, `lane=all\|main\|workers\|gpu`, `limit` 1..256 | |
| Profiler | `POST /profiler/set` | `capture`, `display_pause`, `pin`, `unpin`, `spike_threshold_ms` | **W** |
| Profiler | `POST /profiler/export` | `frames` 1..120 | |
| Fault | `/debug/fault` | none = list. `kind`, `count` 1..1000 = arm | **W** |
| Frame dbg | `/frame_debugger/open`, `/enable`, `/capture` | `value=true\|false` (enable) | **W** |
| Frame dbg | `/frame_debugger/select_event`, `/set_channel`, `/set_levels` | `index` / `value=all\|r\|g\|b\|a` / `black`,`white` | **W** |
| Frame dbg | `/frame_debugger/state` | none | |
| Frame dbg | `/frame_debugger/get_event_texture` | `index`, `format` | **IMG** |
| Scene | `POST /instantiate_primitive` | `shape`, `name`, `world_position`, `parent` | **W** |
| Scene | `POST /instantiate_light`, `/instantiate_asset` | `name`, `color`, `illuminance_lux` / `gta_path` | **W** |
| Scene | `POST /set_entity_trs` | `name`, `translation`, `rotation_euler_degrees`, `scale` | **W** |
| Scene | `POST /delete_entity` | `name` | **W** |
| Scene | `POST /save_scene`, `/load_scene` | `path` (optional) | **W** |
| Scene | `POST /open_preset_scene`, `/close_preset_scene` | `preset=sponza\|hiz_demo`, `count`, `spacing`, `profile` | **W** |
| Scene | `POST /spawn_instance_grid` | `count`, `spacing` | **W** |
| Camera | `/scene_camera/state`, `/scene_camera/pose` | none | |
| Camera | `/scene_camera/set_pose` | `x`, `y`, `z`, `yaw`, `pitch` (all 5) | **W** |
| Camera | `/scene_camera/pick`, `/scene_camera/select` | `x`,`y` (0..1) / `name` | **W** (select) |
| Camera | `/scene_camera/image` | `overlay=true\|false` | **IMG** |
| Editor | `/list_tabs`, `/activate_tab`, `/split_views`, `/panel_visible` | `name` / `enabled` / `visible` | **W** |
| Editor | `/hierarchy_state`, `POST /hierarchy_action` | `first`,`count` / `kind`,`target`,`dragged` | **W** (action) |
| Editor | `/inspector_state` (also `rotation`: shown Euler, field centers, `rederive_count`), `/select_asset`, `/select_entity` | `path` (abs `.gta`) / `name` | **W** (select) |
| Editor | `/ui_mouse` (drag real ImGui widgets, finish with `release`) | `action=move\|down\|up\|release`, `x`, `y`, `button` | **W** |
| Editor | `POST /exit` | none (closes the editor) | **W** |
| Cluster | `/cluster_viewer/open`, `/state`, `/set`, `/pick` | `path` / `key=value` / `x`,`y` | **W** (open, set) |
| Cluster | `POST /cluster_viewer/verify`, `/capture_hiz` | none | **W** |
| Cluster | `/cluster_viewer/image` | `target=view\|debug`, `overlay` | **IMG** |
| Assets | `POST /import_asset`, `/build_clusters`, `/clone_clusters`, `/inspector/build_clusters` | `source_path` / `gta_path` | **W** |
| Project | `/project_assembly/list_projects`, `/hot_reload/status` | none | |
| Project | `POST /project_assembly/create_project`, `/open_project` | `name` (query) | **W** |
| Project | `POST /project_assembly/create_asset` | `kind`, `name` (query) | **W** |
| Project | `POST /project_assembly/debug/compile_only`, `/hot_reload` | `name` (query). Hot reload blocks up to 600 s | **W** |
| Project | `/project_assembly/debug/ledger` | `name` | |
| Project | `/project_assembly/debug/loaded_assemblies`, `/component_types`, `/scene_snapshot` | none | |
| Demo | `/hiz_demo/geometry_only_features` | none | |

## 6. Images
| How | Do this |
|---|---|
| AI agent | `gte_send_request` with `/get_game_view`. It shows the PNG. |
| AI agent, `http_request` | It does not show images. Use `save_to_file`, then `load_image`. |
| Human | `curl.exe http://127.0.0.1:8080/get_game_view -o game.png` |
| JSON with base64 | Add `?format=json`. You get `width`, `height`, `format`, `data_base64`. |

- `format=png` or no `format` gives raw PNG. `format=base64` and `format=json` give JSON.
- Any other `format` value silently gives PNG. It is not an error.
- Header `Accept: application/json` also selects JSON.
- HDR and depth are tone-mapped for viewing. The picture is not raw values.
- Every **IMG** route in section 5 follows these rules. Draw-call images need a Frame Debugger capture first (R6).

Never use a full-screen screenshot. Ask the engine for the image.

## 7. Recipes
### R1. Visual bug (wrong colors, missing object)
```cmd
curl.exe -X POST http://127.0.0.1:8080/clear_logs
curl.exe http://127.0.0.1:8080/get_blocking_errors
curl.exe "http://127.0.0.1:8080/get_logs?min_level=warning"
curl.exe http://127.0.0.1:8080/get_game_view -o game.png
curl.exe http://127.0.0.1:8080/render_graph -o rg.json
curl.exe http://127.0.0.1:8080/list_textures
```
- Look for: `count` above 0 in logs. A pass with `is_culled:true`. A texture with large `frames_since_update` (its pass is not running).
- Next: capture the texture that feeds the bad pass. Toggle passes off one by one (`set_pass_enabled`) and capture again. Then R6.

### R2. Performance hitch
```cmd
curl.exe http://127.0.0.1:8080/profiler
curl.exe http://127.0.0.1:8080/profiler/spikes
curl.exe -X POST "http://127.0.0.1:8080/profiler/set?pin=<frame_index>"
curl.exe "http://127.0.0.1:8080/profiler/spike?index=<frame_index>"
curl.exe "http://127.0.0.1:8080/profiler/timeline_image?frame=<frame_index>" -o tl.png
```
- Call `/profiler` twice. The first call after a quiet time can be old.
- Look for: `verdict.kind` (`cpu_limited`, `gpu_limited`, `present_limited`, `stalled`, `unreliable`). A spike with `classification:"unexplained"`. The scope with the largest `self`.
- Every number is a metric object. `value:null` means no data, never zero. Check `status` first. If `checks[]` has a `fail`, do not trust the numbers.
- Next: `gpu_limited` goes to `/render_graph` (`gpu_timing_milliseconds`). `cpu_limited` goes to `/profiler/scope?name=...`.

### R3. A/B benchmark
```cmd
build\GreatTamanaEditor.exe --no-vsync
curl.exe "http://127.0.0.1:8080/feature_debug/set?name=GpuDrivenScene&collectStats=false"
curl.exe "http://127.0.0.1:8080/profiler/frames?count=120"
```
- Check `device_state.present_policy` is `uncapped`. Fix camera and window size. Warm up 120 frames.
- Poll `/profiler/frames`, dedupe by `frame_index`, keep `status:"Present"` only. Report median and p95. Not FPS.
- Close the Frame Debugger first. Restore every setting after.
- Script: `tools/profiler_ab/run_ab.ps1`. See [Profiling](profiling.md).

### R4. GPU-driven culling check
```cmd
curl.exe "http://127.0.0.1:8080/feature_debug/set?name=GpuDrivenScene&mode=HiZ&collectStats=true"
curl.exe "http://127.0.0.1:8080/feature_debug/state?name=GpuDrivenScene"
curl.exe -X POST http://127.0.0.1:8080/cluster_viewer/verify
```
- Wait a few frames after `set`. Counts lag by about 2 frames.
- Good: `game.occlusion` is `Applied`. `game.visibleValid` is `true`. `visibleClusters` is below `testedClusters`.
- Bad: `game.inertReason` is not `none`. `capture armed` means the Frame Debugger is on. `hizReason` says why Hi-Z was skipped (`Unavailable`, `TooLarge`, `NothingInView`).
- In the Cluster Viewer, `truth.falseCulls` above 0 is an occlusion bug.
- Page: [GPU-driven scene](../reference/render-features/gpu-driven-scene.md).

### R5. Lighting, shadow, AO, GI look
```cmd
curl.exe "http://127.0.0.1:8080/feature_debug/set?name=Shadow&debugView=CascadeColors"
curl.exe http://127.0.0.1:8080/get_game_view -o shadow.png
curl.exe "http://127.0.0.1:8080/feature_debug/invoke?name=Shadow&action=reset_defaults"
```
- `debugView` values: `Shadow` = `Off` `CascadeColors` `Mask`. `Gtao` = `Off` `AoOnly`. `Ssgi` = `Off` `GiOnly` `Normals`. `VolumetricLight` = `Off` `InScatterOnly` `Transmittance`. `SceneLighting` uses `materialDebugView` `0..8`.
- Values are clamped, not rejected. Read the returned `fields` to see what was applied.
- Settings and ranges: [Render features](../reference/render-features/README.md), [Shadow](../reference/render-features/shadow.md), [GTAO](../reference/render-features/gtao.md), [SSGI](../reference/render-features/ssgi.md), [Volumetric light](../reference/render-features/volumetric-light.md), [Atmosphere](../reference/render-features/atmosphere.md), [Tone mapping](../reference/render-features/tone-mapping.md), [Scene lighting](../reference/render-features/scene-lighting.md).

### R6. Inspect one draw call (Frame Debugger)
```cmd
curl.exe http://127.0.0.1:8080/frame_debugger/open
curl.exe "http://127.0.0.1:8080/frame_debugger/enable?value=true"
curl.exe http://127.0.0.1:8080/frame_debugger/capture
curl.exe http://127.0.0.1:8080/frame_debugger/state
curl.exe "http://127.0.0.1:8080/frame_debugger/select_event?index=5"
curl.exe "http://127.0.0.1:8080/frame_debugger/get_event_texture?index=5" -o draw5.png
```
- Poll `state` until `hasCapturedFrame:true` and `totalEventCount` above 0.
- `get_event_texture` gives `404` until that index is armed and a later frame is captured. Poll it.
- `enable?value=true` pauses playback. `enable?value=false` does not resume it. Resume in the editor.
- While it is on, GpuDrivenScene is inert (`capture armed`). Close it before a benchmark.
- Details: [Frame debugger](frame-debugger.md).

### R7. Hot reload cycle
```cmd
curl.exe http://127.0.0.1:8080/project_assembly/list_projects
curl.exe -X POST "http://127.0.0.1:8080/project_assembly/debug/compile_only?name=MyProj"
curl.exe http://127.0.0.1:8080/project_assembly/hot_reload/status
curl.exe -X POST "http://127.0.0.1:8080/project_assembly/hot_reload?name=MyProj"
curl.exe "http://127.0.0.1:8080/project_assembly/debug/ledger?name=MyProj"
```
- Poll `status` until `phase` is `Idle`. Then call `hot_reload`. It blocks until the cycle ends.
- Look for: `last_outcome` is `Success`. `RolledBack` or `CriticalFailure` means read `last_error_message` and the logs.
- `ledger` lists what the project registered. `/project_assembly/debug/scene_snapshot` proves the scene survived.
- A `503` can mean your HTTP call timed out while the cycle still runs. Poll `status`.
- Guide: [Project assembly](../guides/project-assembly.md).

### R8. Fault injection
```cmd
curl.exe http://127.0.0.1:8080/debug/fault
curl.exe "http://127.0.0.1:8080/debug/fault?kind=main_thread_stall&count=2"
curl.exe http://127.0.0.1:8080/profiler
```
- `kind` values: `offscreen_throw` (Game/Scene render throws, frame must survive), `present_throw` (throws after image acquire), `main_thread_stall` (sleeps 3 s, bridge routes give `504`), `device_lost` (final, restart the editor).
- `count` is 1..1000 (default 1). Arming replaces the count. No `kind` lists what is armed.
- After `main_thread_stall`: `verdict.kind` is `stalled` and `live_block` shows the sleep.

### R9. Build a repro scene
```cmd
curl.exe -X POST http://127.0.0.1:8080/instantiate_primitive -d "{\"shape\":\"cube\",\"name\":\"T1\",\"world_position\":{\"x\":0,\"y\":1,\"z\":0}}"
curl.exe -X POST http://127.0.0.1:8080/set_entity_trs -d "{\"name\":\"T1\",\"scale\":{\"x\":2,\"y\":2,\"z\":2}}"
curl.exe "http://127.0.0.1:8080/scene_camera/set_pose?x=0&y=2&z=6&yaw=0&pitch=-10"
curl.exe "http://127.0.0.1:8080/scene_camera/image?overlay=false" -o scene.png
curl.exe -X POST http://127.0.0.1:8080/delete_entity -d "{\"name\":\"T1\"}"
```
- Entity names are made unique. Always use the `name` from the reply for later calls.
- Load test: `POST /open_preset_scene` with `{"preset":"hiz_demo","count":50000,"spacing":2,"profile":"geometry_only"}`.
- `POST /spawn_instance_grid` spawns a grid of instances (`count` 1..100000).

### R10. Editor looks frozen
```cmd
curl.exe http://127.0.0.1:8080/profiler
curl.exe http://127.0.0.1:8080/debug/fault
```
- `/profiler` answers even when the main thread is blocked. Read `live_block[].site` (what it waits on) and `snapshot_age`.
- `state:"no_snapshot"` is not an error. Retry.
- Bridge routes give `504` while frozen. That is expected. See [Troubleshooting](../troubleshooting.md).

## 8. Gotchas
- Only one editor can serve. A second editor has no HTTP (port 8080 busy).
- `set_pass_enabled` with a typo returns `200` and makes a ghost entry. Check `ever_declared_this_session:true` in `/render_graph/passes`.
- A disabled pass vanishes from `/render_graph`. It only shows in `/render_graph/passes`.
- Toggles and feature settings live in memory. A restart resets them.
- Deny-listed passes (`Present`, `ClearViewTarget`) cannot be toggled (`409`).
- `/get_logs` returns the newest 200 by default. `limit=0` gives the whole ring (2000 entries).
- Log `level` in the reply is `Warning`. In the query use `min_level=warning` (any case).
- `?format=garbage` gives PNG, not an error.
- `/scene_camera/set_pose` needs all 5 params: `x`, `y`, `z`, `yaw`, `pitch`.
- `/panel_visible` supports only `Hierarchy`.
- `feature_debug/set` is all-or-nothing, and values are clamped. Read the reply.
- The profiler GPU lane is not time-aligned to the CPU lanes.
- Cluster counts are clusters, not triangles.
- `primary` is the Game view. `secondary` is the Scene view.

## 9. Windows quoting
| Shell | Example |
|---|---|
| `cmd` | `curl.exe -X POST http://127.0.0.1:8080/delete_entity -d "{\"name\":\"T1\"}"` |
| PowerShell | `curl.exe -X POST http://127.0.0.1:8080/delete_entity -d '{\"name\":\"T1\"}'` |
| Any shell, from file | `curl.exe -X POST http://127.0.0.1:8080/delete_entity --data-binary "@body.json"` |
| AI `http_request` | `url:"/delete_entity"`, `body:"{\"name\":\"T1\"}"` |

- In PowerShell, quote URLs that contain `&`: `curl.exe "http://127.0.0.1:8080/get_logs?min_level=warning&limit=50"`.
- The file method is the safest for big JSON.

## Related
- [HTTP routes](http-routes.md) · [HTTP routes (debug)](http-routes-debug.md) · [HTTP server](../architecture/http-server.md)
- [Profiling](profiling.md) · [Frame debugger](frame-debugger.md) · [Logging](logging.md)
- [Render features](../reference/render-features/README.md) · [Project assembly](../guides/project-assembly.md)
- [Troubleshooting](../troubleshooting.md) · [Glossary](../glossary.md)
