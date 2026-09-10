# PHASE4 — COMPLETION REPORT: `NetworkServer` Routes — `POST /set_entity_trs` + `POST /instantiate_light`

Status: **DONE.** Both new `httplib::Server::Post(...)` routes specified by
`PHASE4_NETWORK_POST_ROUTES_WIRING.md` were registered in
`src/Network/NetworkServer.cpp`'s `RegisterRoutes()`, wired exactly per the
phase document's own reference implementation. Zero new files were created,
`Application.h`/`Application.cpp` were untouched, and `NetworkServer.h`'s
public signature needed no change at all — matching `PHASE0_MASTER_STRATEGY.md`'s
Step 2/Non-Goals expectations exactly.

## What was implemented

### `src/Network/NetworkServer.cpp`

- Added `#include "../Math/Quat.h"` immediately alongside the existing
  `#include "../Math/Vec3.h"` (this file already `#include`s
  `"../Application/EngineCommandBridge.h"`, which in turn already exposes
  `EngineCommandKind::SetEntityTrs`/`InstantiateLight` and the
  `SetEntityTrsParams`/`InstantiateLightParams`/`SetEntityTrsOutcome`/
  `InstantiateLightOutcome` structs via its own transitive
  `"../Game/EngineCommandResults.h"` include — no new include was needed for
  those).
- Registered `server.Post("/set_entity_trs", ...)` immediately after the
  existing `server.Post("/delete_entity", ...)` registration, inside
  `RegisterRoutes()`, following the exact five-step shape documented in the
  phase doc:
  1. `ParseSetEntityTrsRequest(req.body)` (Phase 1) → `400` on `!valid`.
  2. `commandBridge == nullptr` → `503`.
  3. Build an `EngineCommandRequest` of kind `SetEntityTrs`, converting Phase
     1's plain floats into real `Vec3` values right here (the one place that
     boundary is crossed, per `NetworkRoutes.h`'s own "stays Math-free"
     convention).
  4. `EngineCommandBridge::SubmitAndWait()` → `503` (`alreadyPending`) / `504`
     (`timedOut`).
  5. On a real outcome: `outcome.entityNotFound ? 404 : 409` for a
     `success == false` result (the two distinct failure reasons `PHASE2`'s
     own `SetEntityTrsOutcome::entityNotFound` field exists to distinguish);
     on success, builds a `TransformSnapshotView` (Euler degrees via
     `outcome.resultingRotation.ToEulerDegrees()`, plus the raw quaternion's
     own `x`/`y`/`z`/`w` fields) and responds `200` with
     `BuildSetEntityTrsResponseJson(...)`.
- Registered `server.Post("/instantiate_light", ...)` immediately after
  `/set_entity_trs`, following the identical five-step shape, reusing
  `BuildInstantiatePrimitiveResponseJson()` verbatim for its own
  success/failure response body (per Phase 1, Step 3.4 — `InstantiateLightOutcome`'s
  fields line up 1:1 with what that builder already expects), responding
  `outcome.success ? 200 : 400`.
- No signature changes anywhere: `commandBridge` was already an in-scope,
  already-captured lambda-closure variable inside `RegisterRoutes()` — both
  new lambdas simply capture the same variable, exactly as
  `PHASE4`'s own Step 3.2 predicted.

## Deviations from the strategy document

None. Every parser call, field mapping, status-code mapping, and response
builder call was implemented exactly as specified in the phase document's own
Step 3.1 reference implementation — copied essentially verbatim, with only the
addition of a short explanatory comment block above the two new registrations
(mirroring the existing comment style already present above
`/instantiate_primitive`/`/delete_entity`). `edit_line`'s auto-dedup safety net
fired once while inserting the new `#include "../Math/Quat.h"` line (it
detected and removed a leftover duplicate `#include "../Math/Vec3.h"` line
immediately after the inserted block) — verified correct by re-reading the
resulting file immediately afterward; noted here purely for transparency,
consistent with every previous phase's own completion report calling out the
same tooling behavior.

## Verification performed

### 1. Fast, targeted compile check (not a full rebuild)

```
cmake --build build --target CMakeFiles/gte_core.dir/src/Network/NetworkServer.cpp.obj
```
— compiled cleanly, zero warnings/errors.

