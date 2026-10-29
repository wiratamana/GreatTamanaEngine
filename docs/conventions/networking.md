# Networking

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

`src/Network/` (`NetworkRoutes.h/.cpp`, `NetworkServer.h/.cpp`) is the
engine's first real network I/O - a single embedded, loopback-only HTTP
server (`gte::Network::NetworkServer`, built on the already-vendored
cpp-httplib - see `cmake/FetchHttplib.cmake`), auto-started by
`Application`'s constructor (gated by the `GTE_ENABLE_NETWORK` CMake
option, default ON - see `task_manager/network-impl-1/PHASE0_MASTER_STRATEGY.md`
for the full campaign writeup). Follow these rules whenever touching this
module or adding a new endpoint:

- **The server only ever binds to `127.0.0.1` (loopback) - never a
  LAN-visible interface.** This is enforced by `NetworkServer::Start(int
  port)`'s own signature - there is no `host` parameter anywhere in its
  public API for a caller to override upward, so the constraint holds by
  construction, not by convention (see PHASE0's own "Locked Design
  Decisions"). A future requirement to expose this remotely needs a fresh,
  explicit design/security review first, not a one-line signature change.
- **Every registered route handler runs on NetworkServer's own dedicated
  background `std::thread` (via `httplib::Server::listen_after_bind()`),
  NEVER the main thread - and, symmetrically, nothing outside
  `NetworkServer.cpp` may call into `httplib::Server` directly.** This is
  a NEW background thread, structurally similar to (but independent from)
  the Job System's own worker-thread pool (see [Job System](job-system.md) above) - the
  exact same category of problem applies: this thread runs completely
  unsynchronized with the main thread's own frame loop, so it must never
  read OR write any engine-owned mutable state without a dedicated,
  reviewed thread-safe bridge.
- **A route handler must be a PURE function of its own request data only -
  it must NEVER touch `Registry`/`Renderer`/`Game`/`AssetDatabase`/
  `IEditorLayer`/ImGui/any other engine subsystem, directly or indirectly,
  full stop.** Every single row of the Job System's own Phase 4
  thread-safety classification table (see [Job System](job-system.md) above) that says
  **NEVER** for a job body applies at least as strongly here - none of
  those subsystems were built with ANY concurrent access in mind, and this
  network thread has no more special standing than an arbitrary job body
  would. `NetworkRoutes.h`'s own convention (every handler is a small, pure
  function with no engine-side parameter at all, e.g. `HandleHelloWorld()`)
  is what makes this rule trivially satisfiable today - a future endpoint
  that genuinely needs engine data (e.g. "how many entities are in the
  scene") needs a dedicated, reviewed, thread-safe bridge built first (e.g.
  a fixed-size, mutex-guarded command/snapshot queue the main thread drains
  once per frame, mirroring `Jobs::detail::JobQueue`'s own fixed-capacity,
  mutex-guarded shape) - never a raw pointer/reference into live engine
  state handed to a handler lambda.
- **`FrameCaptureBridge` (`src/Application/FrameCaptureBridge.h/.cpp`,
  `network-impl-2` campaign) is the ONE sanctioned exception to the rule
  above.** A route handler may call `FrameCaptureBridge::
  RequestCaptureAndWait()` and nothing else engine-side - it never reaches
  into `Renderer`/`Registry`/`Game`/`AssetDatabase` directly, even
  indirectly through this bridge; the bridge itself only ever moves
  already-produced, plain `CapturedPngImage` byte buffers, never a live
  pointer/reference. `Application::Run()` is the ONLY thing that ever calls
  `IsCaptureRequested()`/`FulfillPendingRequest()`/`FailPendingRequest()`,
  once per frame, from the main thread. A future endpoint needing DIFFERENT
  engine data must NOT extend this class's `FrameCaptureKind` enum for an
  unrelated purpose - build its own small, similarly-reviewed, similarly-
  narrow bridge instead, following this one's shape (see
  `task_manager/network-impl-2/PHASE2_CROSS_THREAD_FRAME_CAPTURE_BRIDGE.md`).
- **`GET /get_swapchain`/`GET /get_game_view`** (`network-impl-2` campaign,
  `task_manager/network-impl-2/PHASE0_MASTER_STRATEGY.md`) are this engine's
  first engine-state-touching endpoints, built entirely on top of
  `FrameCaptureBridge` (above) - `src/Encoding/` (`Base64.h`/
  `PixelConversion.h`/`PngEncoder.h`, Tier 1, always-compiled, Vulkan-free)
  provides the base64/BGRA→RGBA/PNG-encode primitives both routes share;
  `Renderer::CaptureRenderTexturePixels()` (a synchronous readback,
  piggybacking on the already-synchronous offscreen Game-view regime) and
  `SwapchainCaptureService` (`src/Renderer/SwapchainCaptureService.h/.cpp`, a
  pipelined, frame-in-flight-aware swapchain readback with zero added GPU
  stall) are the two capture mechanisms feeding it. Both routes accept an
  optional `?format=` query parameter (`png` - the default, raw bytes,
  `Content-Type: image/png` - or `base64`/`json`, a
  `{"width":...,"height":...,"format":"png","data_base64":"..."}` JSON
  envelope) or an `Accept: application/json` header when `?format=` is
  omitted - `Network::ResolveCaptureResponseFormat()`/
  `BuildCaptureJsonBody()` (`src/Network/NetworkRoutes.h`) are the one,
  shared, Tier-1-tested implementation of this precedence, reused verbatim
  by both routes' shared `RegisterCaptureRoute()` helper
  (`NetworkServer.cpp`) - never reimplemented per-route. A future THIRD
  capture kind (e.g. a Scene-view endpoint) should reuse
  `Renderer::CaptureRenderTexturePixels()` directly (it already works for
  ANY `RenderTexture`, not just the Game view) and extend
  `FrameCaptureKind`/`RegisterCaptureRoute()` rather than duplicating any of
  this.
- **Every one of `NetworkServer`'s own failure modes (a bind failure, e.g.
  the port already being in use) is NON-FATAL - log to stderr and continue,
  never throw/crash/abort engine startup.** This is a debugging/tooling
  aid layered on top of the engine, not a required subsystem the engine
  cannot run without (the same spirit `cmake/FetchHttplib.cmake`'s own
  header comment already states) - a developer running two instances of
  the engine at once, or a port already held by something else, must never
  be the reason the engine window itself fails to open. This exact path
  (a bind collision leaving `IsRunning() == false`) has an automated
  regression test - see Phase 4's `NetworkServerTests.cpp`.
- **`NetworkServer::Start()`/`Stop()` are NOT thread-safe against each
  other or against themselves (no internal mutex) - by design, since
  `Application` is their only caller, and it only ever calls both from the
  main thread.** A future caller from a different thread (e.g. a future
  Editor "Network" panel's Start/Stop button, if that panel itself isn't
  already guaranteed to run on the main thread the way every other Editor
  panel does - see [Editor Module Structure](editor-module-structure.md)) must not assume this is safe
  without adding real synchronization first.
- **`GTE_ENABLE_NETWORK` follows the exact same "class always compiles,
  only the production call site is gated" precedent as
  `GTE_ENABLE_JOB_SYSTEM`/`GTE_ENABLE_PROFILER` (see the
  [Job System](job-system.md) and [Profiling](profiling.md) sections above).**
  `NetworkServer`/`NetworkRoutes` compile and their tests pass identically
  whether the switch is ON or OFF - turning it OFF only skips
  `Application`'s own `m_networkServer.Start(...)` call, so a build with it
  OFF never opens a socket at all, at zero runtime cost.
- **POST support now exists (`network-impl-3` campaign - see
  `task_manager/network-impl-3/PHASE0_MASTER_STRATEGY.md`), and the existing
  "pure function of its own request data" rule extends to it unchanged.** A
  POST route handler parses its own request BODY (via `NetworkRoutes.h`'s
  `ParseInstantiatePrimitiveRequest()`/`ParseDeleteEntityRequest()`) but still
  never touches `Registry`/`Renderer`/`Game` directly - it only ever calls
  `EngineCommandBridge::SubmitAndWait()`, this campaign's own sanctioned
  bridge, exactly mirroring `FrameCaptureBridge`'s existing "the ONE
  sanctioned exception" precedent bullet above, just for a SECOND, independent
  bridge.
