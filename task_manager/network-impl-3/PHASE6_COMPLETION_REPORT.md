# PHASE6 — COMPLETION REPORT: Automated Tests, Docs, and Full Regression Pass

Parent: `PHASE0_MASTER_STRATEGY.md`
Phase document: `PHASE6_AUTOMATED_TESTS_DOCS_AND_REGRESSION_SAFETY.md`
Branch: `feature/network-impl`

## Summary

Implemented Phase 6 of the `network-impl-3` campaign exactly as specified —
this is the campaign's closing phase. Closed the one remaining test gap
(`Game::DeleteEntityByName()`'s own dedicated Tier-1 test, deferred by Phase
3), extended `AGENTS.md`'s "Networking" section and `README.md`'s "Status"
section to document everything Phases 1-5 built, ran a full clean build +
full `ctest` regression pass in TWO CMake configurations (default, and
`-DGTE_ENABLE_NETWORK=OFF`), and performed a real, end-to-end smoke test
against a live, running `GreatTamanaEngine.exe` instance via `gte_send_request`
— `POST /instantiate_primitive` → `POST /delete_entity` → `GET
/get_swapchain` visual confirmation, exactly as the phase document's own
"Full regression pass" section calls for.

## What was done

### 1. Closed the remaining test gap — `tests/Game/GameEntityCommandsTests.cpp` (new file)

Phase 3's own completion report explicitly deferred a dedicated Tier-1 test
for `Game::DeleteEntityByName()` to this phase (it needs no live
`Renderer`/GPU device at all, unlike `InstantiatePrimitive()`, which stays in
this codebase's accepted "Tier 2, no automated coverage yet" bucket). Added
three tests, following the phase document's own suggested shape:

- `DeleteEntityByNameDestroysTheNamedEntityAndItsDescendants` — constructs a
  real `Game` (no `Renderer` needed), hand-builds a parent/child pair via
  `GetRegistry()` plus an unrelated sibling entity, calls
  `DeleteEntityByName()` on the parent's name, and asserts the parent AND its
  child are destroyed (`Registry::IsAlive()` false for both) while the
  unrelated sibling survives untouched — proving
  `DestroyEntityAndDescendants()` is reached transitively and scoped
  correctly.
- `DeleteEntityByNameFailsForANameThatWasNeverUsed` — asserts `success ==
  false`, a non-empty error message, and that the pre-existing entity is left
  untouched.
- `DeleteEntityByNameFailsForAnEmptyName` — asserts `success == false` for an
  empty name.

Added `Game/GameEntityCommandsTests.cpp` to `tests/CMakeLists.txt`'s
`GTE_TEST_SOURCES` list, immediately after the existing
`Game/RenderSystemTests.cpp` line.

**Explicit decision on Phase 5's own end-to-end coverage gap (per this
phase's own Step 3.1):** `tests/Network/EngineCommandEndpointsEndToEndTests.cpp`
(Phase 5) already proves the real `EngineCommandBridge` <-> `NetworkServer`
cross-thread handshake, HTTP status-code mapping, and real JSON
request/response bodies round-tripping over a real socket — via a
`FakeEngineCommandStandIn` that deliberately does NOT replicate `Game`'s own
real shape-name-validation/auto-dedup/parent-lookup logic (that logic is
Phase 2/3's own, independently Tier-1-tested responsibility, and this new
`GameEntityCommandsTests.cpp` file covers `DeleteEntityByName()`'s own real
logic directly). No test harness anywhere in this suite stands up a real,
live `Renderer`/`Game` wired to a real `NetworkServer` over a real socket —
this remains an explicit, accepted Tier 2 gap, closed instead by the manual,
real `gte_send_request` end-to-end smoke test in section 4 below (exactly the
same "Tier 2, no automated coverage yet, covered by manual smoke test"
convention this engine already accepts for `Buffer`/`RenderTexture`/etc. — see
`AGENTS.md`/`TESTING.md`). Writing this decision down explicitly here, per the
phase document's own instruction, rather than leaving it silently unresolved.

### 2. Confirmed every new file from Phases 1-5 is correctly registered

Per the phase document's own Step 3.1, ran `search_in_dir` against
`tests/CMakeLists.txt` for every new test file name
(`EntityQueryTests.cpp`/`EngineCommandBridgeTests.cpp`/
`EngineCommandEndpointsEndToEndTests.cpp`) and against the root
`CMakeLists.txt` for every new non-test file
(`EntityQuery.cpp`/`EngineCommandBridge.cpp`/`EngineCommandDispatch.cpp`/
`EngineCommandResults.h`) — all four were already present and correctly
wired (Phases 2-4 registered themselves correctly at the time). The full
clean build in section 4 below is the final, conclusive confirmation of this.

### 3. `AGENTS.md` — extended the existing "Networking" section

Appended new bullets directly below the existing `GTE_ENABLE_NETWORK` bullet,
in the same voice/style, covering (per the phase document's own Step 3.2):
POST support extending the existing "pure function of its own request data"
rule unchanged; `EngineCommandBridge`'s shape contrasted directly against
`FrameCaptureBridge`'s (single global slot, real payload + real mutation
outcome, drained EARLY in `Application::Run()` before `Game::Update()`); a
one-paragraph summary of `POST /instantiate_primitive`/`POST /delete_entity`'s
request/response JSON shapes and status codes; a note that a future THIRD
engine command should extend `EngineCommandKind`'s tagged-struct shape rather
than inventing a new bridge; and an explicit callout that the newly-vendored
`nlohmann/json` is a deliberate, narrow exception to this engine's "no JSON
library, hand-rolled formats only" precedent, not blanket permission to reach
for it elsewhere.

### 4. `README.md` — extended the existing "Status" section

Added one new bullet, in the same voice/detail-level as the existing
`network-impl-1`/`network-impl-2` bullets, immediately after the
`network-impl-2` writeup and before the `editor-enchancements-1` bullet —
summarizing POST support, both new endpoints' behavior (shape spawning +
auto-dedup + non-fatal parent-not-found warning; name-based delete +
descendant cascade), the `EngineCommandBridge` pattern in context against
`FrameCaptureBridge`, and a pointer at this campaign's own
`PHASE0_MASTER_STRATEGY.md`.

