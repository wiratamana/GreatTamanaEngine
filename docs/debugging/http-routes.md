# HTTP routes

> Reference for the general debug routes: capture, logs, scene, editor UI, scene camera, assets and clusters. For people and AI agents who need exact parameters.
> Verified against code: 2026-11-01

## TL;DR
- Server: `127.0.0.1:8080`, editor only. See [HTTP server](../architecture/http-server.md).
- Reads use `GET` with query parameters. Scene and asset commands use `POST` with a JSON body.
- `/project_assembly/*` POST routes use query parameters, not JSON.
- Errors are `{"success":false,"error":"..."}`. Image routes `/get_game_view` and `/get_swapchain` fail with plain text.
- Profiler, faults, feature debug, frame debugger and render graph control are in [HTTP routes: debug tools](http-routes-debug.md).
- These tables were read from code. They were not run against a live editor.

## Conventions
| Topic | Rule |
|---|---|
| Methods | Only `GET` and `POST` exist. |
| POST body | JSON object. The `Content-Type` header is ignored. |
| Bad JSON | `400` with `malformed JSON body` or `request body must be a JSON object`. |
| Empty body | Allowed for `/save_scene`, `/load_scene`, `/spawn_instance_grid`, `/inspector/build_clusters`. Defaults apply. |
| Booleans in query | Exactly `true` or `false`, lowercase. |
| Unknown JSON fields | Ignored. |
| Success body | Not uniform. Many read routes have no `success` key. |
| Names | JSON keys are `snake_case`. Some state reports use `camelCase`. |

Legend: `*` = required. **Q** = query parameter. **B** = JSON body key. **IMG** = returns an image. **MUT** = changes state.

## Status codes
| Code | Meaning |
|---|---|
| `200` | OK |
| `400` | Bad parameter, bad JSON, or a refused command |
| `403` | Browser request refused (only `/exit`, `/profiler/set`, `/profiler/export`, arming `/debug/fault`) |
| `404` | Unknown tab, entity or camera name. Event image not captured yet. |
| `409` | Valid request, wrong state (panel closed, nothing rendered, no mesh) |
| `500` | Environment failure (scene save, preset open or close) |
| `503` | Bridge missing or busy, GPU busy, device lost, Editor module missing |
| `504` | Main thread was too slow. The work may still happen. |

## Health and discovery
| Route | Params | Response | Notes |
|---|---|---|---|
| `GET /http_hello_world` | none | text `hello world` | Is the server alive? |
| `GET /list_tabs` | none | `{"tabs":["Hierarchy","Inspector",...]}` | Every registered window: built-in windows first, then project windows, then render feature windows. No bridge. |
| `GET /list_textures` | none | `{"textures":[{name, regime, format, width, height, has_depth, frames_since_update, kind, depth}]}` | Names for `/get_texture`. |
| `GET /render_graph` | none | Full graph of the current frame. See below. | No bridge. |

`list_textures` fields:
- `regime` is `synchronous` or `pipelined`.
- `kind` is `texture2d` or `texture3d`. `depth` is the Z size, and 0 for 2D.
- A large `frames_since_update` means the pass is not running. The image is stale.

`GET /render_graph` top-level keys:
- `schema_version`
- `offscreen_regime` and `present_regime`, each with `passes`, `resources`, `gpu_frame_status`, `gpu_span_milliseconds`, `gpu_folded_passes`, `dropped_gpu_frames`
- `render_features[]`: `name`, `stage`, `priority`, `blend_mode`, `enabled`, `enabled_primary`, `enabled_secondary`, `is_project_feature`
- `indirect_draw`, `gpu_wait` (`device_lost`, `consecutive_frame_timeouts`, `abandoned_objects`), `gpu_memory` (`total_bytes`, `mesh_count`, `by_name`)

Pass fields to read first:
- `name`, `is_culled`, `owning_feature_name`, `view` (`shared`, `primary`, `secondary`)
- `reads[]`, `writes[]`
- `draw_call_count`, `triangle_count`, `gpu_timing_milliseconds` (may be `null`)

