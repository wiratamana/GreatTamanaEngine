# PHASE6 — Automated Tests, Documentation, and Full Regression Pass

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: Phases 1-5, all complete and
individually committed. This is the campaign's closing phase.

## Step 1 — The Goal

Close every testing gap deliberately deferred by Phases 1-5, bring
`AGENTS.md`/`README.md` up to date (matching every prior networking campaign's
own closing-phase precedent —
`task_manager/network-impl-1/PHASE4_AUTOMATED_TESTS_AND_REGRESSION_SAFETY.md`,
`task_manager/network-impl-2/PHASE6_AUTOMATED_TESTS_DOCS_AND_REGRESSION_SAFETY.md`
— read both before starting, and mirror their structure/level of detail), and
run a full, clean build + full `ctest` regression pass across the WHOLE
engine, in more than one CMake configuration.

## Step 2 — The Situation

By this point:

- Phase 1 added JSON parsing/response-building, fully Tier-1-tested in
  isolation.
- Phase 2 added `EntityQuery.h` + `TryParsePrimitiveTypeName()`, fully
  Tier-1-tested in isolation.
- Phase 3 added `Game::InstantiatePrimitive()`/`DeleteEntityByName()` — the
  former is Tier 2 (touches a live `Renderer`), the latter may or may not have
  gained a dedicated Tier-1 test already (see Phase 3's own Step 3.4 — check
  its completion report for what was actually decided/done there).
- Phase 4 added `EngineCommandBridge` + `EngineCommandDispatch`, with a new
  Tier-1 test file for the bridge itself.
- Phase 5 added the two real HTTP routes, extended
  `tests/Network/NetworkServerTests.cpp`, and already ran ONE manual
  end-to-end smoke test via `gte_send_request`.
- `AGENTS.md`'s "Networking" section and `README.md`'s "Status" section still
  only describe the `network-impl-1`/`network-impl-2` state of the world
  (`GET /http_hello_world`, `GET /get_swapchain`, `GET /get_game_view`) — this
  campaign's two new POST endpoints, and the new `EngineCommandBridge`
  pattern, are not documented there yet.

## Step 3 — The Plan

### 3.1 — Close any remaining test gaps

- If Phase 3's `DeleteEntityByName()` did not yet get a dedicated Tier-1 test
  (check `PHASE3_COMPLETION_REPORT.md`), add
  `tests/Game/GameEntityCommandsTests.cpp` now: construct a real `Game`
  instance (no `Renderer` needed for this method), use `GetRegistry()` to
  hand-create a couple of entities with `Name` components (and a parent/child
  relationship, to prove `DestroyEntityAndDescendants()`'s recursive behavior
  is actually reached), then call `DeleteEntityByName()` and assert: the
  named entity AND its children are gone (`Registry::IsAlive()` false for
  all); a name that was never used returns `success == false` with the
  expected message; an empty name returns `success == false`.
- If Phase 5's own end-to-end test coverage (`tests/Network/NetworkServerTests.cpp`)
  was deferred for lack of an existing live-`Renderer` test harness pattern,
  resolve that gap now: either build the missing harness (check whether
  `tests/Network/CaptureEndpointsEndToEndTests.cpp` already has one worth
  reusing — network-impl-2's own Phase-6-equivalent report should say) or
  make an explicit, documented decision to leave real HTTP-round-trip
  coverage of `/instantiate_primitive`/`/delete_entity` as a Tier 2 gap
  (covered only by the manual `gte_send_request` smoke test), same as this
  engine already accepts for `Buffer`/`RenderTexture`/etc — write this
  decision down explicitly in this phase's own completion report either way,
  do not leave it silently unresolved.
- Re-read every phase's own "Tests" section above and confirm each one's new
  test file is actually present in `tests/CMakeLists.txt`'s source list (a
  quick `search_in_dir` for each new test file's name against
  `tests/CMakeLists.txt` is enough to confirm this mechanically).
