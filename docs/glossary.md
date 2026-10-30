# Glossary
> One line per term used in the docs and the code. For anyone who meets a new word and wants the short answer.
> Verified against code: 2026-10-30

## TL;DR
- Terms are in A to Z order. The last column links to the doc that owns the term.
- Names in `code font` are real symbols, routes or JSON keys.
- Renderer code says `Primary` / `Secondary`. The editor says Game / Scene. They are the same two views.

## Terms
| Term | Meaning | Owner doc |
|---|---|---|
| Blackboard | Per-frame key and value store. Passes publish data to it, later passes read it. Cleared every frame. | [Render graph](architecture/render-graph.md) |
| Bridge | A one-slot mailbox between an HTTP thread and the main thread. A second request gets `503`. | [HTTP server](architecture/http-server.md) |
| Browser guard | Check on a few POST routes. It answers `403` to requests that look like they come from a web page. | [HTTP server](architecture/http-server.md) |
| `capture armed` | A `GpuDrivenScene` `inertReason`. The Frame Debugger is on, so the GPU cull path is off for that view. | [GPU-driven scene](reference/render-features/gpu-driven-scene.md) |
| Channel | A named hand-off of data between two features, on the blackboard. Features never include each other. | [Feature kit](reference/feature-kit.md) |
| Cluster | A small group of triangles (by default up to 64 vertices and 124 triangles) with bounds and a normal cone. The unit of GPU culling. | [Mesh clusters](architecture/mesh-clusters.md) |
| Composite (PostComposite) | The last stage. Features blend their result into the view color in priority order. `ToneMapping` runs last. | [Rendering](architecture/rendering.md) |
| `Core` | The engine facade in `gte_core`. Frame calls: `Update()`, `BuildFrame()`, `Present()`. No SDL, no ImGui. | [Overview](architecture/overview.md) |
| Culled pass | A pass whose output nobody reads. The graph drops it with no error. `/render_graph` shows `is_culled`. | [Render graph](architecture/render-graph.md) |
| Deny-listed pass | `Present` and `ClearViewTarget`. `set_pass_enabled` on them gives `409`. | [Render honesty](conventions/render-honesty.md) |
| `EditorHost` | The host. It owns the window, `Core`, the editor UI, the HTTP server and the main loop. | [Overview](architecture/overview.md) |
| Entity | An 8-byte handle: `index` and `generation`. Never a pointer. IDs change after a hot reload. | [ECS](architecture/ecs.md) |
| `fx` (Feature Kit) | `gte::fx`. The Vulkan-free API that render features are written against. | [Feature kit](reference/feature-kit.md) |
| Feature (render feature) | A folder in `src/Features/` with passes, settings, a panel and a debug endpoint. 8 ship today. | [Render features](reference/render-features/README.md) |
| Feature off vs pass off | `set_feature_enabled` skips the whole feature. `set_pass_enabled` switches one pass name. | [Render features](reference/render-features/README.md) |
| Frame Debugger | Editor window and routes. It freezes one Game view frame and shows its draw events. | [Frame debugger](debugging/frame-debugger.md) |
| Froxel | A cell of a 3D grid that follows the camera frustum. Volumetric light uses 160x90x64 froxels. | [Volumetric light](reference/render-features/volumetric-light.md) |
| `.gta` | The engine asset container. 64-byte header, metadata, payload. One Guid per file. | [Asset pipeline](architecture/asset-pipeline.md) |
| `.gtscene` | A scene as JSON, format version 2. | [Scene serialization](architecture/scene-serialization.md) |
| Ghost entry | A pass name in the toggle registry that no pass ever declared. A typo in `set_pass_enabled` makes one. | [HTTP cheat sheet](debugging/http-cheat-sheet.md) |
| Game view / Scene view | The two rendered views. Game is the camera view. Scene is the editor free camera. | [Rendering](architecture/rendering.md) |
| GPU-driven | Clusters are culled by a compute pass. One indirect draw renders the survivors. The CPU path stays complete. | [GPU-driven rendering](architecture/gpu-driven-rendering.md) |
| Guard check | A ctest check that enforces a code rule, such as `VolkLeakCheck`. Never add to its allowlist. | [Contributing](conventions/contributing.md) |
| Hi-Z | A depth pyramid. The cull pass uses it to drop clusters hidden behind others. `mode=HiZ`. | [GPU-driven rendering](architecture/gpu-driven-rendering.md) |
| `hizReason` | Why Hi-Z was skipped for a view: `Unavailable`, `TooLarge`, `NothingInView`. | [GPU-driven scene](reference/render-features/gpu-driven-scene.md) |
| Honesty (render honesty) | A pass's enabled state, the Frame Debugger tree and side effects must agree. Three detectors check it. | [Render honesty](conventions/render-honesty.md) |
| Hot reload | Unload, rebuild and reload a project DLL, then restore the ECS world. HTTP only. It freezes the main thread. | [Project assembly](guides/project-assembly.md) |
| `inertReason` | Why the GPU cluster path did nothing: `device caps`, `shared scope`, `capture armed`, `toggled off`, `invalid state`. | [GPU-driven scene](reference/render-features/gpu-driven-scene.md) |
| Job | A batch of pure work on a worker thread. No `Registry` writes, no GPU, no ImGui. | [Job system](conventions/job-system.md) |
| Ledger | The list of passes, features, panels and component types one project DLL registered. The host uses it to unload the project. | [Project assembly](guides/project-assembly.md) |
| `live_block` | Profiler list of waits in progress now. The best field when the editor looks frozen. | [Profiling](debugging/profiling.md) |
| Main thread | The one thread that owns the ECS, the GPU and ImGui. | [Overview](architecture/overview.md) |
| Metric object | A profiler number with a `status`. `value:null` means not measured. It is never `0`. | [Profiling](debugging/profiling.md) |
| Offscreen graph / present graph | The two render graphs of a frame. Offscreen draws the views. Present blits and draws the UI. | [Rendering](architecture/rendering.md) |
| One-shot (`rg::OneShot`) | A render graph chain run outside the frame loop. Used for editor previews and one-time work, never per frame. | [Render graph](architecture/render-graph.md) |
| Pass | One unit of GPU work in the render graph: graphics, compute or transfer. Has a stable name and a toggle. | [Render graph](architecture/render-graph.md) |
| Persistent target / volume | A feature-owned texture kept per view across frames. A volume is 3D and its contents do not carry over. | [Volumetric resources](conventions/volumetric-resources.md) |
| Priority | Order inside one stage. The lower number runs first. | [Render features](reference/render-features/README.md) |
| Project assembly | A game project built as `<Name>_Game.dll` and `<Name>_Editor.dll`, loaded by the editor. | [Project assembly](guides/project-assembly.md) |
| Provider | Code that declares a group of passes into the graph, such as `RenderOpaque`. | [Rendering](architecture/rendering.md) |
| `rg` (render graph) | `gte::rg`. The pass declaration API. The graph orders, culls, syncs and records passes. | [Render graph](architecture/render-graph.md) |
| `Registry` | The ECS world. Not thread-safe. Only the main thread writes it. | [ECS](architecture/ecs.md) |
| SDSM | Sample Distribution Shadow Maps. Shadow cascades fit the depth range that is really visible. | [Shadow](reference/render-features/shadow.md) |
| Spike | A frame that took much longer than normal. `/profiler/spikes` lists them. | [Profiling](debugging/profiling.md) |
| Stage | Where a feature runs in a frame: `PreOpaque`, `Opaque`, `PostOpaque` or `PostComposite`. | [Render features](reference/render-features/README.md) |
| Tier (project) | State of a project folder: `NotAProject`, `NotBuildable`, `NotCompiled`, `Compiled`, `AlreadyLoaded`. | [Project assembly](guides/project-assembly.md) |
| Tone mapping | The last pass. It turns linear HDR color into display values 0..1. | [Tone mapping](reference/render-features/tone-mapping.md) |
| Toggle | A runtime on/off switch for a pass or feature. Kept in memory. A restart resets it. | [HTTP cheat sheet](debugging/http-cheat-sheet.md) |
| Verdict | Profiler summary of what limits the frame: `cpu_limited`, `gpu_limited`, `present_limited`, `stalled`, `unreliable`. | [Profiling](debugging/profiling.md) |
| Verlet chain | A bone chain moved by Verlet integration. Used for hair and skirts. Not a rigid-body engine. | [Animation and physics](architecture/animation-physics.md) |
| View background | A sky or backdrop drawn by a fullscreen pass after the opaque scene. The Frame Debugger can replay it. | [Feature kit](reference/feature-kit.md) |

## Related
- [Documentation index](README.md) · [Troubleshooting](troubleshooting.md)
- [HTTP cheat sheet](debugging/http-cheat-sheet.md) · [Architecture overview](architecture/overview.md)