```text
curl.exe http://127.0.0.1:8080/render_graph
```

## Images
All image routes use the same format rule:

| Request | Reply |
|---|---|
| `?format=png`, or nothing | Raw PNG, `Content-Type: image/png` |
| `?format=base64` or `?format=json` | JSON `{"width","height","format":"png","data_base64"}` |
| Any other `format` value | Raw PNG. It is not an error. |
| No `format`, header `Accept: application/json` | JSON base64 |

- `/get_texture` JSON adds `frames_since_update`.
- Depth and HDR textures are tone-mapped for viewing. The picture is not the raw values.
- AI agents: use `gte_send_request`. It shows the PNG. `http_request` does not show images. Save the file and use `load_image`.

| Route | Captures | Needs | Errors |
|---|---|---|---|
| `GET /get_game_view` **IMG** | Game view render texture. No ImGui. | Game view visible | `409` no Game view, `504` (text) |
| `GET /get_swapchain` **IMG** | The presented editor window. All docked panels. | Frames running | `409`, `503`, `504` (text) |
| `GET /get_texture` **IMG** | Any named render graph texture | Name from `/list_textures` | see below |
| `GET /get_window` **IMG** | A floating (undocked) ImGui window | Window was dragged out once | `400` no name, `409` not floating |
| `GET /scene_camera/image` **IMG** | Scene view | Scene view rendered | `409` |
| `GET /cluster_viewer/image` **IMG** | Cluster Viewer | Viewer open | `409` |
| `GET /frame_debugger/get_event_texture` **IMG** | One captured draw | Capture done | `404` until captured |

`/get_texture` params: `texture_name`* and `channel` (`color` default, or `depth`). Both are case-sensitive.
- `400`: bad `channel`.
- `409`: no depth for this texture.
- `504`: the name never registered or never rendered again. The message says so.
- `503`: another named-texture capture is running.

`/get_window` param: `name`* is the ImGui window title.

```text
curl.exe "http://127.0.0.1:8080/get_texture?texture_name=SceneDepth&channel=depth" -o depth.png
curl.exe http://127.0.0.1:8080/get_game_view -o game.png
```

Each capture kind holds one request. A second capture of the same kind gets `503`.

## Logs
No bridge. These answer at once.

| Route | Params | Response |
|---|---|---|
| `GET /get_logs` | Q `since_id`, `min_level`, `category`, `keyword`, `frame_min`, `frame_max`, `limit` | `{logging_enabled, count, latest_id, entries[]}` |
| `GET /get_blocking_errors` | Q `since_id` | Same shape. Only entries with `is_blocking: true`. |
| `POST /clear_logs` **MUT** | none | `{"success":true,"cleared_count":N}` |

`/get_logs` filters:
| Param | Meaning |
|---|---|
| `since_id` | Entries with a higher id. Default 0 = all. |
| `min_level` | `debug`, `info`, `warning`, `error`. Any letter case. |
| `category` | Exact match. Case-sensitive. |
| `keyword` | Substring of the message. Case-insensitive. |
| `frame_min`, `frame_max` | Inclusive frame range. |
| `limit` | Default 200. Larger than 2000 is cut to 2000. `0` means no limit. |

- Each entry: `id`, `frame`, `timestamp_seconds`, `level` (`Debug`, `Info`, `Warning`, `Error`), `category`, `message`, `is_blocking`.
- You get the newest `limit` matches. The buffer holds 2000 entries.
- `id` only grows. `/clear_logs` does not reset it.
- Bad numbers give `400`.

Polling: read once, keep `latest_id`, then call `?since_id=<latest_id>`.

```text
curl.exe "http://127.0.0.1:8080/get_logs?min_level=warning&limit=50"
curl.exe "http://127.0.0.1:8080/get_logs?category=Network&keyword=bind"
curl.exe -X POST http://127.0.0.1:8080/clear_logs
```

## Scene and entities
All **MUT**. They use `EngineCommandBridge`: one request at a time, 3 s timeout.

