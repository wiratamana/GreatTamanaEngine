# Troubleshooting
> Symptom, cause and fix for the most common problems: build, start, HTTP, rendering, projects and tests.
> Verified against code: 2026-10-30. HTTP behaviour is read from code, not from a live run.

## TL;DR
- Build problems: almost always the toolchain. Only MinGW-w64 and Ninja work.
- `503` means busy. `504` means slow. Neither means "failed". Read the state, then retry one call at a time.
- Rendering problems: ask the engine. Use `/get_blocking_errors`, `/render_graph` and `/get_game_view` before you guess.
- Still stuck: read the Log panel, or `GET /get_logs?min_level=warning`.

## Build and configure
| Symptom | Cause | Fix |
|---|---|---|
| Configure stops: `glslc` not found | `glslc` is not on `PATH` | Install the Vulkan SDK or `scoop install shaderc`. Open a new shell. |
| Linker rejects `-Wl,--whole-archive` | Compiler is not MinGW-w64 | Use MinGW-w64 `g++` and Ninja. MSVC does not work. |
| `--config Debug` changes nothing | Ninja is single-config | Configure again with `-DCMAKE_BUILD_TYPE=Debug`. |
| Dependency fetch fails | No internet or a proxy | Fix the network. Run configure again. |
| Sponza missing, only a warning | No `git` or no network | Ignore it, or set `-DGTE_FETCH_SPONZA=OFF`. |
| Two clones build different code | Many pins are `latest` | Pin with `-D<NAME>_RELEASE_TAG=<tag>`. See [Building](../BUILDING.md). |
| `ctest` lists no tests | `GTE_BUILD_TESTS=OFF` | Configure with `-DGTE_BUILD_TESTS=ON`. |
| CMake warns about `libstdc++-6.dll` | Project DLLs need the shared C++ runtime | Put the shared runtime next to the compiler. See [Project assembly](guides/project-assembly.md#before-you-start). |

## Start and run
| Symptom | Cause | Fix |
|---|---|---|
| Editor runs, no HTTP | Port `8080` is taken. The log says `failed to bind` (category `Network`). | Close the other editor. One instance can serve. |
| Editor runs, no HTTP at all | Built with `GTE_ENABLE_NETWORK=OFF` | Configure with it `ON`. |
| Window stops drawing after a GPU error | Device lost. `Core` logs one line and stops rendering. | Restart the editor. |
| Need Vulkan validation messages | Validation is off by default | Start with `--vulkan-validation`. |
| Benchmark numbers jump | Vsync snaps FPS to refresh steps | Start with `--no-vsync`. Compare milliseconds. |

## HTTP
| Symptom | Cause | Fix |
|---|---|---|
| `Connection refused` | Editor is down or HTTP failed to bind | Start one editor. Check `/http_hello_world`. |
| `503 ... already in progress` | Another request is in the same bridge | Send calls one by one. |
| `503` on `/get_texture` or `/get_game_view` | GPU busy, device lost, or no bridge | Retry once. If it stays, restart the editor. |
| `504` | Main thread did not answer in time. The work may still run. | Read state before you retry a write. Check `/profiler` (`live_block`). |
| `403` | The browser guard refused the request | Use `curl.exe` or a script. Send no `Origin` or `Host` header. |
| `409` on `/get_game_view` | The Game view has no target (tab hidden) | Click the Game tab. |
| `409` on `set_pass_enabled` | The pass is deny-listed (`Present`, `ClearViewTarget`) | Pick another pass. |
| `404` on `get_event_texture` | That draw index is not captured yet | Poll until a later frame is captured. |
| `400` | Bad param, bad JSON or unknown key | Read `error`. It names the key and range. |
| PowerShell breaks the URL or JSON | `curl` is an alias. `&` is special. | Use `curl.exe`. Quote the URL. Or use `--data-binary "@body.json"`. |
| Image route returns text, not PNG | These two routes fail with plain text | Read the text. It names the cause. |
| Old log lines are gone | The ring holds 2000 entries. Default `limit` is 200. | Pass `limit=0`. Filter with `min_level` or `category`. |
| `/profiler` says `no_snapshot` | No frame copied yet. Not an error. | Retry. |
| `/profiler` says `compiled_out` | Built with `GTE_ENABLE_PROFILER=OFF` | Configure with it `ON`. |

More on codes: [HTTP cheat sheet](debugging/http-cheat-sheet.md#4-status-codes).

## Rendering
| Symptom | Cause | Fix |
|---|---|---|
| Pass missing in `/render_graph` | It is disabled, or culled (nobody reads its output) | Look in `/render_graph/passes`. Check `is_culled`. |
| `set_pass_enabled` gave `200`, nothing changed | A typo made a ghost entry | Check `ever_declared_this_session:true` in `/render_graph/passes`. |
| Whole scene is black | Sun below the horizon, or a `DirectionalLight` pointing up | Fix the light. See [Scene lighting](reference/render-features/scene-lighting.md). |
| Single black pixels | Radiance above 65504 overflows `RGBA16F` to `+inf` | Clamp to `kMaxSceneRadiance` (8192) in the shader. See [PBR materials](conventions/pbr-materials.md). |
| Colors look raw and dark | `ToneMapping` is off | Turn it on. It makes the display image. See [Tone mapping](reference/render-features/tone-mapping.md). |
| Feature status `NotRendered` | The view did not render for a few frames, or the feature or pass is off in that view | Scene view defaults: see [Render features](reference/render-features/README.md#scene-view-defaults). |
| Feature setting rejected | `set` is all or none. One bad key gives `400`. | Fix the key. Read the `error` text. |
| Settings gone after a scene load | Settings live in memory. Scenes do not save them. | Set them again. |
| `inertReason` is `capture armed` | The Frame Debugger is on | `GET /frame_debugger/enable?value=false`. Playback stays paused. |
| Culling seems off | `mode` is `NoCull`, or `inertReason` is not `none` | `GET /feature_debug/state?name=GpuDrivenScene`. See [GPU-driven scene](reference/render-features/gpu-driven-scene.md). |
| Hi-Z not used | `hizReason` is `Unavailable`, `TooLarge` or `NothingInView` | Read the reason. It says why the view skipped Hi-Z. |
| Log shows `RenderPassHonesty` | A pass runs although its toggle says off | Fix the pass name at the declaration. See [Render honesty](conventions/render-honesty.md). |
| Log shows `FrameDebuggerCoverage` or `FrameDebuggerSideChannel` | A pass is not a leaf, or hidden data leaks while off | Same doc. Follow the clause in the message. |
| Frame Debugger tree is empty | Capture takes two frames | Poll `/frame_debugger/state` for `hasCapturedFrame:true`. |
| One bad frame, then fine | The offscreen graph threw. Logged once per message. | `GET /get_logs?min_level=error`. |

## Projects and hot reload
| Symptom | Cause | Fix |
|---|---|---|
| `New Project` refuses | `GTE_ENABLE_PROJECT_ASSEMBLIES=OFF`, bad name, or folder exists | Read the message. Pick a new valid name. |
| `Open Project` does not load | Tier is `NotCompiled` or `NotBuildable` | Compile first. Open again. |
| Compile fails with a permission error | The loaded DLL is locked by Windows | Use hot reload, not Compile. |
| Panel or pass missing after load | Not registered in `RegisterProject` | Add the call. Check `/project_assembly/debug/ledger`. |
| `last_outcome` is `RolledBack` | Build or load failed. The old DLLs were restored. | Read `last_error_message` and the log. Fix the code. Try again. |
| `last_outcome` is `CriticalFailure` | Rollback failed. The project is unloaded. | Fix by hand. Open the project again. |
| `hot_reload` answers `503` | A build is running, or the wait ended | Poll `/project_assembly/hot_reload/status`. The cycle may still run. |
| Editor frozen during hot reload | Hot reload blocks the main thread on purpose | Wait. Poll status from a second connection. |
| Entities have new IDs after reload | IDs change on every hot reload | Look entities up by name. |

## Tests
| Symptom | Cause | Fix |
|---|---|---|
| A GPU test is skipped | No usable Vulkan device | Expected. Run on a machine with a GPU. |
| A guard check fails | Code broke a rule, for example Vulkan outside `src/Renderer/` | Fix the code. Never add to an allowlist. See [Testing](../TESTING.md#guard-checks). |
| Test exe cannot start | `SDL3.dll` is missing next to it | Build again. The build copies it. |

## Related
- [HTTP cheat sheet](debugging/http-cheat-sheet.md) · [HTTP server](architecture/http-server.md)
- [Building](../BUILDING.md) · [Testing](../TESTING.md)
- [Frame debugger](debugging/frame-debugger.md) · [Profiling](debugging/profiling.md)
- [Glossary](glossary.md) · [Documentation index](README.md)
