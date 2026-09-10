# NETWORK_IMPL_5 — CAMPAIGN COMPLETION REPORT

Campaign folder: `task_manager/network-impl-5/`. Parent/reference document:
`PHASE0_MASTER_STRATEGY.md`. Five implementation phases (PHASE1 through
PHASE5 — PHASE0 itself is the orchestrator/reference document, not a
numbered implementation phase), all now complete and committed.

## The story this campaign closes

An LLM operating this engine over HTTP hit a wall: only
`instantiate_primitive` and `delete_entity` existed, with no way to rotate a
transform or spawn a light purely through the API — so it could not spawn or
rotate a light and visually confirm the sky/lighting responds without asking
a human to do it manually in the Editor. This campaign closes that gap
completely, with **zero human-in-the-loop manual Editor interaction**
required for the exact scenario the story describes — proven twice,
independently (once in Phase 4, once again in Phase 5's own final smoke
test), against two different freshly built executables.

## What the campaign built, end to end

Two new POST endpoints on the engine's embedded, loopback-only HTTP server:

- **`POST /set_entity_trs`** — updates an existing, by-name entity's LOCAL
  (parent-relative) `Transform`: `translation`/`rotation_euler_degrees`
  (Euler degrees only)/`scale`, each independently optional and
  all-or-nothing when present. The response always echoes the entity's
  FULL resulting local transform (position, rotation as both Euler degrees
  and a raw quaternion, scale) plus a `"changed"` object reporting exactly
  which of the three groups this call actually touched — a request
  specifying none of them is a valid, harmless no-op that doubles as a
  de-facto "read the current transform" query. `404` for an unknown name,
  `409` for a name that resolves to a live entity with no `Transform`
  component, `400` for malformed JSON/an incomplete group.
