# PHASE0 — MASTER STRATEGY: `set_entity_trs` / `instantiate_light` over HTTP POST

Campaign folder: `task_manager/network-impl-5/`
Branch: `feature/feature/network-impl`

Prior campaigns this one builds directly on top of (READ THESE FIRST if
anything below is unclear — do not re-derive what they already established):

- `task_manager/network-impl-1/PHASE0_MASTER_STRATEGY.md` — the embedded HTTP
  server itself (`gte::Network::NetworkServer`), loopback-only, background
  thread.
- `task_manager/network-impl-2/PHASE0_MASTER_STRATEGY.md` — the cross-thread
  bridge pattern (`FrameCaptureBridge`) and `GET /get_game_view`/
  `GET /get_swapchain`.
- `task_manager/network-impl-3/PHASE0_MASTER_STRATEGY.md` — **the campaign
  this one most directly extends.** It built the engine's first POST/
  ECS-mutating endpoints (`POST /instantiate_primitive`, `POST
  /delete_entity`), vendored `nlohmann/json` (`cmake/FetchJson.cmake`), and
  built `EngineCommandBridge` (`src/Application/EngineCommandBridge.h/.cpp`) —
  the single, reviewed, thread-safe bridge a network route handler is allowed
  to use to mutate the ECS world on the main thread. Every locked decision,
  file, and struct that campaign produced is assumed to already exist,
  unchanged, in the current tree (verified directly against the current
  source tree while writing this plan — see Step 2 below).
- `task_manager/network-impl-4/PHASE0_MASTER_STRATEGY.md` — added `GET
  /get_texture`/`GET /list_textures` (read-only, GPU-texture-capture
  endpoints) on top of the same `NetworkRoutes.h`/`NetworkServer.cpp` files
  this campaign also touches. Not otherwise load-bearing for this campaign,
  but its `PHASE5` document is a second good reference for "how to add two
  more routes to an already-established `RegisterRoutes()` without
  disturbing the existing ones."
- `AGENTS.md`, section **"Networking"** — the load-bearing thread-safety rule
  this whole campaign must keep satisfying: *"A route handler must be a PURE
  function of its own request data only — it must NEVER touch
  Registry/Renderer/Game/AssetDatabase/IEditorLayer/ImGui/any other engine
  subsystem, directly or indirectly, full stop."* Every new route this
  campaign adds only ever touches `NetworkRoutes.h`'s pure parsers/builders
  and `EngineCommandBridge::SubmitAndWait()` — never anything else
  engine-side, exactly like `/instantiate_primitive`/`/delete_entity` already
  do.

---

## The Story (why this campaign exists)

An LLM operating this engine over HTTP hit a wall it could not code around:

> *"Only `instantiate_primitive` and `delete_entity` POST endpoints exist,
> with no way to rotate transforms or create directional lights via HTTP, so
> I can't spawn or rotate a light purely through the API without new engine
> code... I'll instead use `gte_send_request` against `/get_game_view` or
> `/get_texture` for the Sky-View LUT at two different rotations to visually
> confirm the sky and aerial perspective actually respond live."*

That workaround (asking a human to rotate the light manually in the Editor,
or giving up on the automated check entirely) is exactly the gap this
campaign closes. Two new capabilities, both requested directly by the
project owner:

1. **`set_entity_trs`** — change an existing entity's Translation and/or
   Rotation and/or Scale, independently (translation only, rotation only,
   any combination, or all three at once) — looked up by name, the same way
   `delete_entity` already works.
2. **`instantiate_light`** — spawn a new light entity (today: the engine's
   only implemented light type, `DirectionalLight`) over HTTP, the light
   equivalent of `instantiate_primitive`.

---

## Step 1 — The Goal (Where are we going?)

Give the engine's embedded HTTP server two new POST endpoints, built on
top of the EXISTING `EngineCommandBridge`/`NetworkRoutes.h` machinery
`network-impl-3` already established — no new cross-thread mechanism, no new
vendored dependency, no new CMake target. By the end of this campaign:

