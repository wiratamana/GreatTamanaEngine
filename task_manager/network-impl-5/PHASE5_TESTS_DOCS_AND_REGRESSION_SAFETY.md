# PHASE5 — Tests, Docs, and Full Regression Safety

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: Phases 1-4 all complete and
individually committed. This is the campaign's final phase — by the end of
it, both new endpoints have real end-to-end HTTP test coverage, every
relevant doc file mentions them, and a full clean build + `ctest` pass
confirms zero regressions anywhere else in the engine.

## Step 1 — The Goal

1. Add real end-to-end HTTP test coverage for `POST /set_entity_trs` and
   `POST /instantiate_light`, extending the EXISTING
   `tests/Network/EngineCommandEndpointsEndToEndTests.cpp` (not a new file).
2. Update `AGENTS.md`'s "Networking" section and `README.md`'s "Status"
   section to describe both new endpoints, mirroring exactly how
   `network-impl-3`'s own endpoints are already documented there.
3. Run a full clean build and full `ctest` regression pass.
4. Run the REAL, live-engine smoke test that directly resolves this
   campaign's own motivating story (this may already be done informally in
   Phase 4's own completion report — this phase's job is to make it
   FORMAL/repeatable and capture it as this campaign's final piece of
   evidence).

## Step 2 — The Situation

- `tests/Network/EngineCommandEndpointsEndToEndTests.cpp`
  (`network-impl-3`'s own file) already has a `FakeEngineCommandStandIn`
  class — a GPU/ECS-free background-thread stand-in for
  `Application::Run()`'s real per-frame drain, used so these tests can
  exercise the REAL `EngineCommandBridge` + REAL `NetworkServer` (over a
  REAL loopback socket) without needing a live `Game`/`Renderer`/Vulkan
  device anywhere. **This stand-in currently only handles
  `InstantiatePrimitive`/`DeleteEntity`** (its own `Run()` method's
  `if (request->kind == EngineCommandKind::InstantiatePrimitive) { ... }
  else { ... }` is a two-way branch, hardcoded for exactly those two kinds)
  — it needs extending for the two new kinds before any new test case in
  this file can exercise them.
- Per Phase 2's own Testability note, `Game::SetEntityTrs()`/
  `InstantiateLight()` are BOTH fully Tier-1-testable directly (no
  `Renderer` needed at all) — this means, unlike
  `InstantiatePrimitive()`'s own accepted "Tier 2, GPU-touching, no
  end-to-end functional test of the REAL Game-layer logic, only of the
  wiring around it" gap (documented explicitly in `network-impl-3`'s own
  `PHASE5_COMPLETION_REPORT.md`), this campaign has NO such gap to accept —
  every layer of BOTH new commands (`NetworkRoutes.h` parsing, `Game::`
  logic, the bridge, the HTTP route) already has, or will have by the end of
  this phase, real, direct, Tier-1 automated coverage. State this
  explicitly in `PHASE5_COMPLETION_REPORT.md` as a genuine improvement over
  the previous campaign's own accepted gap.
- **[v2 correction — verified directly against the current `AGENTS.md`, not
  just inferred]** `AGENTS.md`'s "Networking" section already describes
  `/instantiate_primitive`/`/delete_entity`/`EngineCommandBridge` IN DETAIL
  today (the `network-impl-3` campaign's own Phase 6 added several bullets
  there, not just to `README.md`'s "Status" section as an earlier draft of
  this document assumed) — it does NOT yet mention `/set_entity_trs`/
  `/instantiate_light` specifically, since those don't exist until this
  campaign. This phase's own new bullet (Step 3.3 below) should sit
  alongside those existing `network-impl-3` bullets, not introduce the
  section's first-ever mention of `EngineCommandBridge`/POST routes as the
  original draft's wording implied. Critically, `AGENTS.md`'s existing
  bullet **"A future THIRD engine command (e.g. `set_transform`/
  `play_animation`/`list_entities`) should extend `EngineCommandKind`..."**
  is now STALE once this campaign lands — `set_entity_trs` is exactly the
  `set_transform` it was speculating about, and this campaign actually adds
  the THIRD and FOURTH `EngineCommandKind` values at once (`SetEntityTrs`/
  `InstantiateLight`). Step 3.3 below has an explicit instruction to update
  this bullet rather than leaving it incorrectly describing still-
  hypothetical future work that has, by the end of this phase, already
  happened.
- `README.md`'s "Status" section already has one bullet point per prior
  networking campaign (`network-impl-1` through `network-impl-4`, each
  reproduced in full in this campaign's own `PHASE0_MASTER_STRATEGY.md`
  investigation) — this phase adds ONE MORE bullet, in the same style,
  immediately after the existing `network-impl-4` bullet (`/get_texture`/
  `/list_textures`) and before whatever bullet currently follows it.

## Step 3 — The Plan