| Route | Body | Success | Errors |
|---|---|---|---|
| `POST /instantiate_primitive` | B `shape`* (`cube`, `sphere`, `capsule`, `cone`, `plane`), `name`*, `world_position` {x,y,z}, `parent` (name) | `{success, entity:{index,generation}, name, parent_requested_but_not_found, requested_parent_name}` | `400` |
| `POST /instantiate_light` | B `name`*, `light_type` (`""` or `directional`), `world_position`, `rotation_euler_degrees`, `color` {r,g,b}, `illuminance_lux`, `active`, `parent` | Same shape as primitive | `400` |
| `POST /instantiate_asset` | B `gta_path`* (absolute path of a mesh `.gta`) | `{success, entity, name}` | `400` |
| `POST /delete_entity` | B `name`* | `{success, entity}` | `404` unknown name |
| `POST /set_entity_trs` | B `name`*, `translation`, `rotation_euler_degrees`, `scale` | `{success, entity, changed, transform}` | `404` unknown name, `409` no Transform |
| `POST /save_scene` | B `path` (empty = default path) | `{success, resolved_path}` | `500` I/O, `503` |
| `POST /load_scene` | B `path` (empty = default path) | `{success, resolved_path}` | `400` bad file (scene is kept), `503` |
| `GET /project_assembly/debug/scene_snapshot` | none | Raw scene JSON, same format as a saved scene | `400`, `503` |

Details:
- Entity names are made unique. Always read the returned `name` for the next call.
- A missing `parent` still spawns the entity. The reply sets `parent_requested_but_not_found`.
- In `set_entity_trs`, each of `translation`, `rotation_euler_degrees`, `scale` is all-or-nothing: x, y and z are all required. `null` means "leave it".
- Rotation is Euler degrees: x = pitch, y = yaw, z = roll. The reply has both Euler and `rotation_quaternion`.
- The reply's `rotation_euler_degrees` is the canonical Euler (pitch folded into [-90, 90]). Sending x = 9999 returns x = -81. It is the same rotation.
- Cluster viewer `camPitch`, `camYaw`, `camRoll`: a partial update (e.g. only `camPitch`) re-derives the other angles from the stored quaternion.
- `instantiate_primitive` `world_position` can miss axes. Missing axes are 0.
- `instantiate_light`: `rotation_euler_degrees` is all-or-nothing. `illuminance_lux` must be 0 or more (default 100000). `color` default is 1,1,1.
- `set_entity_trs` with no field is a safe no-op. It returns the current transform.

```text
curl.exe -X POST http://127.0.0.1:8080/instantiate_primitive -d "{\"shape\":\"cube\",\"name\":\"T1\",\"world_position\":{\"x\":0,\"y\":1,\"z\":0}}"
curl.exe -X POST http://127.0.0.1:8080/set_entity_trs -d "{\"name\":\"T1\",\"scale\":{\"x\":2,\"y\":2,\"z\":2}}"
curl.exe -X POST http://127.0.0.1:8080/delete_entity -d "{\"name\":\"T1\"}"
```

JSON quoting on Windows is painful. Use `--data-binary "@body.json"`, or the `http_request` tool with its `body` parameter.

## Preset scenes
| Route | Body | Success | Errors |
|---|---|---|---|
| `POST /open_preset_scene` **MUT** | B `preset`* (`sponza` or `hiz_demo`), `count` (integer, 0 or more), `spacing` (0.1 to 1000), `profile` (`geometry_only` default, or `full`) | `{success, preset, message, ...}` | `400`, `500` open failed (old scene kept), `503`, `504` after 300 s |
| `POST /close_preset_scene` **MUT** | none | `{success, message}` | `500` no demo open or restore incomplete, `504` after 30 s |
| `GET /hiz_demo/geometry_only_features` | none | `{"prefixes":["Shadow","VolumetricLight",...]}` | none. Constant data. |

