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
  pipelined, frame-in-flight-aware swapchain readback mirroring
  `GpuTimingService`'s own Present-timing pattern, with zero added GPU
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
  translation-rotation-scale group. **`POST /instantiate_light`** spawns a
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
  `cmake/FetchHttplib.cmake`'s own pattern) - a deliberate, narrow exception to
  the "no JSON library, hand-rolled formats only" precedent
  `Scene/SceneTextFormat.h`/`NetworkRoutes.h`'s own pre-existing
  `BuildCaptureJsonBody()` established, made specifically because this
  campaign needs to PARSE untrusted/possibly-malformed input (an incoming
  POST body), not just emit a few already-known-safe fields. A future
  contributor should not read this as blanket permission to reach for
  `nlohmann::json` anywhere else in the engine without the same
  "genuinely parsing untrusted input" justification.

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
  `name` isn't one of the engine's known panel names
  (`src/Editor/EditorPanelCatalog.h`'s `kKnownEditorPanelNames`/
  `IsKnownEditorPanelName()` - the SAME shared source of truth
  `DockLayout.cpp`'s own default dock-layout logic already reads from), `409`
  when `name` IS known but has no live window yet this session (either a
  narrow just-started-Editor race in a `GTE_ENABLE_EDITOR=ON` build, or
  PERMANENTLY in a `GTE_ENABLE_EDITOR=OFF` build, since
  `NullEditorLayer::ActivateTab()` always reports no live window - never a
  `503` for this reason, since `Application` still owns a real, non-null
  `EditorUiCommandBridge` unconditionally either way), `503` only when the
  bridge pointer itself is null (reachable only in a test that constructs
  `NetworkServer` directly), and `504` on a bridge timeout. `GET /list_tabs`
  always returns `200` with every currently-known panel name and needs no
  bridge round-trip at all, since the catalog is compile-time-fixed.
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
- **`Renderer::WaitForGpuIdle()` (a full `vkDeviceWaitIdle()`) is a
  deliberate, NARROW, bounded exception to `network-impl-2`'s own "zero added
  GPU stall" swapchain-capture design principle - `GET /get_texture`, and
  ONLY that endpoint, calls it once per request, before reading pixels back.**
  This is acceptable here specifically because this endpoint is rare and
  human/LLM-triggered debugging traffic, never part of any per-frame path -
  unlike `/get_swapchain`'s pipelined, zero-stall
  `SwapchainCaptureService`/`GpuTimingService`-style design. **No other
  endpoint, and no per-frame engine code anywhere else, may ever call
  `Renderer::WaitForGpuIdle()`** - this is a warning for future contributors,
  not just a historical note; if a future endpoint needs to read back a
  texture without stalling, it must build its own pipelined capture path
  (mirroring `SwapchainCaptureService`) rather than reaching for this method.
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
  mechanism is fully generic over ANY current or future `VolumeTextureHandle`
  a render-graph pass declares and keeps alive via
  `RenderGraphBuilder::KeepVolumeTextureOutput()` - not hardcoded to the
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
