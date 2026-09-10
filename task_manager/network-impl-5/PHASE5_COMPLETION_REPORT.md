# PHASE5 — COMPLETION REPORT: Tests, Docs, and Full Regression Safety

Status: **DONE.** Every item in `PHASE5_TESTS_DOCS_AND_REGRESSION_SAFETY.md`'s
own Step 3 plan was completed: end-to-end HTTP test coverage was added for
both `POST /set_entity_trs` and `POST /instantiate_light`, `AGENTS.md`'s
"Networking" section and `README.md`'s "Status" section were both updated, a
full clean build plus a full `ctest` regression pass were run with zero
failures, and the campaign's own final, formal live-engine smoke test
(spawn a light, rotate it via `set_entity_trs`, visually confirm via a
captured texture that the sky genuinely responds) was re-run and its
evidence captured below. This is the campaign's final phase — this report
is followed by `NETWORK_IMPL_5_CAMPAIGN_COMPLETION_REPORT.md`, summarizing
the whole five-phase campaign.

## What was implemented

### 3.1/3.2 — `tests/Network/EngineCommandEndpointsEndToEndTests.cpp` (existing file, extended)

- `FakeEngineCommandStandIn::Run()`'s request-handling branch was rewritten
  from a two-way `if`/`else` (`InstantiatePrimitive`/everything-else) into a
  real `switch (request->kind)` covering all four `EngineCommandKind` values.
  A new small `FakeTransformState` struct (position/rotation-Euler/scale,
  Transform.h's own identity defaults) plus a `std::map<std::string,
  FakeTransformState>` (alongside the pre-existing `liveNames` set) gives the
  fake stand-in just enough state to prove `SetEntityTrs`'s translation/
  rotation/scale change-tracking and echo-back wiring, and `InstantiateLight`
  reuses the exact same `liveNames`/`nextIndex` bookkeeping
  `InstantiatePrimitive` already uses (a light and a primitive are
  indistinguishable to this fake stand-in, per the phase doc's own Step 3.1).
  `Quat::FromEulerDegrees()` is used directly to build the echoed
  `resultingRotation` quaternion — a real, but not-attempting-to-be-a-full-
  reimplementation, use of the engine's own existing Euler helper (the phase
  doc explicitly says this fake stand-in "does NOT need to replicate real
  quaternion math faithfully").
- 11 new test cases were added:
  - `SetEntityTrsValidPayloadReturns200AndReflectsChanges`
  - `SetEntityTrsMalformedJsonReturns400`
  - `SetEntityTrsUnknownNameReturns404`
  - `SetEntityTrsNoOpRequestStillReturns200WithCurrentTransform`
  - `InstantiateLightValidPayloadReturns200AndSuccess`
  - `InstantiateLightMalformedJsonReturns400`
  - `InstantiateLightMissingNameReturns400`
  - `FullInstantiateLightThenSetTrsThenDeleteRoundTripAllReturn200`
  - Plus a widened version of the existing no-bridge test, renamed
    `CommandBridgeNullptrReturns503ForAllFourRoutes` (was
    `...ForBothRoutes`), now also asserting `503` for `/set_entity_trs` and
    `/instantiate_light` when `commandBridge == nullptr`.
- All 16 tests in this file (7 pre-existing `EngineCommandEndpointsEndToEndTest`
  cases + the one no-bridge test, plus the 8 new cases above) pass — see
  Verification below for the exact run.

### 3.3 — `AGENTS.md`, "Networking" section

- The existing, now-stale **"A future THIRD engine command (e.g.
  `set_transform`/`play_animation`/`list_entities`) should extend
  `EngineCommandKind`..."** bullet was replaced in place with a
  comprehensive new bullet describing `POST /set_entity_trs`/`POST
  /instantiate_light` as the actual THIRD-and-FOURTH `EngineCommandKind`
  values this campaign added — explicitly citing them as the worked example
  the old bullet used to describe only hypothetically, folding the old
  bullet's still-true general guidance ("extend `EngineCommandKind` plus the
  tagged-struct shape rather than inventing a new bridge") into the new text
  rather than leaving it stale. The new bullet also covers both endpoints'
  exact contract (fields, status codes, the "no groups present is a valid
  no-op" behavior, the shared `DefaultDirectionalLightRotation()` helper) and
  explicitly calls out the genuine test-coverage improvement over
  `network-impl-3`'s own accepted Tier-2 gap (`Game::SetEntityTrs()`/
  `InstantiateLight()` are both fully Tier-1-testable end to end).
- See "Deviations" below for the one, deliberate positioning choice made
  here versus the phase doc's own suggested placement.

### 3.4 — `README.md`, "Status" section

- One new bullet was added immediately after the existing `network-impl-4`
  bullet (`/get_texture`/`/list_textures`) and before the
  `editor-enchancements-1` bullet that already followed it, in the same
  voice/style as every other networking-campaign bullet, covering both new
  endpoints' contract and linking to
  `task_manager/network-impl-5/PHASE0_MASTER_STRATEGY.md`.

### 3.5 — Full regression pass

- **Full clean build**: `cmake --build build --clean-first` from the project
  root. This is a genuine, from-scratch rebuild of every target (410 files
  cleaned, 409 build steps re-run: `gte_core`, `GreatTamanaEngineTests`,
  `GreatTamanaEngine`, every vendored third-party static library — ImGui,
  ImGuizmo, volk, saba, KTX-Software/basisu/astc-encoder, googletest — and
  every `.vert`/`.frag`/`.comp` shader recompiled to SPIR-V) — **zero
  compile/link errors**.
- **Full `ctest` pass**: `ctest -C Debug --output-on-failure` from `build/`.
  **1248 tests total, 1247 passed, 1 skipped** (the pre-existing,
  documented, machine-gated `PmxLoaderRealModelSmokeTest
  .LoadsAnMmdModelIfPresentOnThisMachine` — unrelated to this campaign, same
  skip every prior campaign's own completion report has recorded) — **zero
  failures, zero regressions** anywhere else in the engine, including every
  test this campaign's own Phases 2/3 edits could plausibly have disturbed
  (`CreateDirectionalLightEntity()`'s refactored implementation,
  `EngineCommandRequest`/`EngineCommandResult`'s two widened structs).

### 3.6 — Final, formal live-engine smoke test

Per the phase doc's own allowance ("if Phase 4's own completion report
already did this thoroughly, simply RE-CONFIRM and cite it here rather than
duplicating effort"): **`PHASE4_COMPLETION_REPORT.md`'s own smoke test
already ran this exact scenario successfully once** (spawn `NetworkTestSun`,
rotate it to `x=80,y=45` — near-overhead — capturing a pale/washed-out
near-noon `AtmosphereSkyViewLut_SceneView`, then rotate to `x=5,y=200` — near
the horizon — capturing a visibly different warm sunset/sunrise gradient
with a horizon glow, then clean up). This phase RE-RAN that same scenario,
independently, end to end, against a freshly built `GreatTamanaEngine.exe`
(the one produced by this phase's own full clean build above) as this
campaign's final piece of formal evidence:

1. `run_app_background`'d `build/GreatTamanaEngine.exe` (PID 17744),
   confirmed the embedded HTTP server was up via `GET /http_hello_world`
   (`200`, body `hello world`).
2. `POST /instantiate_light` with
   `{"light_type":"directional","name":"Phase5SmokeSun","world_position":{"x":0,"y":5,"z":0},"rotation_euler_degrees":{"x":80,"y":45,"z":0}}`
   → `HTTP 200`, `{"success":true,"name":"Phase5SmokeSun",...}`.
3. `GET /list_textures` confirmed which view was actively updating THIS
   session (`"SceneView"`/`"AtmosphereSkyViewLut_SceneView"` at
   `frames_since_update: 0`, the opposite of Phase 4's own session, where
   "Game" happened to be the active dock tab instead of "Scene" — a
   session-to-session difference in which Editor tab happens to be focused,
   not a bug) — confirming this phase's own capture target was genuinely
   live before relying on it.
4. `GET /get_texture?texture_name=AtmosphereSkyViewLut_SceneView` captured a
   **pale, washed-out, near-noon sky** (thin blue-white gradient fading
   toward the horizon, no warm color at all) — the sun nearly overhead at
   `x=80°` pitch.
5. `POST /set_entity_trs` with
   `{"name":"Phase5SmokeSun","rotation_euler_degrees":{"x":5,"y":200,"z":0}}`
   → `HTTP 200`,
   `{"changed":{"rotation":true,"scale":false,"translation":false},"success":true,"transform":{...,"rotation_euler_degrees":{"x":4.999...,"y":-159.999...,"z":0.0},...}}`
   — confirms exactly the expected shape: only `rotation` reported changed,
   the echoed transform reflects the new values (the `y` angle round-trips
   as its equivalent `-160°`, expected/harmless Euler-representation
   aliasing from the Euler→quaternion→Euler conversion, not a bug).
6. `GET /get_texture?texture_name=AtmosphereSkyViewLut_SceneView` (same
   texture, same request, no other change) now captured a **clearly
   different, warm sunset/sunrise gradient**: a bright horizon glow, a
   visibly darker/richer blue overhead, and an obvious color-temperature
   shift versus step 4's capture — the sun now nearly at the horizon at
   `x=5°` pitch.
7. `POST /delete_entity` with `{"name":"Phase5SmokeSun"}` → `HTTP 200`,
   `{"success":true,...}` (cleanup).
8. `stop_app_background(pid: 17744)`.

This is a direct, positive, INDEPENDENTLY-REPRODUCED visual confirmation
that `POST /set_entity_trs` genuinely, live propagates a rotation change on
a network-spawned `DirectionalLight` entity all the way through
`DirectionalLightResolver::ResolveActiveDirectionalLight()` into the
real, currently-rendering atmosphere Sky-View LUT — the exact end-to-end
scenario this whole `network-impl-5` campaign's motivating story (see
`PHASE0_MASTER_STRATEGY.md`) was written to prove out, achieved with
**zero human-in-the-loop manual Editor interaction**, on a freshly, fully
rebuilt executable.

## Deviations from the strategy document

None of substance. One deliberate, documented positioning choice:

1. **The Step 3.3 "new bullet describing both endpoints" and the "update the
   stale THIRD-command bullet" instruction were combined into ONE edit, in
   place at the stale bullet's own original location**, rather than adding a
   brand-new bullet immediately after "Named Texture Capture" (as the phase
   doc's primary suggestion reads) and separately editing the stale bullet
   elsewhere. The phase doc's own v2 addition explicitly sanctions this:
   *"Either fold its still-true general guidance...into the new bullet added
   above...or rewrite it..."* — folding the two into a single in-place
   rewrite of the existing bullet satisfies both instructions at once, is
   arguably the more natural "this is where the `EngineCommandBridge`
   narrative already continues from" placement, and avoids the new content
   duplicating anything the old bullet already said nearby. The doc's own
   fallback wording ("or wherever the section's own existing chronological
   ordering places it") was read as permission for this choice.
2. **The pre-existing no-bridge test was renamed**
   (`CommandBridgeNullptrReturns503ForBothRoutes` →
   `...ForAllFourRoutes`) rather than left with a now-inaccurate name while
   widening its body — the phase doc's own Step 3.2 explicitly allows
   "either widen that existing test or add a clearly-named sibling test";
   widening-plus-rename was chosen so the test's own name stays an accurate,
   self-documenting description of what it asserts. The old name only ever
   appeared in this repository's own build-generated artifacts (`ctest`
   discovery caches, prior completion reports) — no other source file
   referenced it, confirmed via a repository-wide search before renaming.

## Verification performed

1. **Fast, targeted compile check** of the test-file change:
   `cmake --build build --target
   tests/CMakeFiles/GreatTamanaEngineTests.dir/Network/EngineCommandEndpointsEndToEndTests.cpp.obj`
   — compiled cleanly.
2. **Targeted run of the new/extended suite**: `GreatTamanaEngineTests.exe
   --gtest_filter=EngineCommandEndpoints*.*` — **16 tests, all passed**
   (7 pre-existing `EngineCommandEndpointsEndToEndTest` cases + 8 new cases +
   the 1 widened no-bridge test).
3. **Full clean build**: `cmake --build build --clean-first` — 409/409 build
   steps succeeded, zero errors (see "3.5" above for the full target list).
4. **Full `ctest` regression pass**: `ctest -C Debug --output-on-failure` —
   **1248 tests total, 1247 passed, 1 skipped** (pre-existing, documented,
   machine-gated), **zero failures**.
5. **Live-engine smoke test** (Step 3.6 above) — completed successfully
   end to end against the freshly, fully rebuilt `GreatTamanaEngine.exe`,
   independently reproducing Phase 4's own earlier result.

## What's next

Nothing — this is the campaign's final phase. See
`NETWORK_IMPL_5_CAMPAIGN_COMPLETION_REPORT.md` for the full five-phase
campaign summary.