Reply extras:
- `sponza` adds `imported_this_call` and `gta_path`.
- `hiz_demo` adds `profile`, `instanceCount`, `countClamped`, `clustersPerInstance`, `workItems`, `entities` (`camera`, `sun`, `wall`, each `"index/generation"`), `gpuDrivenScene` (`mode`, `collectStats`), `restoresOnClose`.
- `hiz_demo` clamps `count` to 1..100000. The `message` says so.
- With `geometry_only`, the demo also turns off the fixed-cost features listed by `/hiz_demo/geometry_only_features`.
- Close the demo to restore those features and settings.
- On `504`, the open may still be running. Check `/get_logs`.

```text
curl.exe -X POST http://127.0.0.1:8080/open_preset_scene -d "{\"preset\":\"hiz_demo\",\"count\":50000,\"spacing\":2,\"profile\":\"geometry_only\"}"
```

## Editor UI
These use `EditorUiCommandBridge`: one request at a time, 3 s timeout.

| Route | Params | Response | Errors |
|---|---|---|---|
| `GET /activate_tab` **MUT** | Q `name`* (exact window name) | `{success, activated_tab}` | `404` unknown name, `409` the editor layer does not know a name the registry knows |
| `GET /panel_visible` **MUT** | Q `name`* (any window name from `/list_tabs`), `visible`* | `{success, name, visible}` | `400` unknown name or bad `visible` |
| `GET /split_views` **MUT** | Q `enabled`* | `{success, split}` | `409` |
| `POST /spawn_instance_grid` **MUT** | B `count` (1 to 100000, default 6), `spacing` (0.1 to 1000, default 1.5) | `{success, instance_count}` | `400` range, `503` no Editor |
| `GET /select_asset` **MUT** | Q `path`* (absolute `.gta`) | `{success, selected_path}` | `400`, `503` |
| `GET /select_entity` **MUT** | Q `name`* (exact entity Name) | `{success, selected_entity}` | `400`, `404` unknown name, `503` no Editor |
| `GET /ui_mouse` **MUT** | Q `action`* (`move`, `down`, `up`, `release`), `x`, `y` (needed for `move`), `button` | `{success, action, button, x, y}` | `400`, `403` browser, `503` no Editor |
| `GET /inspector_state` | none | See below | `503` |
| `POST /inspector/build_clusters` **MUT** | B optional `weld`, `max_vertices`, `max_triangles`, `cone_weight` | `{success, started}` | `400` refused, `503` busy |
| `GET /hierarchy_state` | Q `first` (default 0), `count` (default 200, max 2000) | See below | `400`, `503` |
| `POST /hierarchy_action` **MUT** | B `kind`*, `target`*, `dragged`, `insertAfter` | `{success, kind, changed, firstRow, rows[]}` | `400`, `503` |
| `POST /exit` **MUT** | none | `{"success":true,"message":"exit requested"}` | `403` browser |

Notes:
- `activate_tab` and `panel_visible=true` open the window (Show): it is raised and, if it floats, centered. `panel_visible=false` closes it. Nothing is saved.
- `split_views`: `true` puts Game beside Scene. `false` makes Game a tab of Scene.
- `spawn_instance_grid` is a load-test spawner. It answers `503` if the editor layer is the null one.
- `inspector_state` shows what the Inspector drew last frame: `selection_kind`, `selected_path`, `shown_path`, `clusters_text`, `cluster_status`, `build_button_enabled`, `build_button_disabled_reason`, `build_running`, `clone_button_disabled_reason`, `clone_destination`, `clone_weld`, `open_viewer_after_clone`, and `rotation`.
- `inspector_state.rotation` is the Transform "Rotation" field: `drawn` (false if it was not drawn last frame), `entity` (`{index, generation}`), `euler_degrees` (`[x,y,z]` as shown), `axis_center` (three `[x,y]` pairs, screen pixels of the X, Y, Z drag fields, same space as `/ui_mouse`), `rederive_count`. `rederive_count` counts how often the shown Euler was rebuilt from the quaternion. It must stay constant while a value like 9999 is shown and nothing else edits the rotation.
- `ui_mouse` queues one synthetic mouse event into the editor's ImGui input. It is applied on the next UI frame, so the real widgets react. `action=move` needs `x` and `y` (screen pixels, within +/-10000000). `down` and `up` take an optional `button` (`left` default, `right`, `middle`). `release` lets go of all buttons and gives the pointer back to the real mouse. While a `move` is active the injected position is re-applied every frame. Always finish with `release`.
- Drag a Rotation axis: `select_entity`, read `axis_center` from `inspector_state`, then `ui_mouse` `move` to that center, `down`, `move` to center.x + N pixels (1 pixel = 0.1 degree), `up`, `release`. Wait one frame between calls and read `inspector_state` again.
- `inspector/build_clusters` is async. Read the result from `/inspector_state`. Limits are the same as `/build_clusters`.