- **Also confirm every new NON-test `.cpp`/`.h` file from Phases 1-5 is
  registered in the ROOT `CMakeLists.txt`'s `target_sources(gte_core PRIVATE
  ...)` list** (`add_library(gte_core STATIC ...)` block - this project does
  NOT glob for source files, see Phase 2/Phase 3/Phase 4's own
  "CRITICAL — CMakeLists.txt registration" notes) - specifically
  `src/ECS/EntityQuery.h/.cpp` (Phase 2), `src/Game/EngineCommandResults.h`
  (Phase 3), and `src/Application/EngineCommandBridge.h/.cpp` +
  `src/Application/EngineCommandDispatch.h/.cpp` (Phase 4). If the full clean
  build in 3.4 below already succeeds this is implicitly proven, but a quick
  `search_in_dir` for each file name against the root `CMakeLists.txt` first
  is a cheap, fast way to catch this specific mistake before burning time on
  a full rebuild.

### 3.2 — `AGENTS.md` — extend the existing "Networking" section

Do **not** rewrite the section — append new bullets in the same voice/style,
directly below the existing `GTE_ENABLE_NETWORK` bullet (the section's last
one today). Cover, at minimum:

- **POST support exists now, and the new "pure function of its own request
  data" rule extends to it unchanged**: a POST route handler parses its own
  request BODY (via `NetworkRoutes.h`'s new `ParseInstantiatePrimitiveRequest()`/
  `ParseDeleteEntityRequest()`) but still never touches
  `Registry`/`Renderer`/`Game` directly — it only ever calls
  `EngineCommandBridge::SubmitAndWait()`, the new campaign's own sanctioned
  bridge, exactly mirroring `FrameCaptureBridge`'s existing "the ONE sanctioned
  exception" precedent bullet, just for a SECOND, independent bridge.
- **`EngineCommandBridge` (`src/Application/EngineCommandBridge.h/.cpp`) is the
  cross-thread bridge for ECS-MUTATING network requests** — contrast directly
  with `FrameCaptureBridge`'s read-only/produce-bytes shape: a single global
  slot (not one per kind — a locked, deliberate design choice, see this
  campaign's own `PHASE0_MASTER_STRATEGY.md`), carrying a real caller-supplied
  request payload and a real success/failure outcome. `Application::Run()`
  drains at most one pending command per frame, EARLY — right after SDL input
  polling, BEFORE `Game::Update()` — a deliberate ordering choice (unlike
  `FrameCaptureBridge`'s own checks, which run later, interleaved with
  rendering) so a network-spawned/deleted entity is fully consistent for the
  rest of that exact frame.
- **`POST /instantiate_primitive`/`POST /delete_entity`** — one-paragraph
  summary of the request/response JSON shapes and status codes (mirror
  `network-impl-2`'s own README bullet style for `/get_swapchain`/
  `/get_game_view` exactly), plus a pointer at
  `task_manager/network-impl-3/PHASE0_MASTER_STRATEGY.md` for the full
  campaign writeup.
- **A future THIRD engine command** should extend `EngineCommandKind` +
  `EngineCommandRequest`/`EngineCommandResult`'s tagged-struct shape (Phase 4)
  rather than inventing a new bridge — this is the generalization this
  campaign was explicitly designed to enable (see PHASE0's own framing).
- **JSON parsing now exists via a vendored `nlohmann/json`** (`cmake/FetchJson.cmake`) —
  note explicitly that this is a deliberate, narrow exception to the
  "no JSON library, hand-rolled formats only" precedent `SceneTextFormat`/
  `BuildCaptureJsonBody()` established, made specifically because this
  campaign needs to PARSE untrusted/malformed input (not just emit a few
  known-safe fields) — a future contributor should not read this as
  permission to casually reach for `nlohmann::json` everywhere else in the
  engine without the same justification.

### 3.3 — `README.md` — extend the existing "Status" section

Add one new bullet, in the same voice/detail-level as the existing
`network-impl-1`/`network-impl-2` bullets (*"The engine now has its first
real networking feature..."* / *"The engine's embedded HTTP server now has
its first engine-state-touching endpoints..."*) — summarize: POST support;
the two new endpoints and what each does; the `EngineCommandBridge` pattern
in one sentence; a pointer at this campaign's `PHASE0_MASTER_STRATEGY.md`.

### 3.4 — Full regression pass

- Full clean build (`cmake --build build`, or a from-scratch
  `cmake -S . -B build` + build if convenient) with the DEFAULT CMake
  configuration.
- Full `ctest -C Debug --output-on-failure` run — confirm the total test
  count increased by exactly the number of new test cases added across
  Phases 1-6, and that ZERO previously-passing tests regressed.
- A second full configure+build+`ctest` pass with `-DGTE_ENABLE_NETWORK=OFF`
  — confirm `src/Network/`'s own tests still compile and pass unchanged (per
  the existing "class always compiles, only the production call site is
  gated" precedent this switch already follows), and confirm the engine
  executable itself still starts up fine with networking compiled OFF
  (`run_app_background` briefly, `stop_app_background` after confirming the
  window opens) — this specifically guards against Phase 4's `Application.h`/
  `.cpp` changes having accidentally introduced a hard (non-`#if`-gated)
  dependency on the network module.