- **`EngineCommandBridge` (`src/Application/EngineCommandBridge.h/.cpp`) is
  the cross-thread bridge for ECS-MUTATING network requests** - contrast
  directly with `FrameCaptureBridge`'s read-only/produce-bytes shape: a
  SINGLE GLOBAL slot (not one per kind - a locked, deliberate design choice,
  see `network-impl-3`'s own `PHASE0_MASTER_STRATEGY.md`), carrying a real
  caller-supplied request payload (`EngineCommandRequest`, tagged by
  `EngineCommandKind`) and a real success/failure outcome
  (`EngineCommandResult`). `Application::Run()` drains at most ONE pending
  command per frame, via `TryPeekPendingCommandRequest()`/`FulfillCommand()`,
  EARLY - right after SDL input-event polling, BEFORE `Game::Update()` runs -
  a deliberate ordering choice (unlike `FrameCaptureBridge`'s own checks,
  which run later, interleaved with rendering) so a network-spawned/deleted
  entity is fully consistent for the rest of that exact frame (simulated,
  animated, and rendered as if it had always been there).
- **`POST /instantiate_primitive`** spawns one of the engine's 5 built-in
  primitive shapes (`cube`/`sphere`/`capsule`/`cone`/`plane`, case-insensitive)
  as a new `Transform`+`MeshRenderer`+`Name` entity - `Game::InstantiatePrimitive()`
  (`src/Game/Game.h/.cpp`) - given a JSON body of `shape`/`name`/
  `world_position`/an optional `parent` (looked up BY NAME). The entity's
  display name is auto-de-duplicated Unity-style (`"Cube"`, `"Cube (1)"`,
  `"Cube (2)"`, ... - `ECS/EntityQuery.h`'s `MakeUniqueEntityName()`) and
  returned as `resolvedName`; an unresolvable `parent` name is a non-fatal
  warning (`parent_requested_but_not_found: true`), never a failure - the
  entity is still created, just left unparented. Responds `200` on success
  (even with that warning) or `400` for an unrecognized shape name/malformed
  JSON. **`POST /delete_entity`** destroys a live entity (and every
  descendant of it, via the existing `ECS/TransformHierarchy.h`'s
  `DestroyEntityAndDescendants()`) looked up BY NAME - `Game::DeleteEntityByName()` -
  responding `200` on success, `404` if no live entity currently has that
  name, or `400` for malformed JSON. Both routes respond `503` if a different
  engine command is already pending (the bridge's single global slot) or
  `504` if the main thread doesn't drain the request within the bridge's
  timeout. See `task_manager/network-impl-3/PHASE0_MASTER_STRATEGY.md` for
  the full six-phase campaign writeup.
- **`POST /set_entity_trs` and `POST /instantiate_light`** (`network-impl-5`
  campaign, `task_manager/network-impl-5/PHASE0_MASTER_STRATEGY.md`) are the
  THIRD and FOURTH `EngineCommandKind` values, added in the same campaign -
  this is the worked example the older revision of this bullet (below, now
  folded into this one) used to describe only hypothetically. Same bridge,
  same rule: a route handler stays a pure function of its own request data
  plus `EngineCommandBridge::SubmitAndWait()`, nothing else engine-side.
  **`POST /set_entity_trs`** updates an existing, by-name entity's LOCAL
  (parent-relative) `Transform` - `Game::SetEntityTrs()` - given a JSON body
  of `name` (required) plus three INDEPENDENTLY OPTIONAL, ALL-OR-NOTHING
  groups (`translation`/`rotation_euler_degrees`/`scale`, each requiring all
  of `x`/`y`/`z` together when present - rotation is Euler DEGREES only, no
  quaternion input). The response ALWAYS echoes the entity's full resulting
  local transform (position, rotation as both Euler degrees and a raw
  quaternion, scale) plus a `"changed":{"translation":...,"rotation":...,
  "scale":...}` object, regardless of which fields this call actually
  changed - a request specifying none of the three groups is a valid,
  harmless no-op that doubles as a de-facto "read the current transform"
  query. Responds `200` on success (including the no-op case), `404` if no
  live entity has that name, `409` if the entity exists but has no
  `Transform` component, or `400` for malformed JSON/an incomplete
  translation-rotation-scale group. The default Game camera entity is named `"Camera"`, so
  `/set_entity_trs` can move it by that name. **`POST /instantiate_light`** spawns a
  new light entity (today: the engine's only implemented kind,
  `DirectionalLight`) - `Game::InstantiateLight()` - mirroring
  `/instantiate_primitive`'s own `name`/`world_position`/`parent` contract,
  plus a `light_type` field (`""`/`"directional"` today, case-insensitive -
  future-proofs the request shape for a later point/spot light without an
  API-breaking change; any other value is a `400`), and `color`/
  `illuminance_lux`/`active` fields mapping 1:1 onto `DirectionalLight`'s own
  component fields. A network-spawned light with no explicit
  `rotation_euler_degrees` gets the SAME "late-afternoon" default rotation
  the Editor's own "Create Directional Light" menu already uses
  (`Quat::FromEulerDegrees(45.0f, -30.0f, 0.0f)`) - NOT identity - shared via
  one small private helper (`DefaultDirectionalLightRotation()`,
  `Game.cpp`) both paths call, so they can never silently drift apart.
  Responds `200` on success or `400` for malformed JSON/a missing `name`/an
  unsupported `light_type`. **A genuine test-coverage improvement over
  `network-impl-3`'s own accepted gap**: unlike `Game::InstantiatePrimitive()`
  (GPU/`Renderer`-touching, "Tier 2, no automated coverage yet" per
  [Testability & Regression Safety](../../AGENTS.md#testability--regression-safety) below), `Game::SetEntityTrs()`/
  `InstantiateLight()` are BOTH fully Tier-1-testable end to end (neither
  touches a live `Renderer` at all) - every layer of both new commands
  (`NetworkRoutes.h` parsing, `Game::` logic, the bridge, the HTTP route) has
  real, direct, automated coverage, with no Tier-2 gap to accept this time.
  See `task_manager/network-impl-5/PHASE0_MASTER_STRATEGY.md` for the full
  five-phase campaign writeup.
- **JSON parsing now exists via a vendored `nlohmann/json`** (single-header
  `json.hpp`, fetched via `cmake/FetchJson.cmake` mirroring
  `cmake/FetchHttplib.cmake`'s own pattern) - originally a deliberate, narrow
  exception to the "no JSON library, hand-rolled formats only" precedent
  this file's own pre-existing `BuildCaptureJsonBody()` established, made
  specifically because `network-impl-3` needed to PARSE untrusted/possibly-
  malformed input (an incoming POST body), not just emit a few already-
  known-safe fields. **As of the `task_manager/scene-serialization-2`
  campaign, this scope has WIDENED**: `nlohmann::json` is now ALSO this
  engine's real ON-DISK scene file format (`src/Scene/SceneJsonFormat.h/.cpp`,
  superseding that campaign's own original hand-rolled TEXT grammar,
  `Scene/SceneTextFormat.h`, deleted in that campaign's Phase 3) and the
  underlying value type the new `src/ECS/Reflection/` generic
  field-reflection layer serializes each component's fields into/out of
  (`ComponentTypeRegistry.h`) - not merely a network-parsing exception
  anymore. A future contributor should still not read this as blanket
  permission to reach for `nlohmann::json` for some UNRELATED, brand-new
  purpose without a similarly explicit, reviewed justification - but "an
  on-disk serialization format that needs a naturally-nestable value type"
  is now an established, valid reason alongside "genuinely parsing
  untrusted network input". See
  `task_manager/scene-serialization-2/PHASE0_MASTER_STRATEGY.md`'s Locked
  Design Decision #1 for the full rationale.

- **`GET /activate_tab`/`GET /list_tabs`** (`network-impl-7` campaign,
  `task_manager/network-impl-7/PHASE0_MASTER_STRATEGY.md`) let an external
  HTTP/LLM-agent caller bring a specific named Editor panel/tab to the
  front, exactly as if a human had clicked it - built on a brand-new,
  dedicated cross-thread bridge, `EditorUiCommandBridge`
  (`src/Application/EditorUiCommandBridge.h/.cpp`), a THIRD, separate bridge
  alongside `FrameCaptureBridge`/`EngineCommandBridge` for the exact reason
  the `FrameCaptureBridge` bullet above already states: a future endpoint
  needing DIFFERENT engine data must build its own new, narrow bridge rather
  than repurpose an existing one for an unrelated kind of request - this one
  is the first to touch EDITOR UI focus state rather than pixels or ECS
  data. `GET /activate_tab?name=<PanelName>` returns `200`
  (`{"success":true,"activated_tab":"<PanelName>"}`) when the named tab was
  found and focused THIS frame, `400` for a missing/empty `name`, `404` when
  `name` isn't one of the engine's known panel names (`EditorPanelRegistry`,
  `src/Core/EditorPanelRegistry.h` - a runtime-populated registry, as of the
  `editor-core-separation-3` campaign's PHASE4, replacing the old compile-time-
  fixed `EditorPanelCatalog.h` - the SAME shared source of truth
  `DockLayout.cpp`'s own default dock-layout logic already reads from), `409`
  when `name` IS known but has no live window yet this session (either a
  narrow just-started-Editor race in a `GTE_ENABLE_EDITOR=ON` build, or
  PERMANENTLY in a `GTE_ENABLE_EDITOR=OFF` build, since
  `NullEditorLayer::ActivateTab()` always reports no live window - never a
  `503` for this reason, since `Application` still owns a real, non-null
  `EditorUiCommandBridge` unconditionally either way), `503` only when the
  bridge pointer itself is null (reachable only in a test that constructs
  `NetworkServer` directly), and `504` on a bridge timeout. `GET /list_tabs`
  always returns `200` with every currently-registered panel name (built-in
  panels PLUS any loaded plugin's own panel(s)) and needs no bridge round-trip
  at all - it just reads `EditorPanelRegistry::Instance().AllNames()` directly.
  `Application::Run()` drains this bridge once per frame immediately after
  `m_editorLayer->NewFrame()` and before `BuildUI()` - the one window where
  Dear ImGui's window/dock state is valid to touch AND where the change is
  still visible in THAT SAME frame's own tab rendering, mirroring
  `EngineCommandBridge`'s own "drain as early as possible" precedent just
  relative to a different pair of per-frame calls. See
  `task_manager/network-impl-7/CAMPAIGN_COMPLETION_REPORT.md` for the full
  five-phase campaign writeup.

- **`POST /import_asset`** (`task_manager/stl-parser-2` campaign,
  `task_manager/stl-parser-2/PHASE0_MASTER_STRATEGY.md`) imports a single file
  that lives ANYWHERE on this machine's filesystem into the Editor's
  "Project" folder, through the exact same `AssetImporter::ImportAssetFile()`
  pipeline the Editor's "Project" panel drag-and-drop already uses (STL/PMX
  -> Mesh `*.gta`, VMD -> Animation `*.gta`, a supported image -> KTX2
  `*.gta`, anything else -> a plain file copy). Built on a brand-new,
  FIFTH cross-thread bridge, `AssetImportCommandBridge`
  (`src/Application/AssetImportCommandBridge.h/.cpp`) - the same "a future
  endpoint needing DIFFERENT engine data must build its own new, narrow
  bridge" rule the `FrameCaptureBridge` bullet above already states, this
  time for a request that is Editor/Project/`AssetDatabase`-shaped rather
  than pixels, ECS data, or Editor UI focus state. A JSON body of
  `source_path` (required, an absolute path to the external file) and an
  optional `destination_folder` (a path RELATIVE to the "Project" root,
  created automatically if missing - omitted/empty means "the Project root
  itself") is parsed by `NetworkRoutes.h`'s `ParseImportAssetRequest()`, then
  handed to `AssetImportCommandBridge::SubmitAndWait()` - unlike every other
  bridge, this one's default timeout is **120000ms (120 seconds)**, not
  3000ms, since the actual import (parsing the file, writing the `*.gta`)
  runs SYNCHRONOUSLY on the MAIN THREAD (a deliberate, documented trade-off -
  a large import, e.g. a 52MB/1,045,458-triangle `.stl`, briefly stalls the
  whole engine frame loop rather than ever letting a route handler touch
  `AssetDatabase` from the network thread). Responds `200` with the
  imported asset's `final_relative_path`/`final_absolute_path`/`guid`/mesh
  conversion details on success, `400` for malformed JSON/a missing
  `source_path`/a semantic import failure (bad source file, a
  `destination_folder` that is absolute, drive/root-relative, or would
  lexically escape the Project root via `".."` - hardened in
  `ProjectPanel::ImportExternalFile()`), `503` if the bridge pointer itself
  is null OR the Editor's "Project" panel isn't available in this build
  (`GTE_ENABLE_EDITOR`/`GTE_ENABLE_PROJECT_PANEL` is OFF) OR another import
  is already pending, or `504` on a bridge timeout. `Application::Run()`
  drains this bridge once per frame, appended after every other bridge's own
  drain block. See `task_manager/stl-parser-2/PHASE0_MASTER_STRATEGY.md` for
  the full five-phase campaign writeup.


- **`POST /build_clusters`** rebuilds the cluster table of a mesh `*.gta` inside the
  project root. It shares `AssetImportCommandBridge` with `/import_asset` (new kind
  `BuildMeshClusters`), so only one of the two is pending at a time. Body:
  `{"gta_path", "weld": "exact"|"position_only", "max_vertices" 3..255,
  "max_triangles" 4..512 (multiple of 4), "cone_weight" 0..1}`; only `gta_path`
  is required. `ParseBuildClustersRequest()` is pure and range-checks every number
  before any cast. The editor's single `MeshClusterBuildRunner` runs the build as a job;
  `EditorHost` polls it once per frame and runs the follow-up (cache invalidation,
  asset record refresh, one log line) once per finished build, even when the caller
  already timed out. Replies `200` with the report, `400` for bad input or a failed
  build, `503` when busy or no editor is available, `504` after the 120 s wait
  (the build still finishes and writes the file atomically).

- **`POST /spawn_instance_grid`** spawns a square grid of instances that share one clustered
  mesh (built from a procedural grid) through the Editor UI command bridge. Body:
  `{"count": 1..100000, "spacing": 0.1..1000}`, both optional (defaults 6 and 1.5).
  `ParseSpawnInstanceGridRequest()` is pure and range-checks every number before any cast.
  A grid above the cluster draw limits is refused, never truncated. Replies `200` with
  `{"success":true,"instance_count":N}`, `400` for bad input or a refused grid, `503` when
  no editor is available or another UI command is pending, `504` on a bridge timeout.
  The shared mesh and pipeline live in `InstanceGridResources`, owned by the editor layer.

- **Inspector debug routes** (`GET /select_asset`, `GET /inspector_state`,
  `POST /inspector/build_clusters`) and **`GET /split_views`** use the same
  `EditorUiCommandBridge` as `/activate_tab`: new `EditorUiCommandKind` values `SelectAsset`,
  `InspectorState`, `InspectorBuildClusters` and `SplitViews`, drained by `EditorHost` between
  `NewFrame()` and `BuildUI()`. Parsing is pure (`NetworkRoutes.h`) and Tier-1 tested.
  - `GET /select_asset?path=<absolute .gta>` selects that mesh asset as the Inspector target, like a
    click in the Project panel. The path must be inside the project root. `400` for a bad path or a
    file that is not a mesh asset, `503` without an editor.
  - `GET /inspector_state` returns `{"selection_kind","selected_path","shown_path","clusters_text",
    "cluster_status","build_button_enabled","build_button_disabled_reason","build_running",
    "clone_button_disabled_reason","clone_destination","clone_weld","open_viewer_after_clone"}`. The
    strings are what the Inspector drew on its last frame (`clusters_text` is the `Clusters: N` line,
    `cluster_status` the cluster tool status line); they are empty when nothing was drawn for that field.
    `clone_button_disabled_reason` is empty when "Create Clustered Copy" is enabled, `clone_destination` is the
    file name only, `clone_weld` is `auto|exact|position_only`.
  - `POST /inspector/build_clusters` presses "Build Clusters" for the selected asset. Body: the optional
    fields of `POST /build_clusters` without `gta_path`, with the same checks. It calls
    `StartInspectorClusterBuild()`, the function the button calls, so the weld and limits land in the
    Inspector controls. `200` = started (read the report from `/inspector_state` once
    `build_running` is false), `400` = refused (nothing selected, not eligible), `503` = a build is
    running or no builder. The route never polls the runner; `EditorHost` is the only poller.
  - `GET /split_views?enabled=true|false` docks `Game` beside `Scene` (`true`) or back as a tab of
    `Scene` (`false`) with ImGui DockBuilder (`SetSceneGameSplit()` in `DockLayout.cpp`). Both windows
    then have a real dock id, so the default-layout rebuild (`DefaultDockLayoutIsNeeded()`) never
    fires. Replies `409` until the dock layout is built. Both views render from the next frame on.
  - `GET /hierarchy_state?first=<row>&count=<n>` (defaults `0` and `200`, `count` capped at `2000`) returns
    the Hierarchy rows of the last drawn frame, answered on the main thread through the new
    `EditorUiCommandKind::HierarchyState`:
    `{"visible","entityCount","rowCount","collapsedCount","first","rows":[{"entity":"<index>/<generation>",
    "parent":"<index>/<generation>"|null,"depth","siblingPosition","label","hasChildren","expanded","selected"}],
    "buildMs","flattenMs","drawnRows"}`. `visible` is `false` while the panel is hidden; `rows` is then empty
    and the three timings are `null` (not measured, never `0`). `buildMs` is the snapshot rebuild,
    `flattenMs` the row flatten, `drawnRows` the rows ImGui drew. `400` for a non-numeric `first`/`count`,
    `503` without an editor. Pinned by `NetworkRoutesInspectorTests.cpp`.
  - `GET /panel_visible?name=Hierarchy&visible=true|false` hides or shows an editor panel for this session only
    (`PanelHiddenFlags` in `EditorContext`; never saved, a restart shows the panel again). It runs on the main
    thread through `EditorUiCommandKind::PanelVisible`. `/activate_tab` cannot hide a panel that sits alone in
    its dock. A hidden panel is skipped before it does any work: the `HierarchyPanel` scope is absent and
    `/hierarchy_state` reports `visible` `false` (timings `null`). Supported `name`: `Hierarchy` only (`kTogglePanelNames`
    in `PanelVisibility.h`). Reply `200`: `{"success":true,"name","visible"}`. `400` for an unknown `name` (the
    error lists the supported names) or a `visible` that is not exactly `true` or `false`, `503` without an editor.
    Pinned by `NetworkRoutesInspectorTests.cpp` and `HierarchyRowsTests.cpp`.
  - `POST /hierarchy_action` is a debug route for tests and AI debugging: it does what a mouse drop or an arrow
    click does, on the main thread through `EditorUiCommandKind::HierarchyAction`. Body:
    `{"kind":"reparent"|"reorder"|"toggle","dragged","target","insertAfter"?}`. An entity is
    `{"index","generation"}` or the `"<index>/<generation>"` text of `/hierarchy_state`; `toggle` needs only `target`.
    The route refreshes the rows itself (it works while the panel is hidden), builds the action with
    `PlanHierarchyAction()` from the target row's `parent` and `siblingPosition` (what a real drop records) and applies
    it with `ApplyHierarchyAction()` / `ToggleHierarchyCollapsed()`, the same functions `HierarchyPanel.cpp` calls.
    Reply `200`: `{"success":true,"kind","changed","firstRow","rows":[...]}` with the rows of `/hierarchy_state`
    for the target row +-5 after the action (`firstRow` = index of the first one). `changed` is `false` when the
    action was legal but moved nothing. `400` for a bad body, a dead entity, a target that is not a visible row
    (hidden under a collapsed row), `dragged == target`, a new parent inside the dragged subtree, or a `toggle` on a
    row without children. `503` without an editor. Pinned by `HierarchyRowsTests.cpp` and `NetworkRoutesInspectorTests.cpp`.

- **Cluster viewer routes** (`/cluster_viewer/*`, `POST /clone_clusters`) use two more
  `EditorUiCommandKind` values, `ClusterViewer` and `SceneCamera`, and the `CloneMeshClusters` kind of
  `AssetImportCommandBridge`. The editor reaches the viewer only through `IClusterViewerControl`
  (`ClusterViewerControl.h`). GPU work (render, verify, Hi-Z capture) runs on the main thread inside the
  editor frame. PNG encoding and the overlay raster run on the network thread. Every number comes from a
  GPU read-back. Status codes: `400` bad input, `409` viewer not open / no valid mesh / device unsupported /
  cluster modes unavailable / no selected Camera / Hi-Z or ID pass failed / no image, `503` no editor (no viewer
  in this build) or another UI command pending, `504` bridge timeout.
  - `GET /cluster_viewer/open?path=<absolute .gta>` opens or re-targets the window and loads the mesh before
    it answers. The path must be a mesh inside the project root. Replies `{"success":true,"message","state":{...}}`.
  - `GET /cluster_viewer/state` returns `{"open","path","valid","mesh","settings","cull","hiz","truth",
    "debugCamera","clusterPathSupported"}`. `cull` holds `total`, `passedFrustum`, `passedCone`, `visible`,
    `byFrustum`, `byCone`, `byOcclusion`, `visibleTriangles`, `runMilliseconds` and the `nested` check.
  - `GET /cluster_viewer/set?key=value&key=value` changes settings, all or none. Keys: `drawMode`
    (`normal|clusters|cull`), `frustum`, `cone`, `occlusion`, `culledStyle` (`hidden|dim`), `debugCamera`,
    `cameraPreview`, `cameraSelected`, `showSceneCamera`, `camX/camY/camZ`, `camYaw/camPitch/camRoll`,
    `camFov/camNear/camFar/camAspect`, `copySceneCamera`, `orbitYaw/orbitPitch/orbitDistance/orbitTargetX/Y/Z`,
    `encodeWidth` (`640|1280|1920`). An unknown key or a bad value is a `400` that names the key; nothing is applied.
  - `GET /cluster_viewer/pick?x=<0..1>&y=<0..1>` is a camera pick at that point of the viewer image. Replies
    `{"success":true,"hit":bool}`. `x` and `y` outside 0..1 are a `400`.
  - `POST /cluster_viewer/verify` runs the ID-pass truth check. `POST /cluster_viewer/capture_hiz` captures Hi-Z
    at the debug camera. Both reply with the new `state`.
  - `GET /cluster_viewer/image?target=view|debug[&overlay=true][&format=json]` returns a PNG of the viewer image
    only, never a full screenshot. `target=debug` is the camera preview (`409` when it is off). `overlay=true`
    draws the debug camera frustum and the selected Scene camera in.
  - `POST /clone_clusters` `{ "gta_path", "weld": "auto"|"exact"|"position_only", "open_viewer", "max_vertices",
    "max_triangles", "cone_weight" }` is "Create Clustered Copy", the twin of `POST /build_clusters`. The
    destination is chosen by the editor (`<stem>_clustered.gta`, then `_clustered_2`); it is never a parameter and
    never overwritten. The request stays pending until the job finishes and replies `{ "success", "created_path",
    "guid", "weld_used", "weld_fell_back", "vertex_count_before", "vertex_count_after", "triangle_count",
    "cluster_count", "avg_triangles_per_cluster", "milliseconds", "message" }`. `503` while another build runs.
- **Scene camera routes** (`/scene_camera/*`) use `ISceneCameraControl` (`SceneCameraControl.h`).
  - `GET /scene_camera/state` lists the cameras of the Scene (entity, name, pose, lens, selected, apex screen
    position as fractions of the Scene image) and the segments of the selected camera frustum in image pixels.
    `pose` is the pose of the Scene fly camera (same shape as `/scene_camera/pose`, without `success`).
  - `GET /scene_camera/pose` replies `{"success":true,"position":{"x","y","z"},"yaw_degrees","pitch_degrees"}`.
  - `GET /scene_camera/set_pose?x=&y=&z=&yaw=&pitch=` moves the Scene fly camera and replies the new pose. All five keys
    are required. `x y z` are world units (not pixels), angles are degrees. Yaw 0 looks +Z. Pitch +89 looks straight
    down; pitch is clamped to +-89 and yaw wraps to [-180, 180). A missing key, a non-number, `nan`, `inf` or a value
    above 1e6 (angles 1e5) is a `400` that names the key. The pose applies before the same frame renders. Wait 2 frames
    before reading cluster counts (the stats readback lag) and before `/scene_camera/image` after a move.
  - `GET /scene_camera/pick?x=<0..1>&y=<0..1>` does the same pick as a mouse click on the Scene image and selects
    the camera it hits. Replies `{"success":true,"hit":bool,"entity"}`.
  - `GET /scene_camera/select?name=<entity name>` selects a Camera entity by name. `404` for an unknown name.
  - `GET /scene_camera/image[?overlay=true][&format=json]` returns a PNG of the Scene view only, with the camera
    overlay drawn in (default on). `409` when the Scene view was not rendered.
  - `503` when there is no editor; `504` when the main thread did not answer in time.
- **`POST /exit`** closes the editor through the normal shutdown path (the process ends with exit code 0, a few frames
  after the reply). Reply `{"success":true,"message":"exit requested"}`. It is POST so a link prefetch cannot close the
  editor. File > Exit does the same.
- **Browser guard.** Destructive routes (`/exit`) refuse a request that looks like it came from a web page: `403` and
  nothing changes. The rules: a `Sec-Fetch-Site` header other than `none` or `same-origin`; an `Origin` header whose host
  is not `127.0.0.1` or `localhost`; a `Host` header that is not `127.0.0.1:<port>` or `localhost:<port>`. A client that
  sends none of these headers (curl, the debug tools) passes.
- **`GET /debug/fault[?kind=<kind>&count=<N>]`** is a loopback test tool that arms a one-shot fault so the failure paths
  can be proven live. Kinds: `offscreen_throw` (the offscreen frame throws), `present_throw` (the present path throws after
  the swapchain image was acquired), `device_lost` (marks the GPU device lost, final, it cannot be undone),
  `main_thread_stall` (the main thread sleeps 3 s once per count at the start of a frame). `count` is a whole number in
  1..1000 and defaults to 1; each count fires once. Reply `{"success":true,"armed":"<kind>","count":N}`. With no query it
  lists the armed counters as `{"success":true,"armed":{"<kind>":N,...}}`. Bad kind or count is a `400`. Arming is
  refused with `403` when the request looks like it came from a web page (browser guard); listing is not. It answers
  while the main thread is stalled because it
  only touches atomics. The route exists only when the engine is built with `GTE_ENABLE_NETWORK`.
- **Profiler routes** read the latest profiler snapshot and never wait for the main thread longer than 0.5 s, so they answer
  while the main thread is blocked. Every number sits inside a metric object
  `{"value","unit","kind","status","age_frames","reason"}`; `value` is `null` unless the status is `Present`, `Partial` or
  `Stale`. Statuses: `Present`, `Partial`, `Stale`, `Absent`, `Unsupported`, `Error`, `Inactive`. The only plain numbers are
  `schema_version`, `frame_index`, `thread_slot` and `depth`. The main thread publishes a snapshot when a request asked in the
  last 2 s, when a spike froze, and every 30 frames otherwise. Before the first publish (for example the main thread is stalled
  right after start) `GET /profiler`, `/profiler/frames` and `/profiler/spikes` answer `200` with
  `{"success":true,"schema_version":1,"frame_index":0,"state":"no_snapshot","has_frame":false,"snapshot_age":metric,"live_block":[...]}`;
  `live_block` is read without any lock, so the waits in progress are still visible. `503` when the bridge is missing (and for
  `POST /profiler/export` before the first snapshot). With
  `GTE_ENABLE_PROFILER=OFF` every route answers `{"success":true,"schema_version":1,"state":"compiled_out"}`.
  - **`GET /profiler`**: `success`, `schema_version`, `frame_index`, `state`, `has_frame`, `snapshot_age`, `capture`,
    `display_paused`, `cpu_frame`, `untracked`, `blocked{gpu,workers,other}`,
    `gpu{views{shared,primary,secondary},span,indirect_draws{...},coverage{declared,executed,recorded,disabled_by_toggle}}`,
    `memory{total}`, `scopes[{name,total,self,calls}]` (top-level scopes, largest first, at most 32), `scope_rows_dropped`,
    `nested[{name,depth,total,self,calls}]` (scopes at depth 1 or deeper of the newest frame, one row per name and depth,
    largest total first, capped at 64), `nested_rows_dropped` (distinct name and depth pairs that did not fit),
    `checks[{id,status,count,text}]`, `drops{late_scopes,foreign_scopes,lost_worker_events,dropped_scopes,dropped_gpu_frames}`,
    `device_state{device_lost,consecutive_frame_timeouts,abandoned_objects,present_policy}` (`present_policy` is `capped` or
    `uncapped`; start the exe with `--no-vsync` for `uncapped`, see below), `live_block[{thread_slot,site,detail,elapsed}]`
    (waits in progress now), `spikes_available`, and:
    - `verdict{text,kind,frames,frame,cpu_work,gpu_wait,present_wait,other_wait,untracked,stall_share,stall_site}`. `kind` is
      `unknown`, `unreliable`, `stalled`, `present_limited`, `gpu_limited`, `cpu_limited` or `serial`. Every number is a median
      over the newest 30 frames and the `text` prints them. Rules, in order: untracked above 10% is `unreliable`, other waits
      above 20% is `stalled` (`stall_site` names the longest wait), present waits above 30% or GPU waits from 40% is
      `present_limited` or `gpu_limited` (the larger share wins), CPU work from 60% is `cpu_limited`, anything else `serial`.
    - `overhead{cost_per_event,estimated,share,warn}`: an `estimated` metric (events recorded times the cost of one event, which
      is measured once at startup). `warn` is true above 3% of the frame.
    - `timeline{aligned,reason}`: `aligned` is true only when GPU time can be placed on the CPU axis (needs a calibrated
      timestamp extension). Otherwise `reason` says why, for example `calibrated timestamp extension not enabled`.

    A view without a target reads `Inactive`; capture off reads `Absent` with reason `capture_off`.
  - **`GET /profiler/frames[?count=N]`**: the newest `N` frames (default 30, clamped to 1..120), oldest first, each
    `{frame_index,cpu,gpu_span,gpu_primary,gpu_secondary}`. A count that is not a whole number is a `400`.
  - **`GET /profiler/spikes`**: `spikes_available`, `frozen_total`, `pinned`, `pin_limit` (metrics) and `spikes[...]`, newest
    first, each `{frame_index,cpu,median,excess,dominant_self,gpu_span,classification,cause,marker_present,pinned,gpu_attached}`.
    A frame is frozen when it takes more than twice the median of the previous 60 frames (or more than the
    `spike_threshold_ms` when that is set). `classification` is `explained` when a marker sits on the frame or the dominant
    scope covers at least half of the excess, otherwise `unexplained`. 16 frames are kept; the oldest unpinned one is evicted.
    The GPU span of the present submit of that frame is read back up to 2 frames later. It is added to `gpu_span` and sets
    `gpu_attached`.
  - **`GET /profiler/spike?index=F`**: one frozen frame by its `frame_index` `F` with `scopes[{name,kind,depth,start,duration,self}]`
    in time order (`start` counts from the frame start), `gpu_views`, `dropped_scopes`. A bad index is a `400`, a frame that was
    never frozen or was evicted is a `404`.
  - **`GET /profiler/scope?name=<exact scope name>`**: every call of one scope in the newest published frame, at any depth,
    summed per depth: `success`, `schema_version`, `frame_index`, `state`, `name`, `rows[{depth,calls,total,self}]` (one row
    per depth, shallow first), `dropped_scopes`. The profiler keeps the scope tree of the newest frame and of frozen spikes
    only, so there is no per-frame history of one scope: poll the route and take the median of the samples. Missing or
    over-long `name` is a `400`, a scope that did not run in that frame is a `404`, no published frame yet is a `503`.
  - **`POST /profiler/set`** with `capture`, `display_pause`, `pin`, `unpin` (a spike `frame_index`) and/or `spike_threshold_ms`
    (0 to 10000, 0 selects the median rule) as query values or a JSON body
    `{"capture":bool,"display_pause":bool,"pin":N,"unpin":N,"spike_threshold_ms":x}`. Reply
    `{"success":true,"capture":bool,"display_paused":bool,"pinned_spikes":metric,"spike_threshold":metric}`. At most 8 spikes
    can be pinned: a ninth pin is a `409`, an unknown frame a `404`. A refused pin or unpin changes nothing. It passes the
    browser guard. No value given is a `400`.
  - **`GET /profiler/timeline_image[?frame=F][&width=W]`**: a PNG of one frame's timeline, drawn on the CPU from frame data (it
    never reads GPU pixels). Without `frame` it draws the newest published frame; with `frame` it draws a frozen spike. `width`
    is 200 to 2400 (default 1200). Lanes from the top: CPU main (one row per nesting depth), waits, worker jobs, and GPU per
    view (shared, primary, secondary). The GPU lane is not aligned to the CPU lanes: its bars start at the left edge (see
    `timeline` in `GET /profiler`). Bad `width` or `frame` is a `400`, a frozen frame that is gone is a `404`, no published
    frame yet is a `503`.
  - **`POST /profiler/export[?frames=N]`**: one JSON document with the `GET /profiler/frames` fields (`N` newest frames, 1 to
    120, default 30), `overview` (the `GET /profiler` body) and `pinned_spikes[...]` (each as in `GET /profiler/spike`). It
    writes no file. It passes the browser guard.


- **`POST /instantiate_asset`** (`task_manager/stl-parser-2` campaign,
  `task_manager/stl-parser-2/PHASE0_MASTER_STRATEGY.md`) spawns an already-
  imported Mesh `*.gta` asset into the live ECS Scene - the network-
  triggerable equivalent of dragging a Mesh asset from "Project" onto
  "Hierarchy". Unlike `POST /import_asset` above, this route reuses the
  ALREADY-EXISTING `EngineCommandBridge` as a FIFTH `EngineCommandKind`
  value, `InstantiateMeshAsset` (`Game::InstantiateMeshAssetFromGtaFile()`,
  a bare-bones `Outcome`-returning wrapper around the already-existing,
  unchanged `Game::CreateMeshEntityFromGtaFile()`) - this is exactly the same
  shape of request `/instantiate_primitive`/`/instantiate_light` already are
  (an ECS/Renderer-mutating spawn), so it correctly does NOT get its own new
  bridge, and it works identically whether `GTE_ENABLE_EDITOR` is ON or OFF,
  with no dependency on the Editor/"Project" panel at all. A JSON body of a
  single required field, `gta_path` (used EXACTLY as given - an ABSOLUTE
  path, e.g. exactly the `final_absolute_path` `/import_asset`'s own response
  already hands back; a relative path resolves against the engine PROCESS's
  own current working directory, NOT specially against the "Project" root),
  is parsed by `NetworkRoutes.h`'s `ParseInstantiateAssetRequest()`, then
  handed to `EngineCommandBridge::SubmitAndWait()` using that bridge's own
  UNCHANGED 3000ms default timeout (spawning an already-decoded Mesh `*.gta`
  is "just" a GPU upload + ECS entity creation, not a from-scratch parse like
  `/import_asset`'s own 120-second allowance exists for). The spawned entity
  sits at the world origin, completely unparented, named after the source
  file's own stem - no position/name/parent input, unlike its
  `/instantiate_primitive` sibling. Responds `200` with the spawned entity's
  `index`/`generation`/`name` on success, `400` for malformed JSON/a missing
  `gta_path`/the underlying spawn itself failing (bad path, wrong asset type,
  an empty mesh), `503` if the bridge pointer is null or another engine
  command is already pending, or `504` on a bridge timeout. See
  `task_manager/stl-parser-2/PHASE0_MASTER_STRATEGY.md` for the full
  five-phase campaign writeup.

- **`POST /save_scene`/`POST /load_scene`** (`task_manager/scene-serialization-2`
  campaign, `task_manager/scene-serialization-2/PHASE0_MASTER_STRATEGY.md`,
  Phase 5) are a thin network bridge to the Editor's existing Save/Load
  system, `Editor/SceneIO.h`'s `SaveScene()`/`LoadScene()` - the same shape
  as every other `EngineCommandBridge`-backed route above, reusing that
  ALREADY-EXISTING bridge as a SIXTH/SEVENTH `EngineCommandKind` value
  (`SaveScene`/`LoadScene`) rather than a new one, since this is exactly the
  same "an ECS-mutating (Load) or ECS-reading (Save) engine action" shape
  every other command on this bridge already is. Both accept an OPTIONAL
  JSON body with a single `path` field - omitted, `null`, or an empty
  string all mean "use `Editor/SceneIO.h`'s own `DefaultScenePath()`" (the
  SAME hardcoded path Ctrl+S/Ctrl+O use); a non-empty string is used EXACTLY
  as given (absolute, or resolved against the engine process's own current
  working directory if relative) - the SAME "caller's responsibility, no
  sandboxing" convention `POST /instantiate_asset`'s own `gta_path` field
  already established. Unlike every OTHER `ParseXxxRequest()` in this file,
  `NetworkRoutes.h`'s `ParseScenePathRequest()` treats a genuinely EMPTY
  request body, or a body that isn't even valid JSON, IDENTICALLY to "path
  omitted" rather than as a 400 validation failure - a deliberate, one-time
  exception, since both endpoints' single-optional-field-only shape makes
  "no body at all" a completely reasonable, common request ("just save/load
  the default scene"); an explicitly-present but non-string `path` (a
  number/bool/object/array) is still a `400` ("path must be a string").
  `Editor/SceneIO.h`'s `SaveScene(Game&, path)`/`LoadScene(Game&, Renderer&,
  path)` explicit-path overloads (new siblings of the original zero-argument
  overloads, which still forward to them with `DefaultScenePath()` and still
  back Ctrl+S/Ctrl+O completely unchanged) do the actual work.
  Responds `200` with `{"success":true,"resolved_path":"<absolute path
  actually used>"}` on success; `503` if the bridge pointer is null, another
  engine command is already pending, OR this build was compiled with
  `GTE_ENABLE_EDITOR=OFF` (`Editor/SceneIO.h` does not even exist in that
  configuration - the SAME `GTE_ENABLE_EDITOR`-off `503` precedent
  `POST /import_asset` already established, mirrored here via a conditionally-
  compiled `case` branch in the always-compiled `EngineCommandDispatch.cpp`);
  `504` on a bridge timeout. On a genuine (non-Editor-availability) failure,
  `POST /save_scene` responds `500` (an I/O-write failure - an environment
  problem, not a caller mistake) while `POST /load_scene` responds `400` (a
  missing/malformed scene file - a caller-actionable "that request was bad"
  case) - the ONE deliberate status-code difference between these two
  otherwise-identical routes. See
  `task_manager/scene-serialization-2/PHASE0_MASTER_STRATEGY.md` for the
  full six-phase campaign writeup.

- **`POST /open_preset_scene`** is File > Open Sponza Scene / Open Hi-Z Demo Scene for AI callers.
  Body: `{"preset":"sponza"|"hiz_demo", "count"?, "spacing"?, "profile"?}`. `preset` is required.
  `count` is a non-negative integer, `spacing` a number from 0.1 to 1000, `profile` is
  `"geometry_only"` (default) or `"full"`. Any other value is `400`. `ParseOpenPresetSceneRequest()` in
  `NetworkRoutes.h` checks the body. The route waits up to 300 s (an import can be slow). Reply `200`:
  `{"success":true,"preset","message"}`; for `"sponza"` it also has `"imported_this_call"` and `"gta_path"`.
  `500` when the open failed
  (the old scene is kept), `503` without the Editor module or while another engine command runs,
  `504` on timeout. The old `POST /open_sponza_scene` route is gone (404).
  For `"hiz_demo"` the reply also carries `"profile"`, `"instanceCount"`, `"countClamped"` (the count is
  clamped to 1..100000 and `message` says "count clamped to N"), `"clustersPerInstance"`, `"workItems"`,
  `"entities":{"camera","sun","wall"}` (`"index/generation"`), `"gpuDrivenScene":{"mode","collectStats"}`
  and `"restoresOnClose":true`. The demo scene has a camera ("Main Camera"), a sun ("HizDemo Sun"), a wall
  ("HizDemo Wall") and the instance grid. It sets GpuDrivenScene to Frustum with stats on. With
  `"geometry_only"` it also turns off the fixed-cost features listed by `GET /hiz_demo/geometry_only_features`.
  The new scene is built first; the old one is replaced only when everything worked. File > Close Hi-Z Demo, the
  next preset open and File > Open Sponza Scene put the features and settings back.

- **`POST /close_preset_scene`** is File > Close Hi-Z Demo for AI callers (`EngineCommandKind::ClosePresetScene`
  -> `ISceneIOCapability::ClosePresetScene()` -> `CloseHizDemo()`). The body is ignored. It removes the entities the
  demo created and restores the features and settings it changed. Reply `200`: `{"success":true,"message"}`.
  `500` `{"error"}` when no demo is open or the restore was incomplete (`message` says which; see `/get_logs`),
  `503` without the Editor module or while another engine command runs, `504` after 30 s.
  Pinned by `NetworkRoutesInspectorTests.cpp` (`BuildClosePresetSceneResponseJson()`).

- **`GET /hiz_demo/geometry_only_features`** returns `{"prefixes":["Shadow","VolumetricLight",...]}`: the one
  list of fixed-cost feature name prefixes the Hi-Z demo's `geometry_only` profile turns off. Scripts read
  it from here. It needs no engine command and always answers `200`.

- **`GET /get_logs`/`POST /clear_logs`** (`task_manager/logger-1` campaign,
  `task_manager/logger-1/PHASE0_MASTER_STRATEGY.md`, Phase 3) expose the
  engine's new `gte::Logger` (see [Logging](logging.md)) over HTTP - a
  smart fetch endpoint filterable by `since_id`/`min_level`/`category`/
  `keyword`/`frame_min`/`frame_max`/`limit`, plus a companion clear route.
  **Both routes call `Logger::Query()`/`Logger::Clear()` DIRECTLY, with NO
  new cross-thread bridge** - a deliberate, narrow, explicitly-documented
  exception to this file's own "a route handler reaches engine state only
  through a reviewed bridge" rule (see the bullet list at the top of this
  file), specific to Logger's own from-day-one thread-safe design (every
  public `Logger::` method is already safe to call concurrently from ANY
  thread, unlike `Registry`/`Renderer`/`Game`/ImGui, which were never built
  with concurrent access in mind) - this must NOT be read as loosening that
  rule for any other future route. See [Logging](logging.md) for the full
  query-parameter contract, status-code mapping, and response JSON shape -
  this bullet exists here only to record the endpoint's existence and cross-
  link to the authoritative write-up, avoiding duplication.

## Named Texture Capture (`GET /get_texture`)

`network-impl-4` campaign (`task_manager/network-impl-4/PHASE0_MASTER_STRATEGY.md`)
adds a THIRD, generic way to pull a still image out of the engine over HTTP -
rather than one dedicated endpoint per known render target (as
`/get_swapchain`/`/get_game_view` are), `GET /get_texture` can capture ANY
render-graph texture, by name, that the engine has ever declared this
session, plus a companion `GET /list_textures` that enumerates what's
currently capturable. Follow these rules whenever touching this feature or
registering a new named texture:

- **`gte::rg::RenderGraphDebugTextureRegistry`
  (`src/Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h/.cpp`) is the
  mechanism, and registration is fully AUTOMATIC.** Every texture any pass
  declares via `RenderGraphBuilder::CreateTexture()`/`ImportTexture()`
  becomes capturable by that exact name with ZERO opt-in required from that
  pass's own author - `RenderGraph::ExecuteCompiledGraph()` (both
  `ExecuteTimingMode` regimes) upserts a `DebugTextureSnapshot` for every
  declared texture at the end of its own per-frame execution. **Buffers
  (`CreateBuffer()`/`ImportBuffer()`) are NEVER visible this way, by
  design** - this registry only ever tracks textures; a buffer has a
  completely separate `BufferHandle`/`bufferNames` vocabulary this registry
  never touches at all.
- **Never confuse a render-graph PASS's name with the TEXTURE name actually
  registered for capture.** This campaign's own strategy documents record a
  real, corrected mistake where an earlier revision conflated the `"Present"`
  pass name with the texture name actually capturable for it, which is the
  literal string `"Swapchain"` (registered via
  `ImportTexture("Swapchain", ...)` inside that pass, not derived from the
  pass's own name in any way). A future contributor adding a new named
  texture must always check the literal string passed to
  `CreateTexture()`/`ImportTexture()` for that texture, never the name of the
  `AddPass()` call it happens to sit near.
- **`Renderer::WaitForGpuIdle()` (drains the graphics queue with a bound, then
  idles the device) is a deliberate, NARROW exception to the "zero added GPU
  stall" swapchain-capture design - `GET /get_texture`, and ONLY that endpoint,
  calls it once per request, before reading pixels back.** This is acceptable
  because the endpoint is rare, human/LLM-triggered debugging traffic, never
  part of a per-frame path - unlike `/get_swapchain`'s pipelined, zero-stall
  `SwapchainCaptureService`. A failed wait never throws out of the capture: the
  route answers `503` with the real reason (see "Capture failures" below).
  **No other endpoint, and no per-frame engine code, may call
  `Renderer::WaitForGpuIdle()`.** A future endpoint that must read back a
  texture without stalling builds its own pipelined capture path (mirroring
  `SwapchainCaptureService`).
- **Capture failures.** The `Renderer::Capture*` functions never throw. They return a `CaptureResult` whose status is
  `Ok`, `NothingToCapture`, `GpuBusy` (a bounded GPU wait timed out), `DeviceLost` or `Failed`. The capture routes
  (`/get_swapchain`, `/get_game_view`, `/get_texture`, `/get_window`) answer `503` with the real reason text for the two
  GPU failures: `the GPU did not finish in time; try again` and `GPU device lost, rendering stopped`. A request that
  the main thread did not answer in time is `504`. Any other failure to find a target keeps its `409` answer.
  After a lost device, a capture that needs a GPU wait answers `503`, and `GET /render_graph` shows
  `gpu_wait.device_lost` as `true`.
- **`RenderGraph::NotifyDebugTextureStateOverride()` exists because a
  graph-external manual Vulkan barrier is otherwise invisible to the render
  graph's own internal resource-state tracking** - a pass that hands a
  texture off for external sampling/presentation via a manual
  `EmitImageBarrier()` call (outside the graph's own compiled barrier plan)
  must call this method right afterward so the registry's own
  `colorState`/`depthState` for that texture stays accurate for the NEXT
  frame's barrier synthesis and for a correct `/get_texture` capture. There
  are FOUR existing call sites today - name all four when reasoning about
  this, an earlier revision of this campaign's own strategy undercounted it
  as two before its own audit caught it: `Application::Run()`'s two
  `FinalizeRenderTextureForExternalSampling()` calls (`"GameView"`/
  `"SceneView"`), `FramePresenter.cpp`'s own `"Swapchain"` `PRESENT_SRC_KHR`
  finalize inside `PresentViaRenderGraph()`, and
  `ComputeBlurValidation::FinalizeForSampling()`'s `"BlurredSceneOutput"`
  finalize (called unconditionally from `Application.cpp`, regardless of
  whether that call actually wrote anything this frame). A future pass
  author adding a SIMILAR graph-external manual transition for some other
  named texture must add the matching correction call too, or that texture's
  registry entry silently goes stale.
- **The `frames_since_update` counter (Locked Design Decision 4) only
  advances once per real `SynchronousImmediateReadback` `Execute()` call** -
  a texture registered ONLY by the PIPELINED regime (today: `"Swapchain"`)
  has its own freshness signal driven by how often the OFFSCREEN regime
  happens to run elsewhere, not by how often it itself updates. In a session
  where both Editor panels ("Game"/"Scene") are hidden - or any
  `-DGTE_ENABLE_EDITOR=OFF` build - this reads as a constant, maximally-fresh
  value regardless of real elapsed frames. This is intentional, not a bug to
  "fix" with a second, dedicated counter.
- **`gte::PublishedTextureListEntry` (`src/Application/FrameCaptureBridge.h`)
  and `gte::Network::TextureListEntryView` (`src/Network/NetworkRoutes.h`)
  are two small, nearly-identical, DELIBERATELY SEPARATE structs behind
  `GET /list_textures` - never collapse them into one shared type crossing
  the `Application`/`Network` layer boundary.** `Application::Run()` resolves
  each `rg::DebugTextureSnapshot` into a `PublishedTextureListEntry` (already-
  resolved plain scalars only - `name`/`regime`/`format` as `std::string`,
  `width`/`height` as `std::uint32_t`, `hasDepth`/`framesSinceUpdate` -
  keeping `FrameCaptureBridge.h` itself Vulkan/Renderer/RenderGraph-free) and
  calls `FrameCaptureBridge::PublishTextureList()`; `NetworkServer.cpp` (the
  one place that legitimately depends on BOTH `FrameCaptureBridge.h` and
  `NetworkRoutes.h`) is the ONLY place that copies a `PublishedTextureListEntry`
  into a fresh `TextureListEntryView`, one field at a time, right before
  calling `BuildListTexturesResponseJson()`. This is the exact same "don't
  take a foreign layer's struct" rule this section already establishes for
  `EngineCommandBridge`, applied here to a second, independent case.
- **The two new endpoints' exact contract:**
  `GET /get_texture?texture_name=<name>[&channel=color|depth][&format=png|base64|json]`
  captures the named texture (`channel=depth` requires that texture to have
  been registered `hasDepth == true`) and returns a PNG (default) or a
  `{"width":...,"height":...,"format":"png","data_base64":"...",
  "frames_since_update":...}` JSON envelope (`?format=base64`/`json`, or an
  `Accept: application/json` header) - `400` for a missing `texture_name` or
  an invalid/wrong-case `channel` value (exact-lowercase `"color"`/`"depth"`
  only), `409` if the requested channel doesn't exist for that texture,
  `503` if a different `/get_texture`/`/get_swapchain`/`/get_game_view`
  request is already pending, `504` if the main thread never resolves the
  named texture within the fixed timeout (e.g. an unknown name, or a
  not-currently-rendering view like a hidden "Scene" panel). `GET
  /list_textures` returns a JSON array of every texture currently known to
  the registry, each entry carrying `name`/`regime`/`format`/`width`/
  `height`/`has_depth`/`frames_since_update`.
- **`GET /get_texture` also resolves a name registered as a VOLUME texture**
  (`network-impl-6` campaign,
  `task_manager/network-impl-6/PHASE0_MASTER_STRATEGY.md`) - same query
  parameters, same `?format=png|base64|json` negotiation, same
  `Accept: application/json` honoring as the 2D case documented above.
  `channel=depth` against a volume name is always `409` - a
  `VolumeTarget`/`VolumeTexture` has no depth-companion concept at all (see
  `VolumeTarget.h`'s own doc comment), so this is the SAME existing
  depth-`409` failure mode applied to a case that is always true for every
  volume, not a new one. Unlike an ordinary 2D capture (a pixel COPY of
  whatever the render graph already rendered this frame), a volume capture
  is rendered FRESH, on demand, at request time: a single, fixed-camera,
  front-to-back alpha-composite raymarch (Unity's Texture3D "Volume" preview
  mode equivalent) into a persistent 256x256 RGBA8 thumbnail - no camera/
  yaw/pitch/distance query parameters, and no other preview mode
  (Slice/Maximum-Intensity-Projection), by deliberate design; this is a
  debugging/LLM-agent capability, not an Editor inspector feature. The
  mechanism is fully generic over ANY current or future volume `TextureHandle`
  a render-graph pass declares and keeps alive via
  `RenderGraphBuilder::KeepTextureOutput()` - not hardcoded to the
  Atmosphere feature's own two aerial-perspective volumes specifically,
  mirroring `RenderGraphDebugTextureRegistry`'s own "zero opt-in required"
  property exactly (see [Atmosphere Scattering](atmosphere-scattering.md) below for where the new
  volume-texture registry itself, `RenderGraphDebugVolumeTextureRegistry`, is
  documented). **As of the `atmosphere-scattering-2` campaign's Phase 4**
  (`task_manager/atmosphere-scattering-2/`), this volume-preview raymarch also
  has a SECOND, atmosphere-aware interpretation mode, auto-selected purely by
  volume name — zero new query parameter, zero new endpoint: for a volume
  whose name starts with the literal prefix `"AtmosphereAerialPerspectiveVolume"`,
  `alpha` is reinterpreted as "haze amount" (`1 - transmittance`, since this
  LUT's `alpha` is transmittance, the opposite polarity of a generic density)
  and `rgb` is exposure-adjusted (`2000x`) and Reinhard-tonemapped before
  compositing, rather than the generic `alpha = density, rgb = raw color`
  reading. Every other (present or future) volume texture keeps the original,
  unchanged generic interpretation documented above — see
  [Atmosphere Scattering](atmosphere-scattering.md) below for the full writeup.
- **`GET /list_textures` entries now carry a
  `"kind":"texture2d"|"texture3d"` field, plus a `"depth"` field** (a
  volume's Z/texel-count extent - always `0` on a 2D entry) - every other
  pre-existing 2D-entry field is unchanged. This is how an LLM/AI agent
  caller discovers which `texture_name`s are volumes worth requesting,
  without any prior knowledge of the engine's internal naming convention.
- **`gte::VolumeTexturePreviewRenderer`/`VolumeTexturePreviewMath.h`**
  (`src/Renderer/VolumeTexturePreviewRenderer.h/.cpp`,
  `src/Renderer/VolumeTexturePreviewMath.h/.cpp`) is the self-contained,
  on-demand, no-RenderGraph-dependency renderer behind the raymarch above -
  the same established shape `ComputeBlurValidation`/
  `AtmosphereTransmittanceLutValidation`/`GpuSkinningValidation` already use
  (a small class built directly on `Renderer::ImmediateSubmit()`/
  `Renderer::CaptureImagePixels()`, with its own owned `VkSampler`/
  descriptor-set/compute pipeline, never routed through the render graph
  itself). `VolumeTexturePreviewMath.h`'s `ComputeVolumeCameraSetup()`/
  `IntersectRayBox()` is the permanent CPU ORACLE for the fixed camera/
  ray-box math `VolumeTexturePreview.comp` mirrors in GLSL - the exact same
  "if the GLSL and the CPU oracle ever disagree, the CPU oracle is right by
  definition and the shader is what needs fixing" discipline `AtmosphereMath.h`
  already establishes (see [Atmosphere Scattering](atmosphere-scattering.md) below). See
  `task_manager/network-impl-6/PHASE0_MASTER_STRATEGY.md` for the full
  six-phase campaign writeup. The `atmosphere-scattering-3` campaign
  (`task_manager/atmosphere-scattering-3/PHASE0_MASTER_STRATEGY.md`) later
  added a SECOND, dedicated camera/proxy-shape framing specifically for the
  Aerial Perspective volume (auto-selected the same way as its own
  color-interpretation mode, see [Atmosphere Scattering](atmosphere-scattering.md) below) — every OTHER
  volume texture still resolves through this bullet's own original
  `ComputeVolumeCameraSetup()`/`IntersectRayBox()` path, unmodified.

- **`GET /render_graph`** (`editor-core-separation-7` campaign,
  `task_manager/editor-core-separation-7/PHASE0_MASTER_STRATEGY.md`) returns
  the exact same data the Editor's "Render Graph" panel shows, as one
  versioned JSON object - built on `FrameCaptureBridge`'s existing "publish
  once per frame from the main thread, read a thread-safe copy from the
  network thread" pattern (a SECOND `Publish*`/`Get*` pair,
  `PublishRenderGraphMetadata()`/`GetPublishedRenderGraphMetadata()`,
  alongside its existing texture-list pair - not a new, sixth bridge class;
  see that campaign's PHASE0 Locked Design Decision #4 for why). The response
  body is `Network::RenderGraphMetadataToJson()` (`src/Network/RenderGraphMetadataJson.cpp`): `schema_version`
  (an integer, bumped only on a future breaking shape change - currently
  `2`), `offscreen_regime`/`present_regime` (each one `ExecuteTimingMode`
  regime's own `regime_name`/`passes`/`resources`/`gpu_frame_status`/
  `gpu_span_milliseconds`/`gpu_folded_passes`/`dropped_gpu_frames` - every enum
  already resolved to a human
  string - `kind`, `category`, `draw_kind`, `view_scope` (`Shared`, `Game` or `Scene`), `view` (`shared`, `primary` or `secondary`),
  `instance` (hex id of the pass's binding instance, empty when the pass never executed),
  `render_pass_event` - every GPU timing value already resolved to a status
  word (`gpu_timing_status`: `Present`, `Partial`, `Absent`, `Error`, `Unsupported` or `Inactive`), a
  display string (`gpu_timing_text`) AND a raw nullable number
  (`gpu_timing_milliseconds`, set only when the status is `Present`; `gpu_barrier_milliseconds` and `gpu_work_milliseconds` split
  it into barrier and work time, both `null` when not measured, and the barrier is also `null` in Coarse stamp mode; `gpu_folded_passes` counts the passes
  one overflow record stands for), every pass carrying a plain `owning_feature_name`
  string field - always populated, never a lookup/registry resolution, set
  automatically the moment that pass is declared under an open
  `rg::RenderFeatureScope` - and every resource carrying both its raw
  `first_use_pass_index`/`last_use_pass_index` AND the already-resolved
  `first_use_pass_name`/`last_use_pass_name`, `null` when never used),
  and `render_features` (the
  same loaded `_v2` plugin ordering/blend-mode readout the panel's own Plugin
  Render Features section shows; each entry also has `enabled_primary` and `enabled_secondary`: a feature runs in a view scope when `enabled` and that scope's key are both true). Each pass also has `indirect_draw_count`. Three read-only
  objects report device and memory state: `indirect_draw` (`draw_indirect_count`,
  `multi_draw_indirect`, `draw_indirect_first_instance`, `max_draw_indirect_count`),
  `gpu_memory` (`total_bytes`, `mesh_count`, and `by_name` bytes per GPU debug name; unnamed
  allocations sum under `"(unnamed)"`) and `gpu_wait` (`device_lost`: true once a GPU wait failed
  for good and rendering stopped; `consecutive_frame_timeouts`: frame waits that timed out in a
  row, the device is lost after the fourth; `abandoned_objects`: GPU objects parked because
  the GPU may still use them). ALWAYS reflects the truly latest real
  engine frame, completely independent of whether a human has the Editor
  panel's own "Pause" checkbox ticked (that checkbox only freezes what ONE
  ImGui window displays - see that campaign's PHASE0 Locked Design Decision
  #9). Responds `200` on success, `503` if the bridge pointer itself is null
  (reachable only in a test that constructs `NetworkServer` directly, the
  same convention every other bridge-backed route above already documents).
  No query parameters in this first version - see that campaign's own PHASE0
  Locked Design Decision #8 for why a future `?regime=`/`?pass=` filter is
  deliberately deferred rather than spun up speculatively. This same campaign
  also shipped a real "Export DOT" implementation for the Editor's own
  "Render Graph" panel (`BuildRenderGraphDot()`/`ExportRenderGraphDotToFile()`,
  `src/Editor/RenderGraphDotExport.h/.cpp`, `gte_editor`-tier, no HTTP surface
  of its own) - a pre-existing "Planned for Phase 9" disabled button, now
  real and enabled, writing a Graphviz `.dot` file to a fixed,
  working-directory-relative path (`render_graph_export.dot`, always
  overwritten) and logging the resolved path via `GTE_LOG_INFO`. See
  `task_manager/editor-core-separation-7/PHASE0_MASTER_STRATEGY.md` for the
  full five-phase campaign writeup.

- **`GET /feature_debug/list`, `/feature_debug/state`, `/feature_debug/set`, `/feature_debug/invoke`** read and change
  a built-in render feature that exposes `fx::IFeatureDebugEndpoint` (`Feature::DebugEndpoint()`). The routes are
  generic: Core knows no feature name. One bridge (`FeatureDebugCommandBridge`) hands the request to the main thread,
  and `EditorHost` serves it before the frame is built, so an edit shows in the frame built right after it.

  | Route | Query | Reply |
  |---|---|---|
  | `/feature_debug/list` | none | `{"features":["Shadow", ...]}` (only features with an endpoint) |
  | `/feature_debug/state` | `name` | `{"feature":"Shadow","fields":{...}}` |
  | `/feature_debug/set` | `name`, then every other key is a value to set | the new state; all values apply or none |
  | `/feature_debug/invoke` | `name`, `action`, extra keys are arguments | the state after the action |

  Status codes: 400 for a bad query, an unknown or read-only key, or an unparsable value (the body names the key as
  `"key"`); 404 when no feature of that name has an endpoint; 503 when the bridge is missing or another request is
  pending; 504 when the main thread did not answer in time. A 504 request may still be applied later, so read the
  state before you repeat a `set`. Limits: `name` is required, a duplicate key is a 400, at most 64 arguments, a key
  up to 64 bytes, a value up to 256 bytes. A non-finite number is written as `null`. The Shadow endpoint is
  described in `shadow-mapping.md`. The `GpuDrivenScene` endpoint settable keys are `enabled`, `mode` (`NoCull`,
  `Frustum` or `HiZ`), `cone`, `collectStats` and `debugView` (`Off`, `ClusterId` or `ClusterPhase`). A bad value answers
  `<key> must be <allowed values>`; any other key answers `unknown key`. The only action is `reset_defaults`; any other
  action answers `unknown action; supported: reset_defaults`. Its state is prefixed `game.` and `scene.` per view
  (`testedClusters`, `visibleClusters` and `visibleValid` (needs `collectStats`), `testedAtVisibleFrame`, `hidden`,
  `hiddenValid`, `cameraValid`, `claimedEntities` (drawn plus skipped instances), `occlusion`, `hizReason` (`None`,
  `Unavailable`, `TooLarge` or `NothingInView`), `skippedParts` and `skippedInstances` (parts outside the cull frustum:
  no pass, entities still claimed; a view whose parts are all skipped reports `visibleClusters` 0 with `visibleValid` true;
  the four keys `game|scene.skippedParts|skippedInstances` are numbers, pinned by `GpuDrivenSceneTypesTests.cpp` through
  `AppendGpuDrivenSceneViewFields()`),
  `sharedSurvivors` (the secondary view drew the primary view's survivors), `inertReason`, `ledgerBytes`, ...) plus the CPU draw cull
  of the view: `cpuTested`,
  `cpuCulled`, `cpuUnbounded` (one frame old), `cpuCullState` (`active`, `off`, `suppressed: frame capture` or
  `no camera`), `cullCamera` and `hizSource` (`own`, `primary` or `none`). `cpuPath.skinned`, `cpuPath.noIndexBuffer`,
  `cpuPath.unsupportedLayout`, `cpuPath.noClusterTable`, `cpuPath.tooFewInstances`, `cpuPath.nonFinite` and `cpuPath.noSlot` count
  the renderables that stayed on the CPU path by reason. `reasons.valid`, `reasons.frustum`, `reasons.cone`, `reasons.pyramid`,
  `reasons.history` and `reasons.kept` count the cull items of the view by outcome (only with `collectStats`; `reasons.valid` says
  whether the counters were read). `cameraValid` is the live camera of the
  view. `hidden` is 0 while `hiddenValid` is false. `visibleAge` is `frameIndex - visibleFrame`, the frames since the
  visible count was read back; it is 0 when there is no count yet (`visibleFrame` 0).

- **`GET /render_graph/set_pass_enabled`, `GET /render_graph/passes`,
  `GET /render_graph/set_feature_enabled`, `GET /render_graph/set_feature_priority`,
  `GET /render_graph/set_blur_enabled`, `GET /render_graph/set_gbuffer_enabled`**
  (`editor-core-separation-8` campaign,
  `task_manager/editor-core-separation-8/PHASE0_MASTER_STRATEGY.md`) turn the
  read-only `GET /render_graph` above into a genuinely CONTROLLABLE render
  graph - the natural next layer on top of `editor-core-separation-7`'s own
  "observable" milestone. All 6 routes are `GET` + query parameters only (no
  JSON POST body anywhere in this campaign - every mutated field is a plain
  bool or int, so a query parameter fits perfectly), and all 6 share ONE new,
  dedicated cross-thread bridge, `RenderGraphControlCommandBridge`
  (`src/Application/RenderGraphControlCommandBridge.h/.cpp`) - mirroring
  `FrameDebuggerCommandBridge`'s own exact "one bridge, several command kinds"
  shape (mutex + `std::condition_variable` + single pending-request slot,
  drained once per frame by `EditorHost.cpp`'s own `Run()` loop, immediately
  after the existing `FrameDebuggerCommandBridge` pump) - never a new enum
  value bolted onto an existing, unrelated bridge. **Built-in render passes**
  (`RenderOpaque`, `DrawSkyBackground`, `AtmosphereComposite`, `GpuSkinning`,
  `RenderTransparent`, ...) get ON/OFF ONLY, keyed by their existing
  `rg::RenderPassDesc::debugName` string via a new, plain `gte_core` class,
  `gte::rg::RenderPassToggleRegistry` (`src/Renderer/RenderGraph/
  RenderPassToggleRegistry.h/.cpp`) - auto-discovering each pass's name the
  first time it is actually declared this session (default `enabled = true`,
  zero behavior change for any pass nobody has touched), consulted once per
  frame from `RenderPipeline::DeclareOnePhase()`'s own existing flush loop
  (mirroring `SetLegacyViewScopeTranslator()`'s own injection precedent) plus
  two narrowly-scoped early-return guards inside the `"AtmosphereComposite"`/
  `"GpuSkinning"` provider lambdas specifically (the only two built-in
  providers that call `frame.builder.AddRenderPass(...)` directly and never
  reach that flush loop at all). A small, permanent, hardcoded deny-list
  (`RenderPassToggleRegistry::IsDenyListed()`) refuses to ever disable
  `"Present"` - the pass that finally puts pixels on the swapchain. There is
  NO `RenderPassEvent` reassignment for built-in passes anywhere in this
  campaign, by explicit product decision - the Investigation this campaign was
  based on flagged a real crash risk there (a reassigned pass's tier can
  silently break a real data dependency and trip `RenderGraphCompiler`'s own
  event-contradiction assert). Disabling a shared-name
  built-in pass (`RenderOpaque`/`DrawSkyBackground`/`RenderTransparent`/
  `AtmosphereComposite` are each declared with the SAME literal `debugName` for
  BOTH the Game View and the Scene View) disables it for BOTH views at once -
  there is no way to disable it in one view only, an accepted, intentional
  consequence of keeping the registry dead simple. **Plugin "scriptable render
  features"** (`IRenderFeatureModule_v2`) get ON/OFF AND LIVE runtime priority
  reassignment (stage - `PostComposite`/`PreUI` - stays fixed; only ordering
  WITHIN a stage changes), via two new `RenderFeatureCompositor` methods,
  `SetFeatureEnabled()`/`SetFeaturePriority()` (`src/Core/Plugins/
  RenderFeatureCompositor.h/.cpp`) - safe because every `_v2` plugin already
  renders into its own fully private, isolated offscreen target, and the
  compositing chain is rebuilt fresh from these vectors every single frame.
  The plugin ABI itself (`GtePluginRenderFeatureDescriptor`) is completely
  untouched - the new `enabled`/priority-override state is 100% host-side.
  A disabled plugin feature is never removed from `RenderFeatureCompositor`'s
  own tracked entries (only skipped during compositing), so
  `GET /render_graph`'s existing `render_features[]` array now also reports a
  new `"enabled"` field automatically on every entry, plus `"enabled_primary"` and `"enabled_secondary"` (the per-view-scope
  switch; a feature runs in a scope when `enabled` and that scope's key are true). `GET /render_graph/set_feature_enabled?name=&enabled=`
  takes an optional `view=primary|secondary`: without it the global override and both scope switches are set; with it only that
  scope's switch changes. Any other `view` value is a `400`. A feature may default its secondary view to off
  (`fx::FeatureSetup::DefaultSecondaryViewEnabled(false)`): VolumetricLight, Ssgi, Gtao and the Atmosphere aerial-perspective passes do.
  No new discovery
  endpoint was needed for plugin features. A disabled BUILT-IN pass, by
  contrast, is never declared at all, so it leaves zero trace in
  `GET /render_graph`'s own pass tables - this is why built-in passes alone get
  a brand-new discovery endpoint, `GET /render_graph/passes`, returning
  `{"passes":[{"name":"...","enabled":true,"ever_declared_this_session":true},
  ...]}` for every built-in pass name the registry has ever seen this session.
  **Compute Blur / GBuffer Validation ON/OFF** (already-existing
  `EditorContext::showBlurredSceneOutput`/`showGBufferValidationOutput` bools,
  previously only reachable via 2 checkboxes in the "Scene" panel) now also
  have matching checkboxes in the "Render Graph" panel AND these 2 new HTTP
  routes, routed through 2 new `IEditorLayer` virtual setters
  (`SetShowBlurredSceneOutput()`/`SetShowGBufferValidationOutput()`, mirroring
  `FrameDebuggerSetEnabled()`'s exact existing shape) since this is
  Editor-owned (`EditorContext`) state, never a direct `Core` call. **Every
  new toggle/override this campaign adds is IN-MEMORY ONLY** - nothing
  persists to disk, and a fresh Editor launch always starts with every
  built-in pass enabled, every plugin feature enabled at its own
  author-declared priority, and both debug-pass checkboxes off. **The real,
  as-shipped response shapes** (all confirmed via a live
  `run_app_background`/`gte_send_request` session, see
  `task_manager/editor-core-separation-8/PHASE5_COMPLETION_REPORT.md` for the
  full captured transcript): every mutating route
  (`set_pass_enabled`/`set_feature_enabled`/`set_feature_priority`/
  `set_blur_enabled`/`set_gbuffer_enabled`) responds `200` `{"success":true}`
  on success or a non-200 `{"success":false,"error":"<message>"}` on failure -
  `400` for a missing/malformed query parameter, `409` for a semantically
  rejected name (`"Present"` deny-listed, or a plugin feature name that
  matches no currently-loaded `_v2` plugin), `503` if the bridge pointer
  itself is null, `504` on a bridge timeout; `GET /render_graph/passes` always
  responds `200` with the shape shown above, or `503`/`504` on the same bridge
  failure modes. See
  `task_manager/editor-core-separation-8/PHASE0_MASTER_STRATEGY.md` for the
  full six-phase campaign writeup.

- **`GET /render_graph/set_display_regime?regime=<present|offscreen>`** - the
  7th `/render_graph/*` mutation route, added after the six-route contract
  above. Flips which regime the "Render Graph" panel displays
  (`present` -> `RenderGraphRegimeChoice::Present`, `offscreen` ->
  `RenderGraphRegimeChoice::Offscreen`). `400` for any other `regime` value.
  Response is `{"success":true,"window_open":<bool>}` - `window_open` is
  `false` when the "Render Graph" panel is currently closed, so nothing is
  visibly affected by this call even though the underlying
  `EditorContext::renderGraphDisplayedRegime` field still changed.