- **`POST /instantiate_light`** — spawns a new `DirectionalLight` entity
  (the engine's only implemented light kind today), mirroring
  `/instantiate_primitive`'s own `name`/`world_position`/`parent` contract,
  plus a `light_type` field (`""`/`"directional"` today, case-insensitive —
  future-proofing for a later point/spot light without an API-breaking
  change) and `color`/`illuminance_lux`/`active` fields mapping 1:1 onto
  `DirectionalLight`'s own component fields. A network-spawned light with no
  explicit rotation gets the SAME "late-afternoon" default rotation the
  Editor's own "Create Directional Light" menu already uses, via one small
  shared helper (`DefaultDirectionalLightRotation()`) both paths call, so
  they can never silently drift apart.

Both endpoints are built ENTIRELY on top of machinery `network-impl-3`
already established — the SAME single-global-slot `EngineCommandBridge`, the
SAME "a route handler is a pure function of its own request data plus
`EngineCommandBridge::SubmitAndWait()`" rule, the SAME already-vendored
`nlohmann/json`. **No new file, no new cross-thread mechanism, no new
vendored dependency was needed anywhere in this five-phase campaign** — a
smaller, more surgical campaign than `network-impl-3` by design, exactly as
`PHASE0_MASTER_STRATEGY.md` predicted.

## Phase-by-phase summary

- **PHASE1 — `NetworkRoutes.h`/`.cpp`: request parsing + response
  building.** Added `ParseSetEntityTrsRequest()`/`ParseInstantiateLightRequest()`
  (pure, JSON-in/plain-scalars-out, zero Game/ECS dependency),
  `TransformSnapshotView`/`BuildSetEntityTrsResponseJson()`, and reused
  `BuildInstantiatePrimitiveResponseJson()` verbatim for `instantiate_light`'s
  own response. 22 new Tier-1 unit tests, all passing; zero regressions in
  the 43 pre-existing tests in the same file.
- **PHASE2 — Game-level `SetEntityTrs()`/`InstantiateLight()` APIs.** Added
  four new plain request/outcome structs to `src/Game/EngineCommandResults.h`
  and two new public `Game` methods doing the real ECS mutation — both
  **fully Tier-1-testable** (neither touches a live `Renderer`), a genuine
  quality improvement over `InstantiatePrimitive()`'s own accepted "Tier 2,
  GPU-touching" bucket. Extracted `DefaultDirectionalLightRotation()` from
  `CreateDirectionalLightEntity()` (behavior-preserving, verified by a
  dedicated regression test) so the Editor path and the new network path
  share one literal. 14 new tests, all passing.
- **PHASE3 — `EngineCommandBridge`/`EngineCommandDispatch` extension.**
  Added `EngineCommandKind::SetEntityTrs`/`InstantiateLight` plus matching
  sibling fields on `EngineCommandRequest`/`EngineCommandResult` (reusing
  Phase 2's own structs directly), and two new `switch` cases in
  `ExecuteEngineCommand()`. Confirmed, as predicted, that
  `Application.h`/`.cpp` needed ZERO changes — the existing per-frame drain
  was already fully generic. 2 new round-trip tests, all passing.
- **PHASE4 — `NetworkServer` routes wiring.** Registered
  `server.Post("/set_entity_trs", ...)`/`server.Post("/instantiate_light",
  ...)` in `RegisterRoutes()`, the five-step shape (parse → bridge-available
  check → build request → `SubmitAndWait()` → map outcome to status code +
  response body) mirroring `/instantiate_primitive`/`/delete_entity` exactly.
  Ran the campaign's first live-engine smoke test here — spawned
  `NetworkTestSun`, rotated it via two different `set_entity_trs` calls, and
  visually confirmed via `GET /get_texture` (`AtmosphereSkyViewLut_SceneView`)
  that the captured sky genuinely, visibly changed between a near-overhead
  and a near-horizon sun angle.
- **PHASE5 — Tests, docs, and full regression safety (this phase).**
  Extended `tests/Network/EngineCommandEndpointsEndToEndTests.cpp`'s
  `FakeEngineCommandStandIn` from a two-way branch into a real four-way
  `switch`, added 8 new end-to-end HTTP test cases plus widened the existing
  no-bridge test to cover all four routes (16 tests total in that file, all
  passing). Updated `AGENTS.md`'s "Networking" section (folding the now-
  stale "future THIRD command" bullet into a comprehensive description of
  what actually shipped) and `README.md`'s "Status" section. Ran a full
  clean build (`cmake --build build --clean-first`, 409/409 steps, zero
  errors) and a full `ctest` regression pass (**1248 tests, 1247 passed, 1
  pre-existing machine-gated skip, zero failures**). Independently
  re-ran the campaign's own motivating smoke test end to end against the
  freshly rebuilt executable, reproducing Phase 4's result a second time.

## Final proof: the motivating story, resolved

**Before** (sun rotated to `x=80°,y=45°` — nearly overhead) — the captured
`AtmosphereSkyViewLut_SceneView` texture shows a pale, washed-out,
near-noon sky: a thin blue-white gradient with no warm color anywhere.

**After** (the SAME light, rotated via a SINGLE `POST /set_entity_trs` call
to `x=5°,y=200°` — nearly at the horizon) — the SAME texture, captured
again with no other change, shows a clearly, unambiguously DIFFERENT
sky: a bright warm glow right at the horizon line, a visibly darker/richer
blue overhead, and an obvious color-temperature shift versus the first
capture.

Every step of this — spawning the light, rotating it, and confirming the
visual result — was performed via `gte_send_request` HTTP calls only. No
human ever opened the Editor UI, dragged a gizmo, or clicked a menu. This is
the literal, direct, twice-independently-reproduced resolution of the AI's
own original complaint quoted in `PHASE0_MASTER_STRATEGY.md`.

## Testability posture: a genuine improvement over the prior campaign

`network-impl-3`'s own `PHASE5_COMPLETION_REPORT.md` explicitly accepted a
gap: `Game::InstantiatePrimitive()` touches a live `Renderer`/GPU mesh
cache, so its real Game-layer logic had no automated functional test — only
the wiring around it (via a GPU/ECS-free `FakeEngineCommandStandIn`) was
covered, with the real semantic behavior verified only informally via manual
`gte_send_request` smoke testing. **This campaign has no such gap.**
`Game::SetEntityTrs()` and `Game::InstantiateLight()` take no `Renderer&`
parameter at all and touch no GPU resource — every layer of both new
commands (`NetworkRoutes.h` parsing, `Game::` logic, the bridge, the HTTP
route) now has real, direct, automated Tier-1 coverage, with the manual
live-engine smoke test serving only to confirm the RENDERED visual effect
(something no unit test could ever assert), not to paper over an otherwise
untested code path.

## Regression safety

- Full clean build: 409/409 targets built with zero errors (`gte_core`,
  `GreatTamanaEngineTests`, `GreatTamanaEngine`, every vendored third-party
  library, every shader).
- Full `ctest` pass: **1248 tests total, 1247 passed**, 1 pre-existing,
  documented, machine-gated smoke test skipped
  (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`,
  unrelated to this campaign) — **zero failures, zero regressions**
  anywhere else in the engine.

## Non-goals (unchanged from `PHASE0_MASTER_STRATEGY.md`, still out of scope)

World-space TRS mutation, a dedicated read-only "get entity transform"
endpoint, a second light type (point/spot), reparenting an existing entity
via `set_entity_trs`, and any authentication/authorization — all explicitly
out of scope for this campaign, unchanged from `PHASE0_MASTER_STRATEGY.md`'s
own "Non-Goals" section. None of these were revisited or relitigated during
implementation.

## Files touched across the whole campaign

No brand-new production `.h`/`.cpp` file was created anywhere in this
five-phase campaign, confirming `PHASE0_MASTER_STRATEGY.md`'s own prediction:

- `src/Network/NetworkRoutes.h`/`.cpp` (Phase 1)
- `src/Game/EngineCommandResults.h`, `src/Game/Game.h`/`.cpp` (Phase 2)
- `src/Application/EngineCommandBridge.h`,
  `src/Application/EngineCommandDispatch.cpp` (Phase 3)
- `src/Network/NetworkServer.cpp` (Phase 4)
- `tests/Network/NetworkRoutesTests.cpp` (Phase 1),
  `tests/Game/GameEntityCommandsTests.cpp` (Phase 2),
  `tests/Application/EngineCommandBridgeTests.cpp` (Phase 3),
  `tests/Network/EngineCommandEndpointsEndToEndTests.cpp` (Phase 5)
- `AGENTS.md`, `README.md` (Phase 5)
- `task_manager/network-impl-5/PHASE1_COMPLETION_REPORT.md` through
  `PHASE5_COMPLETION_REPORT.md`, plus this file.

## Closing note

This campaign is complete. All five phases are implemented, tested,
documented, and committed. The engine's embedded HTTP server now supports
spawning a light and rotating any named entity's transform purely over
HTTP, with automated test coverage at every layer and two independent,
successful live-engine visual confirmations that the change genuinely
propagates all the way to the rendered sky.