1. **`POST /set_entity_trs`** — updates the LOCAL (parent-relative)
   Transform of a live entity, looked up by name:

   ```json
   {
     "name": "MyLight",
     "translation": { "x": 0.0, "y": 5.0, "z": -2.0 },
     "rotation_euler_degrees": { "x": 30.0, "y": 90.0, "z": 0.0 },
     "scale": { "x": 1.0, "y": 1.0, "z": 1.0 }
   }
   ```

   `translation`/`rotation_euler_degrees`/`scale` are each **independently
   optional** — a request may include any subset of the three (including
   none of them, which is a valid, harmless no-op — see Locked Design
   Decision #6 below). When a field IS present, it must supply all of
   `x`/`y`/`z` together (see Locked Design Decision #2). The response always
   echoes back the entity's FULL resulting local transform, whether or not
   this particular call changed it — see Locked Design Decision #7.

2. **`POST /instantiate_light`** — spawns a new light entity (this engine's
   only implemented kind today: `DirectionalLight`):

   ```json
   {
     "light_type": "directional",
     "name": "Sun",
     "world_position": { "x": 0.0, "y": 10.0, "z": 0.0 },
     "rotation_euler_degrees": { "x": 45.0, "y": -30.0, "z": 0.0 },
     "color": { "r": 1.0, "g": 1.0, "b": 1.0 },
     "illuminance_lux": 100000.0,
     "active": true,
     "parent": null
   }
   ```

   Mirrors `POST /instantiate_primitive`'s own shape/name/position/parent
   contract exactly, plus light-specific fields (`color`/`illuminance_lux`/
   `active`, mapping 1:1 onto `ECS/Components/DirectionalLight.h`'s own
   fields) and a `light_type` field that future-proofs the request shape for
   a later, currently-unimplemented light kind (point/spot) without an
   API-breaking change (see Locked Design Decision #9).

3. Both are debuggable end-to-end via the `gte_send_request` tool's POST
   support, exactly like `network-impl-3`'s own endpoints — this is the
   acceptance test every implementation phase below should actually run
   against a live, running engine instance before calling itself done, and
   the one that directly answers the story above: spawn a light, rotate it
   via `set_entity_trs`, and use `gte_send_request /get_texture` (Sky-View
   LUT, or any other atmosphere debug texture — see `network-impl-4`) at two
   different rotations to visually confirm the sky responds, with **zero**
   human-in-the-loop manual Editor interaction required.

---

## Step 2 — The Situation (Where are we now?)

Verified directly against the current source tree (`feature/feature/network-impl`
branch) before writing this plan:

- **`src/Application/EngineCommandBridge.h/.cpp`** already exists, fully
  built and tested (`tests/Application/EngineCommandBridgeTests.cpp`): a
  single-global-slot, mutex + condition-variable bridge with
  `EngineCommandKind` (`InstantiatePrimitive`, `DeleteEntity` today),
  `EngineCommandRequest`/`EngineCommandResult` (plain tagged structs, one
  sibling field per kind — not `std::variant`), `SubmitAndWait()` (network
  thread), `TryPeekPendingCommandRequest()`/`FulfillCommand()` (main thread).
  This bridge is **already generically kind-agnostic in its own mechanics**
  (mutex/condition-variable logic never branches on `kind` at all) — adding
  two more `EngineCommandKind` values needs zero changes to
  `EngineCommandBridge.cpp`'s own implementation, only to the two struct
  definitions in `EngineCommandBridge.h` (see Phase 3).
- **`src/Application/EngineCommandDispatch.h/.cpp`** already exists: the
  ONE place that `switch (request.kind)`es and calls into `Game`. Adding a
  new kind means adding one more `case` here — nothing else.
- **`src/Application/Application.cpp`**'s `Run()` already drains the bridge
  once per frame, generically, EARLY (right after SDL input polling, before
  `Game::Update()`) via:
  ```cpp
  if (const std::optional<EngineCommandRequest> request = m_commandBridge.TryPeekPendingCommandRequest()) {
      const EngineCommandResult result = ExecuteEngineCommand(m_game, m_renderer, *request);
      m_commandBridge.FulfillCommand(result);
  }
  ```
  **This code needs ZERO changes for this campaign** — it already dispatches
  by `request.kind` generically inside `ExecuteEngineCommand()`, regardless
  of how many kinds exist. This is a direct, confirmed consequence of
  `network-impl-3` having deliberately generalized this loop for "a THIRD
  future command" (its own `PHASE0_MASTER_STRATEGY.md` says so explicitly).
  No phase below touches `Application.h`/`Application.cpp` at all.
- **`src/Game/EngineCommandResults.h`** already exists: plain,
  Application-independent outcome structs (`InstantiatePrimitiveOutcome`,
  `DeleteEntityOutcome`) that `Game.h`'s own public methods return. This is
  the natural home for this campaign's two new outcome structs AND (a
  deliberate, explicit generalization — see Locked Design Decision #3) its
  two new **request-parameter** structs too.
- **`src/Game/Game.h/.cpp`** already has `InstantiatePrimitive()`/
  `DeleteEntityByName()` (network-impl-3) plus a THIRD, pre-existing,
  Editor-only method this campaign directly reuses:
  `Entity CreateDirectionalLightEntity()` — spawns a `Transform` (given a
  hardcoded "late-afternoon" default rotation,
  `Quat::FromEulerDegrees(45.0f, -30.0f, 0.0f)`) plus a `DirectionalLight`
  component, named `"Directional Light"` (auto-de-duplicated via
  `EntityQuery.h::MakeUniqueEntityName()`, same convention
  `InstantiatePrimitive()` already uses). **This method needs no `Renderer&`
  parameter at all** — a light has nothing to rasterize — which is exactly
  why `Game::InstantiateLight()` (this campaign, Phase 2) can be **fully
  Tier-1-testable**, unlike `Game::InstantiatePrimitive()` (which stays in
  the accepted "Tier 2, no automated GPU coverage" bucket because it touches
  a live `Renderer`/GPU mesh cache — see `AGENTS.md`, "Testability &
  Regression Safety"). This is a genuine, worth-highlighting quality
  advantage this campaign gets "for free."
- **`src/ECS/Components/Transform.h`**: `position`/`rotation`/`scale` are
  ALWAYS parent-relative (LOCAL) — an entity with no parent has local ==
  world. Every entity `InstantiatePrimitive()`/`InstantiateLight()` produce
  is unparented unless the caller explicitly requests a parent, so "local"
  and "world" already coincide for the overwhelming common case this
  campaign's own story needs (rotate a just-spawned light).
- **`src/Math/Quat.h`**: `Quat::FromEulerDegrees(pitchX, yawY, rollZ)` (all
  in DEGREES) is the engine's one existing, already-used, already-tested
  Euler-angle constructor (`tests/Math/QuatTests.cpp`) — this campaign reuses
  it verbatim, never reimplementing any rotation math. `Quat::ToEulerDegrees()`
  is its inverse, used to build human-readable response JSON.
- **`src/ECS/EntityQuery.h`**: `FindEntityByName()`/`MakeUniqueEntityName()`
  already exist and are reused unchanged by both new commands, exactly like
  `InstantiatePrimitive()`/`DeleteEntityByName()` already do.
- **`src/Network/NetworkRoutes.h/.cpp`** is the existing "pure,
  httplib-independent, Tier-1-tested route logic" module. It already has the
  exact precedent this campaign's own parsers/builders must follow:
  `ParseInstantiatePrimitiveRequest()`/`ParseDeleteEntityRequest()` (request
  parsing via `nlohmann::json`, already vendored — no new JSON work needed),
  `BuildInstantiatePrimitiveResponseJson()`/`BuildDeleteEntityResponseJson()`/
  `BuildGenericErrorResponseJson()` (response building). This file is
  deliberately kept Math/Vec3/Quat-**free** — every parsed field is a plain
  `float`/`bool`/`std::string`, and `NetworkServer.cpp` (never
  `NetworkRoutes.cpp`) is the one place that converts those plain scalars
  into a real `Vec3`/`Quat` when building an `EngineCommandRequest`. This
  campaign's new parsers/builders must preserve that same boundary.
- **`src/Network/NetworkServer.cpp`**'s `RegisterRoutes()` already registers
  `POST /instantiate_primitive`/`POST /delete_entity` as two separate,
  deliberately-NOT-shared-helper `server.Post(...)` lambdas (see that
  phase's own rationale — the two routes have different parsers/builders/
  failure-status mappings, so a forced shared abstraction would cost more
  than it saves). This campaign's two new routes follow the exact same
  "two separate registrations, not a forced shared helper" shape.
- **No brand-new `.h`/`.cpp` FILE is required anywhere in this campaign** —
  every change is an ADDITIVE edit to a file that already exists and is
  already registered in `CMakeLists.txt`/`tests/CMakeLists.txt`. This is a
  meaningfully smaller campaign than `network-impl-3` (which had to invent
  the JSON dependency and the bridge itself from nothing) — confirm this
  explicitly in each phase's own completion report; if a phase's
  implementer finds they think they need a new file, that's a signal to
  stop and re-read this document before proceeding.

---

## Locked Design Decisions (confirmed with the project owner via `ask_questions` — do not relitigate)

1. **Rotation input format for `set_entity_trs`: Euler degrees ONLY.** The
   request key is `rotation_euler_degrees: {"x":pitch,"y":yaw,"z":roll}`
   (degrees, matching `Quat::FromEulerDegrees(pitchX, yawY, rollZ)`'s own
   parameter order/units exactly). There is **no** `rotation_quaternion`
   input field anywhere in this campaign, and no ambiguity-between-two-
   representations validation to write. (The RESPONSE may still ECHO BACK
   a quaternion alongside the Euler angles, purely for the caller's
   convenience/debugging — that is read-only output, not input, and creates
   no ambiguity — see Locked Design Decision #7.)
2. **No partial per-axis edits — each of `translation`/`rotation_euler_degrees`/
   `scale` is all-or-nothing.** If the caller includes the key at all, it
   MUST supply `x`, `y`, AND `z` together, all as JSON numbers — a request
   with e.g. `"translation": {"y": 5.0}` (missing `x`/`z`) is a validation
   FAILURE (400), never "leave x/z unchanged." This removes any ambiguity
   about what a missing axis inside a present object would mean, at the
   cost of the caller having to know/re-supply the other two axes (which
   `set_entity_trs`'s own response — see Decision #7 — makes trivial: query
   with an empty body first, or simply always send a full triplet, which is
   what an LLM caller can trivially do since it already has to compute the
   values it wants anyway).
3. **`set_entity_trs` edits the entity's LOCAL transform only** —
   `Transform::position`/`rotation`/`scale` directly, exactly the semantics
   documented on `Transform.h` itself (parent-relative; local == world for
   an unparented entity). There is **no** hierarchy-aware world-space
   conversion (`ECS/TransformHierarchy.h`'s `ComputeWorldMatrix()`/
   `ComputeWorldTransform()`) anywhere in this campaign — ONLY
   `Transform::position`/`rotation`/`scale` are ever read or written, never
   a parent's chain. This is a deliberate, explicit scope limit (see
   Non-Goals below) — it already fully covers this campaign's own story
   (rotating a light that was just spawned, unparented, by
   `instantiate_light`).
4. **`instantiate_light`'s `name` field is REQUIRED**, exactly mirroring
   `instantiate_primitive`'s own contract — enforced at the `NetworkRoutes.h`
   parsing layer (a missing/empty/non-string `name` is a 400), not inside
   `Game::InstantiateLight()` itself (which keeps its own defensive
   fallback-to-`"Directional Light"` for non-network callers, mirroring
   `Game::InstantiatePrimitive()`'s own identical "the network layer
   enforces required-ness; the Game-layer method itself still degrades
   gracefully as defense in depth" split).
5. **A network-spawned light with no `rotation_euler_degrees` field at all
   gets the SAME "late-afternoon" default rotation
   (`Quat::FromEulerDegrees(45.0f, -30.0f, 0.0f)`) the Editor's own "Create
   Directional Light" menu already uses** (`Game::CreateDirectionalLightEntity()`),
   NOT an identity rotation. This is a deliberate divergence from
   `InstantiatePrimitive()`'s own "identity unless the caller says
   otherwise" convention, made because a light with an identity rotation
   (pointing straight down `+Z`, not downward at all) is a much less useful
   default for something whose entire purpose is to shine ONTO the scene
   from above — see Phase 2 for how this shared literal is extracted into
   one small private helper reused by BOTH `CreateDirectionalLightEntity()`
   (Editor path, UNCHANGED behavior) and `InstantiateLight()` (this
   campaign's new network path), so the two can never silently drift apart.
6. **A `set_entity_trs` request that specifies NONE of
   `translation`/`rotation_euler_degrees`/`scale` at all is still VALID
   (never a 400)** — it is a harmless no-op that changes nothing, and (see
   Decision #7) still returns the entity's current full transform. This
   deliberately turns `set_entity_trs` into a de-facto "read the current
   transform of a named entity" query when called with just `{"name":
   "X"}` — a genuinely useful side effect, worth calling out explicitly in
   documentation (Phase 5), even though a dedicated read-only
   "get_entity_transform" endpoint is out of scope for this campaign (see
   Non-Goals).
7. **`set_entity_trs`'s response ALWAYS echoes the entity's full resulting
   local transform** (position, rotation as BOTH Euler degrees and raw
   quaternion, and scale), regardless of which fields this particular call
   actually changed — plus a `"changed": {"translation":bool,"rotation":bool,"scale":bool}`
   object so a caller can tell exactly what this call did versus what it
   merely reports. This is the direct fix for the story's own complaint (an
   LLM needing a SEPARATE screenshot-based check just to confirm a rotation
   "took") — the HTTP response itself is now sufficient confirmation for
   the transform values (a visual screenshot check remains valuable for
   confirming the RENDERED effect, e.g. the sky's aerial perspective, which
   is a fundamentally different kind of confirmation this endpoint cannot
   replace and isn't trying to).
8. **Command bridge concurrency is unchanged** — the existing SINGLE GLOBAL
   slot (`network-impl-3`'s own Locked Design Decision #5) now serializes
   FOUR command kinds instead of two (`InstantiatePrimitive`, `DeleteEntity`,
   `SetEntityTrs`, `InstantiateLight`), with the exact same "a second
   request of ANY kind while one is pending gets an immediate 503" contract.
   No change to `EngineCommandBridge.cpp`'s own logic is needed for this —
   see Step 2 above.
9. **`instantiate_light`'s request body includes a `light_type` field NOW**
   (default/omitted means `"directional"`; case-insensitive; any other
   non-empty value is a validation failure: `"unsupported light_type '...'
   - only 'directional' is currently supported"`), even though
   `DirectionalLight` is the engine's only implemented light component
   today. This future-proofs the request shape for a later point/spot light
   without an API-breaking change for existing callers — mirroring
   `PrimitiveType`'s own multi-value validation shape
   (`TryParsePrimitiveTypeName()`), and matching the project owner's own
   explicit preference.
10. **Where commands execute in the frame loop: unchanged** — still the
    EARLY per-frame drain in `Application::Run()`, right after SDL
    input-event polling, before `Game::Update()` (see Step 2 above — this
    campaign adds no new call site here at all, the existing generic one
    already covers it).

---

## Step 3 — The Plan (child phases)

Each phase below is its own Markdown file in this same folder, written to be
independently implementable, independently compile-checkable, and
independently git-committable — in the fixed order below (each phase's code
builds on the previous phase's, so **do not reorder**):

| Phase | File | One-line summary |
|---|---|---|
| 1 | `PHASE1_NETWORK_ROUTES_REQUEST_PARSING_AND_RESPONSE_BUILDING.md` | Pure, JSON-parsing/response-building functions for both new endpoints, added to the existing `src/Network/NetworkRoutes.h/.cpp`. Zero Game/ECS/bridge dependency — compiles and is fully Tier-1-testable in total isolation, exactly like every existing function in that file. |
| 2 | `PHASE2_GAME_LEVEL_SET_ENTITY_TRS_AND_INSTANTIATE_LIGHT_APIS.md` | New request/outcome structs in `src/Game/EngineCommandResults.h`, plus new public `Game::SetEntityTrs()`/`Game::InstantiateLight()` methods (`src/Game/Game.h/.cpp`) that do the actual ECS mutation. Callable from anywhere in-process (no networking dependency at all) — and, notably, BOTH new methods are fully Tier-1-testable (neither touches a live `Renderer`). |
| 3 | `PHASE3_ENGINE_COMMAND_BRIDGE_AND_DISPATCH_EXTENSION.md` | Extends `src/Application/EngineCommandBridge.h` (two new `EngineCommandKind` values + payload fields — reusing Phase 2's own structs directly, NOT re-declaring them) and `src/Application/EngineCommandDispatch.cpp` (two new `switch` cases). Confirms `Application.h/.cpp` need ZERO changes. |
| 4 | `PHASE4_NETWORK_POST_ROUTES_WIRING.md` | `NetworkServer.cpp`'s `RegisterRoutes()` gains the two new `server.Post(...)` registrations, wiring Phase 1's parsers + Phase 3's bridge together — the campaign's actual user-facing surface. |
| 5 | `PHASE5_TESTS_DOCS_AND_REGRESSION_SAFETY.md` | Fills remaining test gaps (Tier-1 unit tests for Phases 1/2, end-to-end HTTP tests extending `tests/Network/EngineCommandEndpointsEndToEndTests.cpp`), updates `AGENTS.md`("Networking")/`README.md`("Status"), full clean build + full `ctest` regression pass, and a REAL end-to-end smoke test against a running engine instance via `gte_send_request` — including the exact scenario from the story (spawn a light, rotate it, visually confirm via a captured texture). |

### Why this exact ordering

- Phase 1 (JSON parsing/response building) needs nothing from anywhere else
  — it is pure string/`nlohmann::json` manipulation, exactly like
  `network-impl-3`'s own Phase 1, and is complete and independently testable
  on day one.
- Phase 2 (`Game::` methods) needs nothing from Phase 1 (no JSON anywhere in
  `Game.h/.cpp`) and nothing networking-related at all — pure
  `Registry`/`Entity`/`Transform`/`DirectionalLight` logic, reusing
  `EntityQuery.h`/`Quat::FromEulerDegrees()` unchanged.
- Phase 3 (bridge + dispatch) depends on Phase 2's structs/methods existing
  (it embeds Phase 2's own request/outcome structs directly and calls
  straight into Phase 2's `Game::` methods) but has **zero** dependency on
  Phase 1's JSON parsing — it is fully exercisable via a direct
  `EngineCommandBridge::SubmitAndWait()` call in a test, no real HTTP
  request involved, exactly like `network-impl-3`'s own Phase 4.
- Phase 4 (HTTP routes) is the thin final layer gluing Phase 1's parsers to
  Phase 3's bridge — by the time this phase starts, every piece it touches
  already compiles and already has its own passing tests.
- Phase 5 is verification/documentation/polish across everything above,
  including the real, live, running-engine smoke test that directly
  resolves the story this whole campaign exists to fix.

### Non-Goals (explicitly out of scope for this campaign)

- **World-space TRS mutation** (a hierarchy-aware world→local conversion for
  `set_entity_trs`, mirroring `InstantiatePrimitive()`'s own
  "world_position, then reparent with `worldPositionStays`" trick) — out of
  scope per Locked Design Decision #3. A future revision MAY add a
  `"space": "world"` opt-in field additively, without breaking this
  campaign's default (`"local"`) behavior, if a real future need arises.
- **A dedicated, read-only `GET`/`POST` "get entity transform" endpoint** —
  Locked Design Decision #6 already gives every caller a de-facto version of
  this for free via `set_entity_trs`'s own always-populated response, which
  is judged sufficient for now.
- **A second light type** (point/spot) — `light_type` exists now purely as
  a forward-compatible field (Locked Design Decision #9); no new
  `ECS/Components/PointLight.h` or similar is created by this campaign.
- **Reparenting an EXISTING entity** via `set_entity_trs` (changing its
  `Transform::parent`) — this campaign's two endpoints only ever SET a
  parent at CREATION time (`instantiate_light`'s own `parent` field, mirroring
  `instantiate_primitive`'s identical field) — never re-parent an
  already-live entity. A future `set_entity_parent` command is a natural,
  separate follow-up using the exact same machinery, not built here.
- **Any authentication/authorization** on the new endpoints — the server
  stays loopback-only (`127.0.0.1`), exactly like every existing endpoint;
  this campaign changes nothing about that.
- **Undo/redo** for network-issued mutations — out of scope, matches every
  prior networking campaign's own identical exclusion (see `TODO.md`).

---

## Cross-Phase Invariants (every phase must preserve these)

1. **A `NetworkServer` route handler (background thread) still NEVER touches
   `Registry`/`Renderer`/`Game`/any other engine subsystem directly** — it
   only ever calls Phase 1's pure parsing/formatting functions and Phase 3's
   `EngineCommandBridge::SubmitAndWait()`. Repeated at every phase's own
   relevant step on purpose, per `AGENTS.md`'s "Networking" section.
2. **Every new engine-mutating code path is reachable and testable WITHOUT a
   real HTTP request in flight** — `Game::SetEntityTrs()`/`InstantiateLight()`
   (Phase 2) are plain public methods, directly callable/testable with a
   real `Game`/`Registry` and NO `Renderer` at all (unlike
   `InstantiatePrimitive()`); the whole bridge round-trip (Phase 3) is
   directly exercisable via `EngineCommandBridge::SubmitAndWait()` + manually
   draining it, exactly like `tests/Application/EngineCommandBridgeTests.cpp`
   already does for the two existing kinds.
3. **Every failure mode degrades gracefully — never throws past its own
   function, never crashes the engine.** Malformed JSON, an unrecognized
   `light_type`, a not-found entity name, an entity found but missing a
   `Transform` component, a not-found parent, a timed-out/already-pending
   command — all of these are ordinary, expected return values (a `bool
   success` + `std::string errorMessage`, or an HTTP status code), never an
   uncaught exception.
4. **Every new Tier-1-testable module gets its own test file/additions in
   the SAME phase that introduces it** — see `AGENTS.md`, "Testability &
   Regression Safety": *"Every change to Tier 1 code must come with a
   matching test change."* (Phase 5 is still where the END-TO-END HTTP tests
   land, since those need Phases 1/3/4 all present at once — but each of
   Phases 1-3 should add its OWN direct unit test coverage for what it
   itself introduces, not defer everything to Phase 5 — see each phase's own
   "Verification" section.)
5. **No brand-new `.h`/`.cpp` production file is created anywhere in this
   campaign** — every change is an additive edit to a file that already
   exists (see Step 2's own note on this). If any phase's implementer finds
   themselves about to `write_file` a brand-new production source file, stop
   and re-read this document — that is a signal something has drifted from
   the plan.
