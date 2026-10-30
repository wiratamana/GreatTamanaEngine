# HTTP server

> How the embedded debug HTTP server starts, which thread runs what, and how to add a route. For engine developers.
> Verified against code: 2026-10-30

## TL;DR
- The editor runs one HTTP server on `127.0.0.1:8080`. The port is fixed in code.
- Handlers run on a thread pool, never on the main thread.
- A handler never touches engine state. It sends a request through a **bridge** and waits.
- The main thread answers each bridge once per frame. One request fits in a bridge at a time.
- A busy bridge gives `503`. A slow main thread gives `504`.

## Diagram
```text
 client (curl, AI tool)
        |  HTTP, loopback only
        v
 httplib listener thread --> httplib thread pool (>= 8 threads)
                                   |
              parse + validate (NetworkRoutes.h, pure functions)
                                   |
                     bridge.SubmitAndWait(request)
                                   |   mutex + condition variable
                                   v
 main thread (EditorHost frame loop): TryPeekPendingCommand -> do work -> FulfillCommand
                                   |
                     handler builds JSON / PNG and replies
```

## How it works
1. `EditorHost` builds all bridges, then builds `NetworkServer`. Routes are registered once in the `NetworkServer` constructor.
2. Near the end of the `EditorHost` constructor, it calls `Start(8080)`.
3. `Start` binds `127.0.0.1` only. `Start(0)` picks a free port, but only tests use it.
4. A request arrives on a pool thread. A pure function parses it. A bad request gets `400` here.
5. The handler calls `SubmitAndWait`. It blocks until the main thread answers or the timeout ends.
6. The main thread peeks the bridge once per frame, does the work, and calls `FulfillCommand`.
7. The handler turns the result into JSON or a PNG and sets the status code.

## Start and stop
| Item | Fact |
|---|---|
| Library | cpp-httplib 0.54.1 in `third_party/httplib`. `Stop()` runs in the destructor. |
| Bind address | `127.0.0.1` only. `Start` has no host parameter. |
| Port | `8080`. No CLI flag, no env var, no config. |
| Build switch | CMake option `GTE_ENABLE_NETWORK` (default `ON`). `OFF` means no socket at all. |
| Bind failure | Not fatal. Logs `failed to bind` (category `Network`). A second editor instance hits this. It runs with no HTTP. |
| Missing bridge | Every route answers `503` when its bridge or capability is null. |

## Threads
| Thread | Role |
|---|---|
| Listener | Owned by `NetworkServer`. Accepts connections. |
| Pool | httplib workers. At least 8, up to 4x. Handlers can run in parallel. |
| Main | Runs the frame loop. Answers bridges once per frame. |

- Different bridges run in parallel. The same bridge does not: the second request gets `503`.
- If the main thread stalls, every bridge route ends in `504`.
- Image routes for the Scene camera and Cluster Viewer encode the PNG on the pool thread, not in the frame.

## Bridges and timeouts
| Bridge | Routes | Timeout | Slots |
|---|---|---|---|
| `FrameCaptureBridge` | `/get_game_view`, `/get_swapchain`, `/get_texture`, `/get_window` | 3 s | 1 per kind (4 kinds) |
| `EngineCommandBridge` | `/instantiate_*`, `/delete_entity`, `/set_entity_trs`, `/save_scene`, `/load_scene`, `/project_assembly/debug/scene_snapshot` | 3 s | 1 |
| `EngineCommandBridge` (long) | `/open_preset_scene` 300 s, `/close_preset_scene` 30 s | custom | shares the slot |
| `EditorUiCommandBridge` | tabs, panels, inspector, hierarchy, `/cluster_viewer/*`, `/scene_camera/*`, `/exit` | 3 s | 1 |
| `AssetImportCommandBridge` | `/import_asset`, `/build_clusters`, `/clone_clusters` | 120 s | 1 |
| `FrameDebuggerCommandBridge` | `/frame_debugger/*` | 3 s | 1 |
| `RenderGraphControlCommandBridge` | `/render_graph/passes`, `/render_graph/set_*` | 3 s | 1 |
| `FeatureDebugCommandBridge` | `/feature_debug/*` | 3 s | 1 |
| `ProjectLifecycleLoadCommandBridge` | `/project_assembly/open_project` (load step) | 5 s | 1 |
| `ProjectAssemblyHotReloadCommandBridge` | `/project_assembly/hot_reload` | 600 s | 1 |
| Profiler bridge | `/profiler*` | see [debug routes](../debugging/http-routes-debug.md) | 1 |
| No bridge | `/get_logs`, `/get_blocking_errors`, `/clear_logs`, `/list_tabs`, `/render_graph`, `/list_textures`, `/hiz_demo/geometry_only_features`, most `/project_assembly/*` reads | none | n/a |

