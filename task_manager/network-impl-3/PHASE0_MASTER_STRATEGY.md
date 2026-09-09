# PHASE0 — MASTER STRATEGY: `instantiate_primitive` / `delete_entity` over HTTP POST

Campaign folder: `task_manager/network-impl-3/`
Branch: `feature/network-impl`
Prior campaigns this one builds directly on top of (READ THESE FIRST if anything
below is unclear):
- `task_manager/network-impl-1/PHASE0_MASTER_STRATEGY.md` — the embedded HTTP
  server itself (`gte::Network::NetworkServer`), loopback-only, background
  thread, `GET /http_hello_world`.
- `task_manager/network-impl-2/PHASE0_MASTER_STRATEGY.md` — the first
  engine-state-touching endpoints (`GET /get_swapchain`, `GET /get_game_view`),
  and — most importantly for this campaign — the **cross-thread bridge**
  pattern (`FrameCaptureBridge`) that lets a network route handler safely ask
  the main thread to do something and wait for the answer.
- `AGENTS.md`, section **"Networking"** — the load-bearing thread-safety rule
  this whole campaign must keep satisfying: *"A route handler must be a PURE
  function of its own request data only — it must NEVER touch
  Registry/Renderer/Game/AssetDatabase/IEditorLayer/ImGui/any other engine
  subsystem, directly or indirectly, full stop."* AGENTS.md's own Networking
  section explicitly anticipates exactly this future need: *"a future endpoint
  that genuinely needs engine data needs a dedicated, reviewed, thread-safe
  bridge built first (e.g. a fixed-size, mutex-guarded command/snapshot queue
  the main thread drains once per frame ...) — never a raw pointer/reference
  into live engine state handed to a handler lambda."* This campaign is that
  anticipated future need, arriving.

---

## Step 1 — The Goal (Where are we going?)

Give the engine's embedded HTTP server (`gte::Network::NetworkServer`) its
first **POST** endpoints, and its first endpoints that **mutate** the ECS
world instead of merely reading pixels off a render target:

1. **`POST /instantiate_primitive`** — spawns one of the engine's 5 built-in
   primitive shapes (Cube/Sphere/Capsule/Cone/Plane —
   `src/Renderer/Primitives/PrimitiveMeshGenerator.h`'s `PrimitiveType`) as a
   new entity, with a caller-chosen display name, a world-space position, and
   an optional parent (looked up by name). Mirrors Unity's
   `GameObject.CreatePrimitive()` plus `transform.position =`/`transform.SetParent()`,
   but reachable over HTTP instead of only from in-process Editor/Game code.

   Request body (`application/json`):
   ```json
   {
     "shape": "cube",
     "name": "MyCube",
     "world_position": { "x": 0.0, "y": 0.0, "z": 0.0 },
     "parent": null
   }
   ```

2. **`POST /delete_entity`** — destroys a live entity (and every descendant of
   it, via the already-existing `ECS/TransformHierarchy.h::DestroyEntityAndDescendants()`),
   looked up **by name**.

   Request body (`application/json`):
   ```json
   { "name": "MyCube" }
   ```

3. Both are debuggable end-to-end via the `gte_send_request` tool's POST
   support (pass a JSON `payload` string) once implemented — this is the
   acceptance test every implementation phase below should actually run
   against a live, running engine instance before calling itself done.

This campaign is explicitly the **first of a growing family** of "engine
command" endpoints — everything built here (the JSON dependency, the
cross-thread command bridge, the entity-by-name lookup/unique-naming
utilities) is deliberately generalized so a THIRD future command (e.g.
`set_transform`, `play_animation`, `list_entities`) slots into the exact same
machinery instead of inventing a new pattern.

---

## Step 2 — The Situation (Where are we now?)

Verified directly against the current source tree (`feature/network-impl`
branch) before writing this plan:

- `src/Network/NetworkServer.h/.cpp` only ever calls `httplib::Server::Get(...)`
  — there is no `Post(...)` registration anywhere yet, and nothing in the
  engine has ever parsed a request BODY (`httplib::Request::body`).
