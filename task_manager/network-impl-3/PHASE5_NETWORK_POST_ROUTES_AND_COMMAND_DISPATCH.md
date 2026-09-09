# PHASE5 — `NetworkServer` POST Support + The Two New Routes

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: Phase 1's request parsers /
response builders (`NetworkRoutes.h/.cpp`) and Phase 4's `EngineCommandBridge`
(+ its wiring into `NetworkServer`'s constructor). This phase is the
campaign's actual user-facing surface — by the end of it, `POST
/instantiate_primitive` and `POST /delete_entity` are real, working HTTP
endpoints.

## Step 1 — The Goal

Register two new `httplib::Server::Post(...)` routes in
`NetworkServer.cpp`'s `RegisterRoutes()`, each doing EXACTLY the following,
and nothing else:

1. Parse the request body via Phase 1's `ParseInstantiatePrimitiveRequest()`/
   `ParseDeleteEntityRequest()`. On failure, respond `400` with Phase 1's
   `BuildGenericErrorResponseJson()`.
2. If `m_commandBridge` is `nullptr` (mirrors the existing
   `captureBridge == nullptr` → `503` handling for `/get_game_view`/
   `/get_swapchain`), respond `503`.
3. Build an `EngineCommandRequest`, call `EngineCommandBridge::SubmitAndWait()`.
4. Map `SubmitResult::alreadyPending` → `503`, `timedOut` → `504`, otherwise
   inspect the real `EngineCommandResult` and respond `200` (success) or `400`/`404`
   (failure — see per-route status mapping below) with Phase 1's
   `BuildInstantiatePrimitiveResponseJson()`/`BuildDeleteEntityResponseJson()`.

## Step 2 — The Situation

- `NetworkServer.cpp`'s `RegisterRoutes()` currently only ever calls
  `server.Get(...)`. `httplib::Server` (already vendored, see
  `third_party/httplib/httplib.h`) also exposes `server.Post(path, handler)`
  with the identical handler signature
  `(const httplib::Request& req, httplib::Response& res)` — verify this
  exact signature against the actually-vendored header before writing the new
  registrations (same defensive-verification discipline this file's own
  existing header comment already models for `Get`/`bind_to_port`/etc).
- `httplib::Request::body` (a `std::string`) is the raw POST body — already
  implicitly relied upon by this phase's design; confirm the member name/type
  against the vendored header.
- The existing `RegisterCaptureRoute()` helper is a **shared** function
  because `/get_game_view`/`/get_swapchain` are byte-for-byte identical apart
  from `path`/`FrameCaptureKind`. The two NEW routes are **not** identical to
  each other (different request parser, different response builder, different
  failure-status mapping for a not-found case) — do **not** force them into a
  single shared helper the way the capture routes are; two separate,
  similarly-shaped `server.Post(...)` registrations, each calling its own
  Phase 1 parser/builder pair, is the correct amount of duplication here (a
  premature shared abstraction across two routes that only superficially
  resemble each other is exactly the kind of "extracted too early" mistake
  this codebase's own conventions warn against — see AGENTS.md's
  `BonePoseMath.h` precedent: *"extract only once a real second caller needs
  it"* — these two routes are not that kind of duplicate).

## Step 3 — The Plan

### 3.1 — `RegisterRoutes()` additions in `NetworkServer.cpp`

Add two new registrations, immediately after the existing
`RegisterCaptureRoute(...)` calls inside `RegisterRoutes()`:

```cpp
server.Post("/instantiate_primitive", [commandBridge](const httplib::Request& req, httplib::Response& res) {
    const ParsedInstantiatePrimitiveRequest parsed = ParseInstantiatePrimitiveRequest(req.body);
    if (!parsed.valid) {
        res.status = 400;
        res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
        return;
    }
    if (commandBridge == nullptr) {
        res.status = 503;
        res.set_content(BuildGenericErrorResponseJson("engine command bridge not available"), "application/json");
        return;
    }

    EngineCommandRequest request;
    request.kind = EngineCommandKind::InstantiatePrimitive;
    request.instantiatePrimitive.shape = parsed.shape;
    request.instantiatePrimitive.requestedName = parsed.name;
    request.instantiatePrimitive.worldPosition = Vec3{ parsed.worldX, parsed.worldY, parsed.worldZ };
    request.instantiatePrimitive.hasParent = parsed.hasParent;
    request.instantiatePrimitive.parentName = parsed.parentName;

    const EngineCommandBridge::SubmitResult submit = commandBridge->SubmitAndWait(request);
    if (submit.alreadyPending) {
        res.status = 503;
        res.set_content(BuildGenericErrorResponseJson("another engine command is already in progress"), "application/json");
        return;
    }
    if (submit.timedOut) {
        res.status = 504;
        res.set_content(BuildGenericErrorResponseJson("engine command timed out"), "application/json");
        return;
    }

    const InstantiatePrimitiveOutcome& outcome = submit.result->instantiatePrimitive;
    res.status = outcome.success ? 200 : 400;
    res.set_content(BuildInstantiatePrimitiveResponseJson(outcome.success, outcome.errorMessage,
        outcome.entityIndex, outcome.entityGeneration, outcome.resolvedName,
        outcome.parentRequestedButNotFound, outcome.requestedParentName), "application/json");
});

server.Post("/delete_entity", [commandBridge](const httplib::Request& req, httplib::Response& res) {
    const ParsedDeleteEntityRequest parsed = ParseDeleteEntityRequest(req.body);
    if (!parsed.valid) {
        res.status = 400;
        res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
        return;
    }
    if (commandBridge == nullptr) {
        res.status = 503;
        res.set_content(BuildGenericErrorResponseJson("engine command bridge not available"), "application/json");
        return;
    }

    EngineCommandRequest request;
    request.kind = EngineCommandKind::DeleteEntity;
    request.deleteEntity.name = parsed.name;

    const EngineCommandBridge::SubmitResult submit = commandBridge->SubmitAndWait(request);
    if (submit.alreadyPending) {
        res.status = 503;
        res.set_content(BuildGenericErrorResponseJson("another engine command is already in progress"), "application/json");
        return;
    }
    if (submit.timedOut) {
        res.status = 504;
        res.set_content(BuildGenericErrorResponseJson("engine command timed out"), "application/json");
        return;
    }

    const DeleteEntityOutcome& outcome = submit.result->deleteEntity;
    // 404 (not 400) specifically for "no such entity" - a not-found lookup
    // is a distinct, well-known HTTP status from a generic bad request, and
    // is what lets a caller (an LLM/script) tell "you typo'd the JSON shape"
    // (400) apart from "that name doesn't exist right now" (404) without
    // parsing the error string. An EMPTY name (the other DeleteEntityByName()
    // failure case) is unreachable here in practice, since
    // ParseDeleteEntityRequest() above already rejects an empty "name" field
    // as a 400 - Game::DeleteEntityByName()'s own empty-name guard is
    // defense in depth for its OTHER (non-network) callers, not something
    // this route can actually trigger.
    res.status = outcome.success ? 200 : 404;
    res.set_content(BuildDeleteEntityResponseJson(outcome.success, outcome.errorMessage,
        outcome.deletedEntityIndex, outcome.deletedEntityGeneration), "application/json");
});
```

Add the necessary `#include`s at the top of `NetworkServer.cpp`:
`"../Application/EngineCommandBridge.h"` (already added in Phase 4, confirm
it's there), `"../Math/Vec3.h"` (for the `Vec3{...}` construction — verify the
exact include path used elsewhere in this codebase for `Vec3`).

### 3.2 — Update `RegisterRoutes()`'s own signature/threading

`RegisterRoutes()` already takes `FrameCaptureBridge* captureBridge` — extend
its signature to also take `EngineCommandBridge* commandBridge`, and update
its ONE call site inside `NetworkServer`'s constructor to pass
`m_commandBridge` through (this constructor already stores the pointer per
Phase 4's Step 3.5 — this step just threads it the last few lines into the
route-registration function itself).

### 3.3 — Update `NetworkServer`'s own constructor doc comment

`NetworkServer.h`'s constructor doc comment currently only explains
`captureBridge`'s "defaulted, non-owning pointer... nullptr means no
engine-state-touching routes are wired up" contract. Extend this same comment
to explain `commandBridge` identically (nullptr → `/instantiate_primitive`/
`/delete_entity` both respond `503` rather than crashing) — do not leave the
new parameter undocumented while the old one stays fully documented.

### 3.4 — Update `README.md` is Phase 6's job, not this phase's

Resist the urge to touch `README.md`/`AGENTS.md` here — Phase 6 owns the
single, final documentation pass across the whole campaign, so a reviewer
looking at THIS phase's diff sees only the actual code change, and Phase 6's
diff is the one, easy-to-review place documentation catches up wholesale.

## Verification for this phase

- Fast compile check.
- Extend `tests/Network/NetworkServerTests.cpp` (existing file, real
  end-to-end HTTP coverage — see its own file header comment, "a real ...
  server... bound to an OS-assigned ephemeral port") with new cases for both
  routes: a fully valid `POST /instantiate_primitive` (assert `200`, assert
  the response JSON parses and `success == true`); a malformed-JSON body
  (`400`); a request with `commandBridge == nullptr` (construct a
  `NetworkServer` the old way, with only a capture bridge or neither, and
  confirm `503`); a `POST /delete_entity` for a name that was never created
  (`404`); a full round-trip (`instantiate_primitive` a uniquely-named
  primitive, then `delete_entity` that exact name, confirming `200` both
  times) — this last case is the first REAL end-to-end proof this whole
  campaign's cross-thread bridge actually works against a live, real
  `Application`-less `NetworkServer` + a real, directly-constructed `Game`/
  `Renderer` test harness. **CONFIRMED (second-iteration review): no such
  harness currently exists anywhere in this test suite** -
  `tests/Network/CaptureEndpointsEndToEndTests.cpp` deliberately does NOT
  stand up a real `Renderer`; it drives a fake, GPU-free stand-in
  (`FakeMainThreadStandIn`) instead, specifically because this engine has no
  automated way to construct a live `VkDevice`/`Renderer` in a test binary yet
  (see `tests/CMakeLists.txt`'s own "Tier 2 (GPU-dependent) tests:
  intentionally not implemented yet" section, and `AGENTS.md`/`TESTING.md`'s
  "the absence of automated Tier 2 coverage should never itself slow down or
  stop feature work"). Do NOT attempt to build a headless-Vulkan
  `GpuTestFixture` as part of THIS campaign to close this gap - accept it as
  a Tier 2 gap covered only by the manual `gte_send_request` smoke test below,
  and say so explicitly in `PHASE5_COMPLETION_REPORT.md`.
- **Do the manual, real, end-to-end smoke test right now, even before Phase
  6**: `run_app_background` the built engine executable, then use
  `gte_send_request` with a JSON `payload` to actually `POST
  http://127.0.0.1:8080/instantiate_primitive` with a real body (e.g.
  `{"shape":"sphere","name":"NetworkTestSphere","world_position":{"x":1,"y":2,"z":3}}`)
  and confirm a `200`+success JSON response; then `POST /delete_entity` with
  `{"name":"NetworkTestSphere"}` and confirm `200`+success; then
  `gte_send_request /get_game_view` (from `network-impl-2`) to visually
  confirm a spawned-then-not-yet-deleted sphere is actually visible in the
  captured frame, if convenient, as an extra sanity check that the entity is
  genuinely live in the running scene, not just reported as "created" without
  actually rendering. `stop_app_background` the engine afterward.
- Write `PHASE5_COMPLETION_REPORT.md` (include the literal request/response
  bodies observed during the manual smoke test above, as evidence), `git
  add`/`git commit`.