Since this phase's own change is the campaign's final, user-facing wiring
layer and the manual smoke test below requires a real running executable, a
full `GreatTamanaEngine` link was also performed (not a full *rebuild* of
every target — `gte_core`'s static library and every shader were already
up to date from Phase 1-3's own builds, so this was a cheap incremental link):

```
cmake --build build --target GreatTamanaEngine
```
— linked successfully with no errors.

(Per this phase's own workflow rules, the full test-suite regression pass
remains explicitly Phase 5's job — not repeated here.)

### 2. Manual, real, end-to-end smoke test (`run_app_background` + `gte_send_request`)

Launched `build/GreatTamanaEngine.exe` in the background (PID 21656),
confirmed the embedded HTTP server was up via `GET /http_hello_world`
(`200`, body `hello world`), then ran the exact sequence the phase document's
own "Verification for this phase" section calls for:

**Step 1 — `POST /instantiate_light`:**

Request:
```json
{"light_type":"directional","name":"NetworkTestSun","world_position":{"x":0,"y":5,"z":0}}
```
Response — `HTTP 200`:
```json
{"entity":{"generation":1,"index":1},"name":"NetworkTestSun","parent_requested_but_not_found":false,"requested_parent_name":"","success":true}
```

**Step 2 — `POST /set_entity_trs`:**

Request:
```json
{"name":"NetworkTestSun","rotation_euler_degrees":{"x":10,"y":45,"z":0}}
```
Response — `HTTP 200`:
```json
{"changed":{"rotation":true,"scale":false,"translation":false},"entity":{"generation":1,"index":1},"success":true,"transform":{"position":{"x":0.0,"y":5.0,"z":0.0},"rotation_euler_degrees":{"x":9.999998092651367,"y":45.0,"z":-4.3347222344891634e-07},"rotation_quaternion":{"w":0.9203639030456543,"x":0.08052139729261398,"y":0.3812272250652313,"z":-0.033353060483932495},"scale":{"x":1.0,"y":1.0,"z":1.0}}}
```
Confirms exactly the expected shape: `"changed"` correctly shows only
`rotation: true` (`translation`/`scale` both `false`, matching that the
request supplied only `rotation_euler_degrees`), and
`"transform"."rotation_euler_degrees"` reflects the new values (small
floating-point round-trip noise from the Euler→quaternion→Euler conversion,
e.g. `9.999998...` instead of exactly `10.0`, is expected and harmless).

**Step 3 — `POST /delete_entity` (cleanup):**

Request: `{"name":"NetworkTestSun"}`
Response — `HTTP 200`: `{"entity":{"generation":1,"index":1},"success":true}`

(This cleanup call was issued at the very end, after the visual-comparison
step below, so `NetworkTestSun` stayed alive for the whole smoke test.)

### 3. Visual confirmation that the rendered scene genuinely responds to `set_entity_trs`

The default docked layout's **"Scene"** tab was the active one this session
(confirmed via `GET /get_swapchain`, a screenshot of the whole Editor UI) —
**not** "Game" — which is why a first attempt at `GET /get_game_view`
correctly responded `409` (`capture failed`): the Game view's own offscreen
pass is skipped whenever its panel isn't the visible dock tab (see
`README.md`'s "Visibility-driven rendering" — this is expected, documented
behavior, not a bug in this phase's own routes). Switched the visual check to
capture `AtmosphereSkyViewLut_SceneView` instead (via `GET /get_texture`,
`network-impl-4`), which — being part of the Scene view's own render regime —
was actively updating every frame regardless of which dock tab was focused.

Sequence actually run:

1. `POST /set_entity_trs` with `{"name":"NetworkTestSun","rotation_euler_degrees":{"x":80,"y":45,"z":0}}`
   (sun nearly overhead) → `GET /get_texture?texture_name=AtmosphereSkyViewLut_SceneView`
   captured a **washed-out, pale, near-noon sky** (thin blue-white gradient,
   no warm horizon glow).
2. `POST /set_entity_trs` with `{"name":"NetworkTestSun","rotation_euler_degrees":{"x":5,"y":200,"z":0}}`
   (sun almost at the horizon) → the SAME texture, captured again, now showed
   a **clearly different, warm sunset/sunrise gradient** with a bright glow
   right at the horizon line, a visibly darker/richer blue overhead, and a
   distinct color temperature shift versus the first capture.

This is a direct, positive visual confirmation that a `POST /set_entity_trs`
rotation change on a network-spawned `DirectionalLight` entity genuinely,
live propagates all the way through `DirectionalLightResolver::
ResolveActiveDirectionalLight()` into the real, currently-rendering
atmosphere Sky-View LUT — the exact end-to-end scenario this whole
`network-impl-5` campaign's own motivating story (see `PHASE0_MASTER_STRATEGY.md`)
was written to prove out, achieved with **zero human-in-the-loop manual
Editor interaction** (every step above was a `gte_send_request` HTTP call).

The engine was then stopped cleanly via `stop_app_background(pid: 21656)`.

## What's next

Phase 5 (`PHASE5_TESTS_DOCS_AND_REGRESSION_SAFETY.md`) can now add the
remaining Tier-1 unit test coverage and end-to-end HTTP tests
(`tests/Network/EngineCommandEndpointsEndToEndTests.cpp`), update
`AGENTS.md`/`README.md`, and perform the full clean build + full `ctest`
regression pass this phase's own workflow rules deliberately deferred to it.
No open questions or blockers were found. One incidental, non-blocking
observation worth flagging for Phase 5's own documentation pass: `GET
/get_game_view` legitimately returns `409` whenever the Editor's "Game" dock
tab isn't the currently-active/visible one (as it wasn't during this session) —
a caller wanting a guaranteed-fresh visual confirmation without depending on
which tab happens to be focused should prefer `GET /get_texture` against a
Scene-view-scoped texture name (e.g. `AtmosphereSkyViewLut_SceneView`) or
`GET /get_swapchain`, both of which this phase's own smoke test used
successfully; this is pre-existing, documented `network-impl-2`/`network-impl-4`
behavior, not something this phase introduced or needs to fix.