`hierarchy_state` reply:
- `visible`, `entityCount`, `rowCount`, `collapsedCount`, `first`
- `rows[]`: `entity` (`"index/generation"`), `parent` (or `null`), `depth`, `siblingPosition`, `label`, `hasChildren`, `expanded`, `selected`
- `buildMs`, `flattenMs`, `drawnRows`, `rowsReused`. A hidden panel gives `null` for the timings, never 0.

`hierarchy_action` runs the panel's own drag-and-drop code:
- `kind` is `reparent`, `reorder` or `toggle`.
- An entity is `{"index":1,"generation":2}` or `"1/2"`.
- `toggle` needs only `target`. The others need `dragged` and `target`.
- `insertAfter` must be a boolean.

## Scene camera
This moves the **editor** Scene camera. Then `/scene_camera/image` or `/get_swapchain` shows the new view. All use `EditorUiCommandBridge`.

| Route | Params | Response | Errors |
|---|---|---|---|
| `GET /scene_camera/state` | none | `{rendered, image:{width,height}, selected_entity, cameras[], selected_frustum[], pose, view}` | `503` |
| `GET /scene_camera/pose` | none | `{success, position:{x,y,z}, yaw_degrees, pitch_degrees, view:{projection,distance,axis}}` | `503` |
| `GET /scene_camera/set_pose` **MUT** | Q `x`* `y`* `z`* (max 1e6), `yaw`* `pitch`* (max 1e5) | Same as `/pose` | `400` names the bad key |
| `GET /scene_camera/view` **MUT** | Q `direction` (`right`,`left`,`top`,`bottom`,`front`,`back`), `projection` (`perspective`,`orthographic`), `distance` (0.05 to 100000). At least one. | Same as `/pose` | `400` names the bad key |
| `GET /scene_camera/pick` | Q `x`* `y`* (0 to 1, fraction of the image) | `{success, hit, entity?}` | `409` not rendered |
| `GET /scene_camera/select` **MUT** | Q `name`* | `{success, selected}` | `404` unknown entity |
| `GET /scene_camera/image` **IMG** | Q `overlay` (`true` default, or `false`) | PNG | `409` |

- `set_pose` needs all five parameters. Each must be a finite number. It keeps the pivot distance. Pitch is clamped to +-90.
- `direction` looks along the axis around the pivot. `top` looks down (pitch 90). `front` looks along +Z (yaw 0).
- `distance` is applied first, then `direction`, then `projection`. `set_pose` keeps the distance.
- The gizmo is ImGui. `/scene_camera/image` does not contain it. Use `/get_swapchain`.
- Each `cameras[]` item: `entity`, `name`, `position`, `rotation_euler_degrees`, `fov_y_degrees`, `near`, `far`, `selected`, `screen` (`apex_x`, `apex_y`, `visible`).
- `overlay=true` draws the camera frustums on the image.

```text
curl.exe "http://127.0.0.1:8080/scene_camera/set_pose?x=0&y=5&z=-10&yaw=0&pitch=-15"
curl.exe "http://127.0.0.1:8080/scene_camera/view?direction=top&projection=orthographic"
curl.exe "http://127.0.0.1:8080/scene_camera/image?overlay=true" -o scene.png
```

## Assets and clusters
`/import_asset`, `/build_clusters` and `/clone_clusters` share one bridge: 120 s timeout, one request at a time.