### 3.1 — Extend `FakeEngineCommandStandIn` in `EngineCommandEndpointsEndToEndTests.cpp`

Change its `Run()` method's request-handling branch from a two-way
`if`/`else` into a real `switch (request->kind)` covering all four kinds,
adding minimal, self-consistent fake behavior for the two new ones:

- `SetEntityTrs`: track a small in-memory `std::map<std::string, FakeTransformState>`
  (position/rotation-Euler/scale, all defaulting to identity/zero/one)
  alongside the existing `liveNames` set — on a `SetEntityTrs` request,
  look the name up; if absent, produce a `success = false,
  entityNotFound = true` outcome (mirroring the real `Game::SetEntityTrs()`'s
  own "not found" contract just enough to prove the HTTP status-code mapping
  works); if present, apply whichever of translation/rotation/scale the
  request carries (mirroring `hasX` flags) and echo the resulting fake state
  back in the outcome. This does NOT need to replicate real quaternion math
  faithfully — it only needs to prove the wiring (request in, correctly-
  shaped result out), exactly like the existing stand-in's own explicit
  "deliberately NOT trying to replicate Game::InstantiatePrimitive()'s real
  ... logic" precedent already states for the two existing kinds.
- `InstantiateLight`: reuse the SAME `liveNames`/`nextIndex` bookkeeping
  `InstantiatePrimitive` already uses (a light and a primitive are
  indistinguishable to this fake stand-in — both are just "a named thing
  that now exists") — a request with an unrecognized-looking `lightType`
  (i.e., non-empty and not case-insensitively `"directional"`) should
  produce `success = false` so the real HTTP-level `400` mapping has
  something genuine to prove, even though the REAL semantic check lives in
  `Game::InstantiateLight()`, not here.

### 3.2 — New test cases

Add to `EngineCommandEndpointsEndToEndTests.cpp`, mirroring the existing
tests' own naming/structure exactly:

- `SetEntityTrsValidPayloadReturns200AndReflectsChanges` — instantiate a
  light first (reusing the existing instantiate flow), then `POST
  /set_entity_trs` with a `rotation_euler_degrees` only, assert `200`,
  `"changed"."rotation" == true`, `"changed"."translation" == false`.
- `SetEntityTrsMalformedJsonReturns400`.
- `SetEntityTrsUnknownNameReturns404`.
- `SetEntityTrsNoOpRequestStillReturns200WithCurrentTransform` — a request
  with only `{"name": "..."}` (an already-instantiated name), asserting
  `200` and every `"changed"` flag `false` — directly proves
  `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision #6 end to end.
- `InstantiateLightValidPayloadReturns200AndSuccess`.
- `InstantiateLightMalformedJsonReturns400`.
- `InstantiateLightMissingNameReturns400` — proves Locked Design Decision
  #4 (name is REQUIRED) is enforced end to end, not just at the Phase 1 unit
  level.
- Extend the existing `CommandBridgeNullptrReturns503ForBothRoutes`-style
  test (currently named for the two OLD routes) to ALSO assert `503` for
  both new routes when `commandBridge == nullptr` — either widen that
  existing test or add a clearly-named sibling test; either is acceptable,
  but do not silently leave this case uncovered for the two new routes.
- A round-trip test mirroring `FullInstantiateThenDeleteRoundTripBothReturn200`:
  `instantiate_light` a uniquely-named light, `set_entity_trs` it, then
  `delete_entity` it, confirming `200` all three times.

### 3.3 — Update `AGENTS.md`'s "Networking" section

Add a new bullet immediately after the existing "Named Texture Capture"
paragraph (or wherever the section's own existing chronological ordering
places it), briefly describing:

- `POST /set_entity_trs`/`POST /instantiate_light` (`network-impl-5`
  campaign) as the second extension of `EngineCommandBridge` beyond
  `network-impl-3`'s own original two commands — reiterate, briefly, that
  the SAME single-global-slot bridge and SAME "a route handler is a pure
  function of its own request data + `EngineCommandBridge::SubmitAndWait()`"
  rule apply unchanged.
- The one genuinely new architectural note worth calling out here:
  `Game::SetEntityTrs()`/`InstantiateLight()` are the engine's first
  ENGINE-COMMAND methods that are FULLY Tier-1-testable end to end (no
  Renderer/GPU dependency anywhere in the chain) — worth a short, explicit
  mention since it's a genuine, citable improvement in this subsystem's own
  test-coverage completeness over the prior campaign.
- **[v2 addition — closes a real doc-staleness gap found during this
  campaign's own review pass]** Update the EXISTING "A future THIRD engine
  command (e.g. `set_transform`/`play_animation`/`list_entities`) should
  extend `EngineCommandKind`..." bullet (added by `network-impl-3`, still
  present in `AGENTS.md` as of this campaign) — it is now STALE, since this
  campaign is exactly that speculative "future THIRD command" (and, in
  fact, adds a FOURTH one too, in the same campaign). Either fold its
  still-true general guidance ("extend `EngineCommandKind` plus the
  tagged-struct shape rather than inventing a new bridge") into the new
  bullet added above (citing `set_entity_trs`/`instantiate_light` as the
  worked example that guidance predicted, rather than a hypothetical), or
  rewrite it to describe a genuinely still-hypothetical FIFTH command
  instead — do not leave it unmodified, silently describing already-shipped
  functionality as a future possibility.

### 3.4 — Update `README.md`'s "Status" section

Add one new bullet, in the same style/voice as the existing
`network-impl-3`/`network-impl-4` bullets (both fully reproduced in this
campaign's `PHASE0_MASTER_STRATEGY.md`), describing:

- `POST /set_entity_trs` — update translation/rotation/scale on an existing,
  by-name entity, independently (any subset of the three), operating on its
  local (parent-relative) transform; every call's response always echoes
  the entity's full resulting transform (position, rotation as both Euler
  degrees and quaternion, scale) plus which fields this call actually
  changed — including a call that changes nothing, which doubles as a
  lightweight "read the current transform" query.
- `POST /instantiate_light` — spawn a new `DirectionalLight` entity (a
  `light_type` field future-proofs this for a later point/spot light),
  mirroring `instantiate_primitive`'s own name/position/parent contract,
  plus color/illuminance/active fields mapping directly onto
  `DirectionalLight`'s own component fields. A network-spawned light with no
  explicit rotation gets the same "late-afternoon" default rotation the
  Editor's own "Create Directional Light" menu already uses.
- Both reuse the exact same `EngineCommandBridge`/JSON machinery
  `network-impl-3` already built — no new vendored dependency, no new
  cross-thread mechanism.
- Link to `task_manager/network-impl-5/PHASE0_MASTER_STRATEGY.md` for the
  full five-phase campaign writeup (PHASE1 through PHASE5 - PHASE0 itself is
  the orchestrator/reference doc, not a numbered implementation phase),
  matching every prior networking campaign's own closing sentence
  convention.

### 3.5 — Full regression pass

- Full clean build (`cmake --build build`, Working directory:
  `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`).
- Full `ctest` pass (`cd /d
  C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug
  --output-on-failure`) — confirm every existing test (not just this
  campaign's own new ones) still passes, since Phase 2 edited an
  already-shipped method (`CreateDirectionalLightEntity()`)'s
  implementation and Phase 3 edited two already-shipped structs
  (`EngineCommandRequest`/`EngineCommandResult`).

### 3.6 — Final, formal live-engine smoke test

Repeat (or, if Phase 4's own completion report already did this
thoroughly, simply RE-CONFIRM and cite it here rather than duplicating
effort) the exact scenario from this campaign's own motivating story:

1. `run_app_background` the built engine executable.
2. `gte_send_request` `POST /instantiate_light` to spawn a new,
   uniquely-named directional light at a known position with an EXPLICIT
   initial `rotation_euler_degrees`.
3. `gte_send_request` `GET /get_game_view` (or `/get_texture` for a
   currently-registered atmosphere/Sky-View LUT debug texture — see
   `network-impl-4`) and note the visual result.
4. `gte_send_request` `POST /set_entity_trs` to rotate that SAME light to a
   clearly different `rotation_euler_degrees`.
5. `gte_send_request` the SAME capture endpoint again and confirm the
   visual result genuinely changed (the sky/lighting responds to the new
   rotation) — this is the literal, direct resolution of the AI's own
   original complaint quoted in `PHASE0_MASTER_STRATEGY.md`.
6. `gte_send_request` `POST /delete_entity` to clean up.
7. `stop_app_background` the engine.

Write a final `NETWORK_IMPL_5_CAMPAIGN_COMPLETION_REPORT.md` (mirroring
`network-impl-4`'s own top-level completion-report file, alongside this
phase's own `PHASE5_COMPLETION_REPORT.md`) summarizing the whole five-phase
campaign, explicitly quoting the before/after captured images'
description (or attaching/referencing them, however this project's own
report convention already records visual evidence) as the closing proof
this campaign achieved its Step 1 goal.

## Verification for this phase

- Fast compile check for the test-file changes.
- Run the full new/extended test suite in
  `tests/Network/EngineCommandEndpointsEndToEndTests.cpp` specifically, and
  confirm every new case passes.
- Full clean build + full `ctest` pass (see 3.5 above) — THIS is the one
  phase in the whole campaign where a full build/full regression test is
  explicitly warranted (every other phase's own "Verification" section
  intentionally scopes down to a fast compile check only, per this
  campaign's own working agreement).
- Complete the live-engine smoke test (3.6) and capture its evidence in the
  completion report.
- Write `PHASE5_COMPLETION_REPORT.md` AND
  `NETWORK_IMPL_5_CAMPAIGN_COMPLETION_REPORT.md`, `git add`/`git commit`.