- `src/Network/NetworkRoutes.h/.cpp` is the existing "pure, httplib-independent,
  Tier-1-tested route logic" module — `HandleHelloWorld()`,
  `ResolveCaptureResponseFormat()`, `BuildCaptureJsonBody()`. The latter is a
  **hand-formatted** JSON string (no library, no escaping) — safe today only
  because it exclusively carries integers and base64 text (an alphabet with no
  character that needs JSON-string escaping). This does **not** generalize to
  arbitrary caller-supplied entity/parent names, which absolutely can contain
  characters (`"`, `\`) that need real JSON-string escaping.
- **There is no JSON library vendored in this engine at all.** The engine's
  own scene-serialization format (`src/Scene/SceneTextFormat.h/.cpp`) is a
  deliberately hand-rolled, line-oriented TEXT format specifically because "no
  JSON library is vendored in this engine" (see `README.md`, "Status"). This
  campaign is the first place that actually needs to PARSE untrusted, possibly
  malformed JSON (an incoming POST body) rather than just emit a few
  known-safe fields — a hand-rolled parser is a real, non-trivial piece of new
  code to get right (nesting, escaping, malformed input, numeric edge cases).
  **Locked decision (confirmed with the project owner): vendor a header-only
  third-party JSON library (`nlohmann/json`'s single-header `json.hpp`) via a
  new `cmake/FetchJson.cmake`, mirroring `cmake/FetchHttplib.cmake`'s own
  "single header, fetched straight from GitHub, gitignored, staged into
  `third_party/`" pattern exactly.** This is a deliberate, explicit exception
  to the scene-serialization precedent, made once, for real JSON PARSING only
  — it does not retroactively change `SceneTextFormat`/`BuildCaptureJsonBody`.
- `src/Application/FrameCaptureBridge.h/.cpp` is the ONE existing precedent for
  a network-thread ↔ main-thread bridge: a per-`FrameCaptureKind` mutex +
  condition-variable "slot", `RequestCaptureAndWait()` (network thread,
  blocking with timeout) / `IsCaptureRequested()` + `FulfillPendingRequest()` /
  `FailPendingRequest()` (main thread, called once per frame from
  `Application::Run()`). This is a **read-only, produce-a-copy-of-bytes**
  bridge — it has no concept of a caller-supplied request PAYLOAD (a capture
  request carries no data beyond "which kind"), and no concept of a MUTATION
  outcome (success/failure with a message). The new bridge for this campaign
  needs both.
- `src/Game/Game.h/.cpp` already has `CreatePrimitiveEntity(Renderer&, PrimitiveType)`
  (spawns an entity with an identity `Transform` + `MeshRenderer`, no `Name`,
  no parent) — the exact primitive-spawning machinery this campaign reuses,
  unchanged. There is currently no way to: (a) parse a shape NAME string into
  a `PrimitiveType`, (b) find a live entity BY NAME, (c) guarantee a spawned
  entity's name is unique, or (d) destroy an entity by name. `ECS/TransformHierarchy.h`
  DOES already have `DestroyEntityAndDescendants(Registry&, Entity)` — the
  destroy-side primitive this campaign needs already exists and needs zero
  changes.
- `Transform::position`/`rotation`/`scale` are always PARENT-RELATIVE
  (`Transform.h`) — an unparented entity's local position IS its world
  position. `ECS/TransformHierarchy.h::SetParent(registry, child, newParent,
  worldPositionStays = true)` (default `true`) recomputes `child`'s local
  fields so its WORLD transform is unchanged by the reparent. This combination
  is exactly what lets "set world_position, THEN reparent" produce the
  requested world position with zero new math: set `Transform::position` to
  the requested world position while the entity is still unparented (so
  local == world), then call `SetParent(..., worldPositionStays = true)`,
  which preserves that exact world position while recomputing the local
  fields relative to the new parent.

## Locked Design Decisions (confirmed with the project owner — do not relitigate)

1. **JSON parsing library:** vendor `nlohmann/json`'s single-header `json.hpp`
   (header-only, MIT-licensed, the de facto standard modern-C++ JSON library)
   via a new `cmake/FetchJson.cmake`. See Phase 1.
2. **`parent` name not found:** the entity is still created, **unparented**
   (world space) — the response reports this as a non-fatal warning
   (`parent_requested_but_not_found: true`, `requested_parent_name: "..."`),
   never as an HTTP error. The request as a whole still succeeds.
3. **Entity name uniqueness (programmatic `instantiate_primitive` only):**
   Unity's own `"GameObject"`, `"GameObject (1)"`, `"GameObject (2)"`, ...
   auto-rename convention — mirrors the already-existing precedent in
   `src/Editor/ProjectPanelData.h`'s `MakeUniqueDestinationPath()` (auto-renames
   a dropped file to avoid clobbering an existing one — see `README.md`,
   "Project panel"). This applies **only** to entities created through this
   new programmatic path — it does not retroactively enforce uniqueness on
   entities named through any other existing means (Inspector's "Name" text
   field, `Game::CreateMeshEntityFromGtaFile()`'s per-part naming, etc.), none
   of which are touched by this campaign.
4. **Primitive shapes accepted:** all 5 existing `PrimitiveType` values —
   `cube`, `sphere`, `capsule`, `cone`, `plane` (case-insensitive).
5. **Command bridge concurrency:** a single global slot — only ONE engine
   command (`instantiate_primitive` OR `delete_entity`) may be in flight at a
   time, across the whole bridge, not one slot per kind. A second request
   arriving while one is already pending gets an immediate `503`, exactly
   mirroring `FrameCaptureBridge`'s own per-kind "already pending" outcome,
   just scoped to the whole bridge instead of per-kind (justified: unlike two
   independent read-only capture kinds, two ECS-mutating commands are not
   safe/meaningful to reason about "concurrently" from the caller's point of
   view anyway — they must serialize).
6. **Where commands execute in the frame loop:** **EARLY** — right after SDL
   input-event polling, **before** `Game::Update()` runs. Rationale (this
   feature is primarily for an LLM/automation client, not a human, so
   same-frame consistency beats code-locality-with-FrameCaptureBridge): a
   freshly spawned entity is immediately fully "live" for that same frame
   (`Update()`/Animation/Physics/`RenderSystem::Draw()` all see it with zero
   1-frame lag); a delete takes effect before that frame even simulates the
   doomed entity once more.

---

## Step 3 — The Plan (child phases)

Each phase below is its own Markdown file in this same folder, written to be
independently implementable, independently compile-checkable, and
independently git-committable — in the fixed order below (each phase's code
builds on the previous phase's, so **do not reorder**):

| Phase | File | One-line summary |
|---|---|---|
| 1 | `PHASE1_JSON_DEPENDENCY_AND_REQUEST_PARSING.md` | Vendor `nlohmann/json` (`cmake/FetchJson.cmake`); add pure, Tier-1-tested request-parsing + response-building functions to the existing `src/Network/NetworkRoutes.h/.cpp`. Zero engine/ECS dependency — compiles and is fully testable in total isolation. |
| 2 | `PHASE2_ECS_ENTITY_LOOKUP_AND_UNIQUE_NAMING_UTILITIES.md` | New `src/ECS/EntityQuery.h/.cpp` (`FindEntityByName`/`IsEntityNameInUse`/`MakeUniqueEntityName`) plus a new `PrimitiveMeshGenerator::TryParsePrimitiveTypeName()`. Pure ECS logic, Tier-1-tested, zero networking/Renderer dependency. |
| 3 | `PHASE3_GAME_LEVEL_INSTANTIATE_AND_DELETE_APIS.md` | New `src/Game/EngineCommandResults.h` (plain outcome structs) plus new public `Game::InstantiatePrimitive()`/`Game::DeleteEntityByName()` methods that do the actual ECS mutation. Callable from anywhere in-process (no networking dependency at all) — this is the "what happens" layer. |
| 4 | `PHASE4_ENGINE_COMMAND_BRIDGE_AND_MAIN_LOOP_INTEGRATION.md` | New `src/Application/EngineCommandBridge.h/.cpp` (the cross-thread bridge, mirroring `FrameCaptureBridge`) plus new `src/Application/EngineCommandDispatch.h/.cpp` and the `Application.h/.cpp` wiring (constructor, `Run()`'s early per-frame drain). This is the "how it gets from a background thread to the main thread" layer. |
| 5 | `PHASE5_NETWORK_POST_ROUTES_AND_COMMAND_DISPATCH.md` | `NetworkServer` gains real `httplib::Server::Post(...)` support; the two new routes are registered, wired through Phase 1's parsers + Phase 4's bridge. This is the "how it gets from an HTTP request to a background-thread call" layer — the campaign's actual user-facing surface. |
| 6 | `PHASE6_AUTOMATED_TESTS_DOCS_AND_REGRESSION_SAFETY.md` | Fills any remaining test gaps, updates `AGENTS.md`("Networking")/`README.md`("Status"), full clean build + full `ctest` regression pass, and a REAL end-to-end smoke test against a running engine instance via `gte_send_request` POST. |

### Why this exact ordering

Each phase is chosen so the engine **compiles and its own new tests pass**
before the next phase adds anything on top — nothing here is a "wire it all up
at the very end and hope" design:

- Phase 1 (JSON) needs nothing from anywhere else in the engine — it is
  parsing/formatting pure strings, and is complete and testable on day one.
- Phase 2 (ECS utilities) needs nothing from Phase 1, and nothing
  networking-related at all — pure `Registry`/`Entity`/`PrimitiveType` logic.
- Phase 3 (`Game::` API) depends on Phase 2's utilities (name lookup/dedup,
  shape-name parsing) but has **zero** dependency on Phase 1's JSON work or
  any networking code — `Game::InstantiatePrimitive()`/`DeleteEntityByName()`
  take plain C++ parameters (strings/`Vec3`/bools), not JSON. This means Phase
  3's new methods are directly callable/testable (well, Tier-2-observable —
  they touch a live `Renderer`, see that phase's own testability notes) from
  completely ordinary in-process code, independent of whether Phase 1/4/5 even
  exist yet.
- Phase 4 (bridge + main-loop wiring) depends on Phase 3's `Game::` methods
  existing (its dispatcher calls straight into them) but has **zero**
  dependency on Phase 1's JSON parsing or Phase 5's HTTP routes — it can be
  fully wired and smoke-tested by directly calling
  `EngineCommandBridge::SubmitAndWait()` from a test, with no real HTTP
  request involved at all.
- Phase 5 (HTTP routes) is the thin final layer gluing Phase 1's parsers to
  Phase 4's bridge — by the time this phase starts, every piece it touches
  already compiles and already has its own passing tests.
- Phase 6 is verification/documentation/polish across everything above.

### Non-Goals (explicitly out of scope for this campaign)

- Spawning an imported Mesh asset (`Game::CreateMeshEntityFromGtaFile()`) over
  the network — this campaign is PRIMITIVES only, matching the story's own
  scope ("to start with i want you to implement instantiate_primitive").
  `EngineCommandKind` is deliberately designed (Phase 4) so a THIRD future
  command extends the same enum/dispatch machinery rather than requiring a
  redesign.
- Any command that reads engine state back out (e.g. "list all entities") —
  out of scope; a natural, separate future campaign using the exact same
  `EngineCommandBridge` shape.
- Any authentication/authorization on the new endpoints — the server is
  loopback-only (`127.0.0.1`), exactly like every existing endpoint; this
  campaign changes nothing about that.
- Undo/redo for network-issued mutations — out of scope, matches the Editor's
  own current lack of undo/redo (see `TODO.md`).
- Rotation/scale on `instantiate_primitive` — the story only asked for
  `world_position`; `rotation`/`scale` stay at `Transform{}`'s defaults
  (identity rotation, `Vec3::One()` scale), exactly like
  `Game::CreatePrimitiveEntity()` already produces today. A future command
  revision can add these fields additively (optional, defaulting to
  identity/one) without breaking this campaign's request shape.

---

## Cross-Phase Invariants (every phase must preserve these)

1. **A `NetworkServer` route handler (background thread) still NEVER touches
   `Registry`/`Renderer`/`Game`/any other engine subsystem directly** — it
   only ever calls Phase 1's pure parsing/formatting functions and Phase 4's
   `EngineCommandBridge::SubmitAndWait()`. This is the single most important
   rule in this entire campaign (see AGENTS.md, "Networking") — every phase
   document below repeats it at its own relevant step, on purpose.
2. **Every new engine-mutating code path is reachable and testable WITHOUT a
   real HTTP request in flight** — `Game::InstantiatePrimitive()`/
   `DeleteEntityByName()` (Phase 3) are plain public methods; the whole bridge
   round-trip (Phase 4) is directly exercisable from a test via
   `EngineCommandBridge::SubmitAndWait()` + manually draining it, exactly the
   way `tests/Application/FrameCaptureBridgeTests.cpp` (if present — verify
   and mirror its pattern) already exercises `FrameCaptureBridge` without a
   real socket.
3. **Every failure mode degrades gracefully — never throws past its own
   function, never crashes the engine.** Malformed JSON, an unknown shape
   name, a not-found parent, a not-found delete target, a timed-out/already-
   pending command — all of these are ordinary, expected return values (a
   `bool success` + `std::string errorMessage`, or an HTTP status code), never
   an uncaught exception unwinding into `NetworkServer`'s background thread or
   `Application::Run()`'s main loop.
4. **Every new Tier-1-testable module gets its own test file/additions in the
   SAME phase that introduces it** — see AGENTS.md, "Testability & Regression
   Safety": *"Every change to Tier 1 code must come with a matching test
   change."*