| Route | Params | Response | Errors |
|---|---|---|---|
| `POST /import_asset` **MUT** | B `source_path`*, `destination_folder` (relative) | `{success, message, final_relative_path, final_absolute_path, guid, converted_to_mesh_asset, mesh_source_format, converted_to_ktx2, converted_to_motion_asset, mesh_vertex_count, mesh_triangle_count}` | `400` import failed, `503` no Project panel, `504` |
| `POST /build_clusters` **MUT** | B `gta_path`*, `weld`, `max_vertices`, `max_triangles`, `cone_weight` | `{success, weld, vertex_count_before, vertex_count_after, triangle_count, cluster_count, avg_triangles_per_cluster, avg_vertices_per_cluster, poor_fill, milliseconds}` | `400`, `503` busy, `504` |
| `POST /clone_clusters` **MUT** | B `gta_path`*, `weld` (`auto` default), `open_viewer`, same three limits | `{success, created_path, guid, weld_used, weld_fell_back, vertex_count_before, vertex_count_after, triangle_count, cluster_count, avg_triangles_per_cluster, milliseconds, message}` | `400`, `503`, `504` |

Cluster build limits (`/build_clusters`, `/clone_clusters`, `/inspector/build_clusters`):
| Param | Default | Range |
|---|---|---|
| `weld` | `exact` (`auto` for clone) | `exact`, `position_only` (`auto` for clone) |
| `max_vertices` | 64 | 3 to 255 |
| `max_triangles` | 124 | 4 to 512, multiple of 4 |
| `cone_weight` | 0.5 | 0 to 1 |

- `exact` merges identical vertices. It loses nothing.
- `position_only` merges by position and recomputes normals. It is refused for UV meshes.
- On `504`, the build or clone keeps running. It writes the file when done.
- `import_asset` and `instantiate_asset` chain well: use the returned `final_absolute_path` as `gta_path`.

## Cluster Viewer
All use `EditorUiCommandBridge`. Open the viewer first with `/cluster_viewer/open`.

| Route | Params | Response | Errors |
|---|---|---|---|
| `GET /cluster_viewer/open` **MUT** | Q `path`* (absolute `.gta`) | `{success, message, state}` | `409` no valid mesh |
| `GET /cluster_viewer/state` | none | See below | `503` |
| `GET /cluster_viewer/set` **MUT** | Q one or more setting pairs (max 64, no duplicate key) | `{success, message, state}` | `400` unknown key or bad value |
| `GET /cluster_viewer/pick` | Q `x`* `y`* (0 to 1) | `{success, hit}` | `409` |
| `POST /cluster_viewer/verify` **MUT** | none | Like `set`. `state.truth` is filled. | `409` |
| `POST /cluster_viewer/capture_hiz` **MUT** | none | Like `set`. Captures Hi-Z for the occlusion test. | `409` |
| `GET /cluster_viewer/image` **IMG** | Q `target` (`view` default, or `debug`), `overlay` (default `false`) | PNG | `409` not open |

`/cluster_viewer/state` keys: `open`, `path`, `valid`, `mesh`, `settings`, `cull`, `hiz`, `truth`, `debugCamera`, `clusterPathSupported`.
- `cull` has `visible`, `byFrustum`, `byCone`, `byOcclusion`, `runMilliseconds`, `occlusionApplied`, `error`.
- `truth.falseCulls > 0` means the occlusion test hid a cluster that is visible. That is a bug.

`/cluster_viewer/set` keys. A bad key or value rejects the whole request. The error names the allowed range.
| Keys | Values |
|---|---|
| `drawMode` | `normal`, `clusters`, `cull` |
| `culledStyle` | `hidden`, `dim` |
| `frustum`, `cone`, `occlusion`, `debugCamera`, `cameraPreview`, `cameraSelected`, `showSceneCamera`, `planes`, `copySceneCamera` | `true`, `false` |
| `camX`, `camY`, `camZ` | -1e7 to 1e7 |
| `camYaw`, `camPitch`, `camRoll` | -1e6 to 1e6 degrees |
| `camFov` | 1 to 170 |
| `camNear` | 1e-4 to 1e6 |
| `camFar` | above `camNear` x 1.01, up to 1e7 |
| `camAspect` | 0.1 to 10 |
| `orbitYaw` | -1e6 to 1e6 degrees |
| `orbitPitch` | -85 to 85 degrees |
| `orbitDistance` | above 0. The window clamps it. |
| `orbitTargetX`, `orbitTargetY`, `orbitTargetZ` | -1e7 to 1e7 |
| `encodeWidth` | 640, 1280, 1920 |

