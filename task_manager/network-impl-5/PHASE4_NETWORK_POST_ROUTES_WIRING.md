# PHASE4 — `NetworkServer` Routes: `POST /set_entity_trs` + `POST /instantiate_light`

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: Phase 1's request parsers/
response builders (`NetworkRoutes.h/.cpp`) and Phase 3's extended
`EngineCommandBridge`/`EngineCommandKind`. This phase is the campaign's
actual user-facing surface — by the end of it, both new HTTP endpoints are
real and working.

## Step 1 — The Goal

Register two new `httplib::Server::Post(...)` routes in
`NetworkServer.cpp`'s `RegisterRoutes()`, each doing exactly the same
five-step shape the existing two routes already follow:

1. Parse the request body via Phase 1's parser.
2. If `m_commandBridge`/`commandBridge` is `nullptr`, respond `503`.
3. Build an `EngineCommandRequest` (converting Phase 1's plain floats into
   real `Vec3`/`Quat` values HERE — this is the one place that boundary is
   crossed, per `NetworkRoutes.h`'s own "stays Math-free" convention).
4. `EngineCommandBridge::SubmitAndWait()`.
5. Map `alreadyPending` → `503`, `timedOut` → `504`, otherwise inspect the
   real outcome and respond with the correct status code + Phase 1's
   response builder.

## Step 2 — The Situation

- `NetworkServer.cpp`'s existing two `server.Post(...)` registrations
  (`/instantiate_primitive`/`/delete_entity`, reproduced in full in
  `PHASE0_MASTER_STRATEGY.md`'s Step 2) are the exact template to mirror —
  read them again immediately before writing this phase's own two routes.
- `NetworkServer.cpp` already `#include`s `"../Application/EngineCommandBridge.h"`
  and `"../Math/Vec3.h"` — this phase additionally needs
  `#include "../Math/Quat.h"` (new, for `Quat::FromEulerDegrees()` at the
  `/set_entity_trs`/`/instantiate_light` call sites — verify the exact
  relative path other `#include`s in this same file use for sibling
  headers, e.g. how `"../Math/Vec3.h"` is already spelled there).
- Per Phase 0's Locked Design Decision #7 and Phase 1's own
  `TransformSnapshotView` struct, the `/set_entity_trs` route additionally
  needs to compute BOTH an Euler-degrees AND a raw-quaternion view of the
  resulting rotation for its response — `Quat::ToEulerDegrees()` (already
  existing, `Math/Quat.h`) is what produces the Euler view; the raw
  quaternion's own `x`/`y`/`z`/`w` fields are used directly for the other.
- Per `PHASE2`'s `SetEntityTrsOutcome::entityNotFound` field, this route's
  own failure-status mapping needs to distinguish TWO different
  `success == false` cases (unlike `/delete_entity`'s single-case mapping):
  `entityNotFound == true` → `404`; `entityNotFound == false` (but
  `success == false`, meaning the entity exists but has no `Transform`) →
  `409`.
- `instantiate_light`'s success/failure response reuses
  `BuildInstantiatePrimitiveResponseJson()` verbatim (Phase 1, Step 3.4) —
  its own route handler's response-building call is textually IDENTICAL to
  `/instantiate_primitive`'s own, just fed `InstantiateLightOutcome`'s
  fields instead of `InstantiatePrimitiveOutcome`'s (which happen to have
  matching names/types for every field this builder consumes).

## Step 3 — The Plan

### 3.1 — `RegisterRoutes()` additions in `NetworkServer.cpp`

Add two new registrations, immediately after the existing
`server.Post("/delete_entity", ...)` registration inside `RegisterRoutes()`:

```cpp
server.Post("/set_entity_trs", [commandBridge](const httplib::Request& req, httplib::Response& res) {
    const ParsedSetEntityTrsRequest parsed = ParseSetEntityTrsRequest(req.body);
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
    request.kind = EngineCommandKind::SetEntityTrs;
    request.setEntityTrs.name = parsed.name;
    request.setEntityTrs.hasTranslation = parsed.hasTranslation;
    request.setEntityTrs.translation = Vec3{ parsed.translationX, parsed.translationY, parsed.translationZ };
    request.setEntityTrs.hasRotationEulerDegrees = parsed.hasRotationEulerDegrees;
    request.setEntityTrs.rotationEulerDegrees =
        Vec3{ parsed.rotationPitchXDegrees, parsed.rotationYawYDegrees, parsed.rotationRollZDegrees };
    request.setEntityTrs.hasScale = parsed.hasScale;
    request.setEntityTrs.scale = Vec3{ parsed.scaleX, parsed.scaleY, parsed.scaleZ };

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

    const SetEntityTrsOutcome& outcome = submit.result->setEntityTrs;
    if (!outcome.success) {
        // See PHASE0/PHASE2's own note: TWO distinct failure reasons get
        // TWO distinct status codes - a not-found NAME is 404 (mirrors
        // /delete_entity's own convention), while a found-but-Transform-less
        // entity is 409 (the entity positively exists, this operation just
        // cannot apply to it).
        res.status = outcome.entityNotFound ? 404 : 409;
        res.set_content(BuildGenericErrorResponseJson(outcome.errorMessage), "application/json");
        return;
    }

    const Vec3 resultEulerDegrees = outcome.resultingRotation.ToEulerDegrees();
    TransformSnapshotView snapshot;
    snapshot.positionX = outcome.resultingPosition.x;
    snapshot.positionY = outcome.resultingPosition.y;
    snapshot.positionZ = outcome.resultingPosition.z;
    snapshot.rotationEulerXDegrees = resultEulerDegrees.x;
    snapshot.rotationEulerYDegrees = resultEulerDegrees.y;
    snapshot.rotationEulerZDegrees = resultEulerDegrees.z;
    snapshot.rotationQuatX = outcome.resultingRotation.x;
    snapshot.rotationQuatY = outcome.resultingRotation.y;
    snapshot.rotationQuatZ = outcome.resultingRotation.z;
    snapshot.rotationQuatW = outcome.resultingRotation.w;
    snapshot.scaleX = outcome.resultingScale.x;
    snapshot.scaleY = outcome.resultingScale.y;
    snapshot.scaleZ = outcome.resultingScale.z;

    res.status = 200;
    res.set_content(BuildSetEntityTrsResponseJson(true, "", outcome.entityIndex, outcome.entityGeneration,
        outcome.translationChanged, outcome.rotationChanged, outcome.scaleChanged, snapshot), "application/json");
});

server.Post("/instantiate_light", [commandBridge](const httplib::Request& req, httplib::Response& res) {
    const ParsedInstantiateLightRequest parsed = ParseInstantiateLightRequest(req.body);
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
    request.kind = EngineCommandKind::InstantiateLight;
    request.instantiateLight.lightType = parsed.lightType;
    request.instantiateLight.requestedName = parsed.name;
    request.instantiateLight.worldPosition = Vec3{ parsed.worldX, parsed.worldY, parsed.worldZ };
    request.instantiateLight.hasRotationEulerDegrees = parsed.hasRotationEulerDegrees;
    request.instantiateLight.rotationEulerDegrees =
        Vec3{ parsed.rotationPitchXDegrees, parsed.rotationYawYDegrees, parsed.rotationRollZDegrees };
    request.instantiateLight.color = Vec3{ parsed.colorR, parsed.colorG, parsed.colorB };
    request.instantiateLight.illuminanceLux = parsed.illuminanceLux;
    request.instantiateLight.active = parsed.active;
    request.instantiateLight.hasParent = parsed.hasParent;
    request.instantiateLight.parentName = parsed.parentName;

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

    const InstantiateLightOutcome& outcome = submit.result->instantiateLight;
    res.status = outcome.success ? 200 : 400;
    // network-impl-5 campaign - deliberately reuses
    // BuildInstantiatePrimitiveResponseJson() verbatim (see NetworkRoutes.h,
    // Phase 1, Step 3.4) - InstantiateLightOutcome's own fields line up
    // 1:1 with what that builder already expects.
    res.set_content(BuildInstantiatePrimitiveResponseJson(outcome.success, outcome.errorMessage,
        outcome.entityIndex, outcome.entityGeneration, outcome.resolvedName,
        outcome.parentRequestedButNotFound, outcome.requestedParentName), "application/json");
});
```

### 3.2 — No signature changes needed anywhere

Unlike Phase 4/5 of `network-impl-3` (which had to ADD `EngineCommandBridge*`
as a brand-new constructor/`RegisterRoutes()` parameter), THIS campaign adds
no new parameter anywhere — `commandBridge` already flows into
`RegisterRoutes()` and is already captured by the existing two POST
lambdas' closures. This phase's two new lambdas simply capture the SAME
already-in-scope `commandBridge` variable, unchanged.

### 3.3 — `README.md`/`AGENTS.md` updates are Phase 5's job, not this phase's

Resist the urge to touch documentation here — Phase 5 owns the single,
final documentation pass across the whole campaign, exactly mirroring
`network-impl-3`'s own Phase 5 convention.

## Verification for this phase

- Fast compile check.
- **Do the manual, real, end-to-end smoke test right now, even before Phase
  5's own formal pass**: `run_app_background` the built engine executable,
  then use `gte_send_request` with a JSON `payload` to:
  1. `POST /instantiate_light` with
     `{"light_type":"directional","name":"NetworkTestSun","world_position":{"x":0,"y":5,"z":0}}`
     and confirm a `200` + `success: true` response.
  2. `POST /set_entity_trs` with
     `{"name":"NetworkTestSun","rotation_euler_degrees":{"x":10,"y":45,"z":0}}`
     and confirm a `200` response whose `"changed"` object shows
     `"rotation": true`, `"translation": false`, `"scale": false`, and whose
     `"transform"."rotation_euler_degrees"` reflects the new values.
  3. `POST /delete_entity` with `{"name":"NetworkTestSun"}` to clean up,
     confirming `200`.
  4. As an extra sanity check directly answering this campaign's own
     motivating story, spawn a light, then call `/set_entity_trs` twice with
     two clearly different `rotation_euler_degrees` values, and between the
     two calls use `gte_send_request /get_game_view` (or `/get_texture` for
     an atmosphere/Sky-View LUT debug texture, if one is currently
     registered this session — see `network-impl-4`) to visually confirm the
     scene's lighting genuinely changes between the two rotations. Include
     the literal request/response bodies AND a brief note on the visual
     comparison in `PHASE4_COMPLETION_REPORT.md`.
  `stop_app_background` the engine afterward.
- Write `PHASE4_COMPLETION_REPORT.md` (include the literal request/response
  bodies observed during the manual smoke test above, as evidence), `git
  add`/`git commit`.