### 5. Full regression pass

**Default CMake configuration:**

- Full clean build of the main executable (`cmake --build build`, working
  directory the repo root) — succeeded with zero errors, including compiling
  and staging every shader.
- Full `ctest -C Debug --output-on-failure` run (from `build/`): **1126/1126
  tests passed** (1 pre-existing, machine-gated smoke test —
  `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine` —
  skipped, same as every prior session in this repository). This is exactly
  1121 (Phase 5's own final count minus the skip) + 3 new tests from
  `GameEntityCommandsTests.cpp` this phase = 1126 total, confirming the count
  increased by EXACTLY this phase's own new test cases and zero
  previously-passing tests regressed.

**`-DGTE_ENABLE_NETWORK=OFF` configuration** (fresh configure + build in a
separate directory, `build-network-off/`, removed again after verification —
see "Deviations" below for why this directory isn't `build_network_off/`
despite `.gitignore`'s existing entry for that name):

- Fresh `cmake -S . -B build-network-off -DGTE_ENABLE_NETWORK=OFF -G Ninja`
  configure succeeded (no dependency needed re-fetching — everything, including
  the newly-vendored `nlohmann/json`, was already staged from the default
  configuration's earlier fetch).
- Full clean build (`cmake --build build-network-off`) succeeded — both
  `gte_core`/`GreatTamanaEngine.exe` and `GreatTamanaEngineTests.exe` built
  and linked with zero errors, confirming `src/Network/`'s own class-always-
  compiles precedent holds, and that Phase 4's `Application.h`/`.cpp` changes
  introduced no hard (non-`#if`-gated) dependency on the network module.
- Full `ctest -C Debug --output-on-failure` run: **1126/1126 tests passed**
  (same 1 pre-existing skip) — identical to the default configuration,
  confirming `src/Network/`'s tests (and every other test in the suite)
  compile and pass unchanged whether `GTE_ENABLE_NETWORK` is `ON` or `OFF`.
- Runtime sanity check: launched `build-network-off/GreatTamanaEngine.exe`
  via `run_app_background` — the process started and stayed up. A
  `gte_send_request` call to `GET /http_hello_world` correctly returned a
  **connection-refused error** (no socket listening on port 8080 at all),
  positively confirming `Application`'s constructor never calls
  `NetworkServer::Start()` in this configuration — the engine window itself
  opens and runs completely undisturbed by networking being compiled out.
  Stopped via `stop_app_background` immediately after.

### 6. Real end-to-end smoke test against the DEFAULT build (networking ON)

Launched `build/GreatTamanaEngine.exe` via `run_app_background`, confirmed it
was up via `GET /http_hello_world` (`200`, `"hello world"`), then executed
every numbered step from the phase document's own Step 3.4 exactly, via
`gte_send_request`:

1. **`POST /instantiate_primitive`** with a full valid payload
   (`shape: "cube"`, `name: "SmokeCube"`, `world_position: {1.0, 0.5, 2.0}`,
   `parent: "NonExistentParent"` — deliberately not yet created) →
   ```json
   {"entity":{"generation":1,"index":1},"name":"SmokeCube","parent_requested_but_not_found":true,"requested_parent_name":"NonExistentParent","success":true}
   ```
   `200`, `parent_requested_but_not_found: true` — confirmed.
2. **`POST /instantiate_primitive`** again with the exact same `"name":
   "SmokeCube"` (a different `world_position`, no `parent`) →
   ```json
   {"entity":{"generation":1,"index":2},"name":"SmokeCube (1)","parent_requested_but_not_found":false,"requested_parent_name":"","success":true}
   ```
   `200`, resolved name auto-deduplicated to `"SmokeCube (1)"` — confirmed the
   Unity-style auto-dedup path works over a REAL HTTP round trip, not just in
   a unit test.
3. **`POST /instantiate_primitive`** a third entity named `"NonExistentParent"`
   (a sphere), then a fourth (`"SmokeChildCone"`, a cone) naming that exact
   parent →
   ```json
   {"entity":{"generation":1,"index":3},"name":"NonExistentParent","parent_requested_but_not_found":false,"requested_parent_name":"","success":true}
   {"entity":{"generation":1,"index":4},"name":"SmokeChildCone","parent_requested_but_not_found":false,"requested_parent_name":"","success":true}
   ```
   `200` both times, `parent_requested_but_not_found: false` on the fourth
   call — confirmed the parent-by-name lookup resolves correctly once the
   named entity actually exists.
4. **`GET /get_game_view`** returned `409 capture failed` — this is because a
   fresh Editor session's default dock layout has "Scene"/"Game" tabbed
   together with "Scene" active on top (see `README.md`'s own "Visibility-
   driven rendering" — an inactive dock tab is genuinely not rendered that
   frame, by design, at zero GPU cost), not a bug in this campaign's own
   code. **`GET /get_swapchain`** (the whole-Editor-UI capture, unaffected by
   which of Scene/Game is the active tab) succeeded (`200`, PNG) and visually
   confirmed every spawned primitive rendered correctly: two cubes
   ("SmokeCube"/"SmokeCube (1)"), a sphere ("NonExistentParent") with a cone
   child ("SmokeChildCone") directly beneath it in "Scene", and "Hierarchy"
   showing the exact expected tree (`SmokeCube`, `SmokeCube (1)`,
   `NonExistentParent` ▾ `SmokeChildCone`).
5. **`POST /delete_entity`** for `SmokeCube`, `SmokeCube (1)`, and
   `NonExistentParent` (in that order — the third deliberately also
   exercising the descendant-cascade delete of its child `SmokeChildCone`,
   never explicitly deleted on its own) → `200` for all three:
   ```json
   {"entity":{"generation":1,"index":1},"success":true}
   {"entity":{"generation":1,"index":2},"success":true}
   {"entity":{"generation":1,"index":3},"success":true}
   ```
   A follow-up `GET /get_swapchain` visually confirmed "Hierarchy" now shows
   only the original `Entity 0 (Camera)` — every spawned primitive, including
   the cascade-deleted `SmokeChildCone`, is gone, and "Scene" is empty again.
6. **`POST /delete_entity`** for `"SmokeCube"` again (already deleted) →
   ```json
   {"error":"no live entity found with name 'SmokeCube'","success":false}
   ```
   `404` — confirmed.
7. **`POST /instantiate_primitive`** with a deliberately malformed body
   (missing `"shape"`) →
   ```json
   {"error":"missing or invalid required field: shape","success":false}
   ```
   `400` with a human-readable `"error"` message — confirmed.

Stopped the engine via `stop_app_background` once every step above was
verified.

## Verification performed (summary)

1. Fast, targeted compile + test check for the new
   `GameEntityCommandsTests.cpp` file alone (3/3 new tests passed) before
   proceeding to the full regression pass.
2. Full clean build (`cmake --build build`) — succeeded, zero errors, main
   executable + shaders + tests all built.
3. Full `ctest -C Debug --output-on-failure` (default configuration) —
   **1126/1126 tests passed** (1 pre-existing machine-gated skip).
4. Full clean configure + build + `ctest` under
   `-DGTE_ENABLE_NETWORK=OFF` — **1126/1126 tests passed** (identical count),
   plus a runtime sanity check confirming the engine executable starts up
   fine with networking compiled out and genuinely opens no socket.
5. A real, live end-to-end smoke test against the default (networking ON)
   build via `gte_send_request`, covering every numbered step in the phase
   document's own Step 3.4 — see section 6 above for the full evidence.

## Deviations from the phase document

None in substance. Two minor procedural notes:

1. The `-DGTE_ENABLE_NETWORK=OFF` configuration was built into
   `build-network-off/` (a directory name using a hyphen) rather than the
   `build_network_off/` (underscore) name `.gitignore` already has a
   dedicated entry for — this was noticed only after the fact via
   `git status` showing it as untracked. Rather than leave a stray untracked
   build-artifact directory sitting in the repo (or rename it to match the
   `.gitignore` entry and re-verify), the directory was deleted outright once
   its own verification (full build + full `ctest` + runtime sanity check)
   was already complete and captured in this report — a temporary,
   already-fully-verified build tree has no reason to persist afterward
   either way. A future session doing the same secondary-configuration check
   should just use the exact `build_network_off/` name `.gitignore` already
   expects, to avoid this cosmetic mismatch entirely.
2. `GET /get_game_view` returned `409` during the smoke test (see step 4
   above) purely because "Scene", not "Game", happened to be the active dock
   tab in this fresh Editor session — an accurate, by-design result (an
   inactive tab's view genuinely isn't rendered that frame), not a failure of
   anything this campaign built. `GET /get_swapchain` (unaffected by tab
   selection) was used instead to provide the same visual confirmation the
   phase document's own Step 3.4 asked for, and did so successfully both
   before and after the delete steps.

## Campaign status

All six phases of `network-impl-3` are now complete. The engine's embedded
HTTP server has two new, fully working, end-to-end-verified POST endpoints
(`/instantiate_primitive`, `/delete_entity`) built on a new, generalized
cross-thread `EngineCommandBridge`, backed by the engine's first vendored
JSON library. Full regression safety confirmed in two independent CMake
configurations, with zero regressions anywhere in the existing 1123-test
suite this campaign started with (1126 including this phase's own 3 new
tests, minus the one pre-existing machine-gated skip).

## Suggested follow-up (per Step 4 of the phase document — noted, not implemented)

A natural next campaign: a read-only `list_entities`/`get_entity` endpoint,
so a caller (in practice, an LLM/automation client) can look up an entity's
current resolved name/parent/world position without having to remember what
an earlier `instantiate_primitive` response returned. This would extend
`EngineCommandKind` with a `ListEntities`/`GetEntity` variant, following the
exact same bridge/dispatch shape this campaign already built — deliberately
NOT implemented as part of this campaign (see `PHASE0_MASTER_STRATEGY.md`'s
own "Non-Goals").