`SyncCommandBridge` (`src/Application/SyncCommandBridge.h`) is the shared template. Default timeout is 3 s.

## Status codes
| Code | Meaning here |
|---|---|
| `200` | OK |
| `400` | Bad or missing parameter, bad JSON, or a refused command |
| `403` | Browser request refused by the guard |
| `404` | Unknown name (tab, entity, feature, event image not captured yet) |
| `409` | Request is valid, but the state does not allow it |
| `500` | Environment failure, for example scene save I/O |
| `503` | Bridge missing, bridge busy, GPU busy, device lost, build busy |
| `504` | Main thread did not answer in time. The work may still run. |

Capture failures map as follows: target not available `409`, timeout `504`, GPU busy `503`, device lost `503`.

## Browser guard
Only four cases check the headers: `POST /exit`, `POST /profiler/set`, `POST /profiler/export`, and `/debug/fault` when it arms a fault.

The guard answers `403` when any of these is true:
- `Sec-Fetch-Site` is present and is not `none` or `same-origin`.
- `Origin` host is not `127.0.0.1` or `localhost`.
- `Host` is present and is not `127.0.0.1:<port>` or `localhost:<port>`.

`curl`, Python and the AI tools send none of these headers, so they pass. Other mutating routes have no guard. Loopback is their only protection.

## Rules
1. Never touch the ECS, renderer or ImGui from a handler. Use a bridge.
2. Put all parsing and JSON building in `src/Network/NetworkRoutes.h` as pure functions.
3. Never put response text inline in a lambda in `NetworkServer.cpp`.
4. Validate strictly. Reject `12abc`. Reject unknown keys. Never guess.
5. Answer `503` when the bridge is null. Never crash.
6. Add no auth, no CORS and no other bind address. This is a loopback dev tool.

## How to add a route
1. Add the parse function and the JSON builder to `NetworkRoutes.h` and `NetworkRoutes.cpp`.
2. Add a unit test in `tests/Network/NetworkRoutesTests.cpp` (or a sibling file).
3. If the route needs engine state, add a request kind to a bridge. Pick the bridge that owns that state.
4. Handle the new kind where the main thread polls that bridge in `src/Editor/EditorHost.cpp`.
5. Add one `server.Get` or `server.Post` line in `RegisterRoutes` in `src/Network/NetworkServer.cpp`.
6. Map `alreadyPending` to `503` and `timedOut` to `504`.
7. Add an end-to-end test in `tests/Network/`, then list the route in the [route reference](../debugging/http-routes.md).

If the route returns an image, reuse the shared format rules in the [image section](../debugging/http-routes.md#images).

## Gotchas
- A `504` on a mutation does not mean "not done". Read the state before you retry.
- `/get_game_view` and `/get_swapchain` fail with plain text. Other routes fail with JSON.

## Related
- [HTTP cheat sheet](../debugging/http-cheat-sheet.md)
- [HTTP routes](../debugging/http-routes.md)
- [HTTP routes: debug tools](../debugging/http-routes-debug.md)
- [Logging](../debugging/logging.md)
- [Architecture overview](overview.md)