```text
curl.exe "http://127.0.0.1:8080/cluster_viewer/set?drawMode=cull&occlusion=true"
curl.exe -X POST http://127.0.0.1:8080/cluster_viewer/verify
```

## Project assembly
These routes are query-based. POST routes take `name` in the query, not a JSON body.

| Route | Params | Response | Errors |
|---|---|---|---|
| `GET /project_assembly/list_projects` | none | JSON array `[{name, tier}]` | `503` |
| `POST /project_assembly/create_project` **MUT** | Q `name`* | `{created_source_directory}` | `400` |
| `POST /project_assembly/create_asset` **MUT** | Q `kind`* (`render_pass`, `compute_shader`, `shader_pair`, `screen_post_process_pass`), `name`* | `{created_files[], reminder_message}` | `400` |
| `POST /project_assembly/open_project` **MUT** | Q `name`* | `{status_message, load_attempted, load_succeeded}` | `400` |
| `POST /project_assembly/hot_reload` **MUT** | Q `name`* | Status JSON (below). Waits up to 600 s. | `503` rejected or timed out |
| `GET /project_assembly/hot_reload/status` | none | `{phase, project_name, cycle_id, phase_elapsed_ms, last_outcome, last_error_message}` | `503` |
| `POST /project_assembly/debug/compile_only` **MUT** | Q `name`* | `{started, reason?}` | `400` |
| `GET /project_assembly/debug/ledger` | Q `name`* | `{project_name, render_pass_names[], render_feature_names[], panel_names[], component_type_names[]}` | `400` |
| `GET /project_assembly/debug/loaded_assemblies` | none | `{dll_file_names[]}` | `503` |
| `GET /project_assembly/debug/component_types` | none | `{type_names[]}` | `503` |
| `POST /project_assembly/debug/set_probe_marker_value` **MUT** | Q `value`* (integer) | `{success}` | `400` |

- `compile_only` returns at once. A second call during a build gets `started:false`. Poll `hot_reload/status`.
- `hot_reload` returns when the cycle ends. `200` does not mean success. Read `last_outcome` in the reply.
- `open_project` loads through a 5 s bridge. A slow load shows in `status_message`.
- `set_probe_marker_value` is for tests only.
- The guide for the whole flow is [Project assembly](../guides/project-assembly.md).

```text
curl.exe -X POST "http://127.0.0.1:8080/project_assembly/hot_reload?name=MyProject"
curl.exe http://127.0.0.1:8080/project_assembly/hot_reload/status
```

## Other routes
| Group | Where |
|---|---|
| `/profiler*`, `/debug/fault` | [HTTP routes: debug tools](http-routes-debug.md) |
| `/feature_debug/*` | [HTTP routes: debug tools](http-routes-debug.md) |
| `/frame_debugger/*` | [HTTP routes: debug tools](http-routes-debug.md) and [Frame debugger](frame-debugger.md) |
| `/render_graph/passes`, `/render_graph/set_*` | [HTTP routes: debug tools](http-routes-debug.md) |

## Gotchas
- A `504` on a change does not mean "not done". Read the state first.
- Send calls to the same bridge one by one. Parallel calls give `503`.
- `/get_game_view` and `/get_swapchain` return text errors. Do not parse them as JSON.
- `?format=garbage` returns a PNG. It does not fail.
- Log `level` in JSON is `Warning`. The query `min_level=warning` takes any case.
- The editor must run one instance. A second one has no HTTP.

## Related
- [HTTP cheat sheet](http-cheat-sheet.md)
- [HTTP routes: debug tools](http-routes-debug.md)
- [HTTP server](../architecture/http-server.md)
- [Logging](logging.md)
- [Project assembly](../guides/project-assembly.md)