- A final, REAL end-to-end smoke test against the DEFAULT build configuration
  (networking ON): `run_app_background` the engine, then via
  `gte_send_request`:
  1. `POST /instantiate_primitive` with a full valid payload including a
     `parent` name that does NOT exist yet — confirm `200`,
     `parent_requested_but_not_found: true`.
  2. `POST /instantiate_primitive` AGAIN with the exact same `"name"` as step
     1 — confirm `200` and a DIFFERENT `resolvedName` (the `" (1)"` suffix),
     proving the auto-dedup path works over real HTTP, not just in a unit
     test.
  3. `POST /instantiate_primitive` a THIRD entity meant to be the parent from
     step 1 (matching name), THEN `POST /instantiate_primitive` a fourth
     entity naming that exact parent — confirm `200` and
     `parent_requested_but_not_found: false` this time.
  4. `GET /get_game_view` (existing `network-impl-2` endpoint) — visually
     confirm (`load_image` on the resulting capture, if the tool surfaces a
     savable file, or simply eyeball the returned image content block) that
     spawned primitives are actually visible/rendered.
  5. `POST /delete_entity` for each entity created above — confirm `200` for
     each, then `GET /get_game_view` again to visually confirm they're gone.
  6. `POST /delete_entity` for a name already deleted (or never created) —
     confirm `404`.
  7. `POST /instantiate_primitive` with a deliberately malformed JSON body
     (e.g. missing `"shape"`) — confirm `400` with a human-readable
     `"error"` message.
  `stop_app_background` the engine when done.
- Write `PHASE6_COMPLETION_REPORT.md` including the literal request/response
  evidence from every numbered step above (this is the campaign's final,
  reviewable proof it actually works end-to-end, not just "the code compiles
  and unit tests pass"), then `git add`/`git commit` — this commit is the
  campaign's natural closing commit.

## Step 4 — Optional follow-up note (write this down, do not implement it)

If, during this phase's own testing, it becomes clear a future consumer would
benefit from a `list_entities`/`get_entity` READ endpoint (e.g. to look up an
entity's current resolved name/parent without needing to remember what
`instantiate_primitive` returned earlier), note this explicitly as a
suggested next campaign in this phase's completion report — do NOT implement
it as part of this campaign (see `PHASE0_MASTER_STRATEGY.md`'s own
"Non-Goals" list). This keeps the door open for `EngineCommandKind` to grow a
`ListEntities`/`GetEntity` variant later, following the exact same
bridge/dispatch shape this campaign already built.
