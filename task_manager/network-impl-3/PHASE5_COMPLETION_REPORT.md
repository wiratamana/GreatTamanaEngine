# PHASE5 — COMPLETION REPORT: `NetworkServer` POST Support + The Two New Routes

Parent: `PHASE0_MASTER_STRATEGY.md`
Phase document: `PHASE5_NETWORK_POST_ROUTES_AND_COMMAND_DISPATCH.md`
Branch: `feature/network-impl`

## Summary

Implemented Phase 5 of the `network-impl-3` campaign exactly as specified:
`NetworkServer.cpp`'s `RegisterRoutes()` now registers two real
`httplib::Server::Post(...)` routes — `POST /instantiate_primitive` and
`POST /delete_entity` — each parsing its body via Phase 1's
`ParseInstantiatePrimitiveRequest()`/`ParseDeleteEntityRequest()`, submitting
through Phase 4's `EngineCommandBridge::SubmitAndWait()`, and mapping the
result to the exact HTTP status codes the phase document's reference design
specifies (`400` malformed/invalid JSON, `503` no bridge/already pending,
`504` timed out, `200`/`400` for `instantiate_primitive`'s outcome, `200`/`404`
for `delete_entity`'s outcome). This is the campaign's actual user-facing HTTP
surface — by the end of this phase, both endpoints are real, working routes.

## What was done

### 1. `src/Network/NetworkServer.cpp` (extended)

- Added `#include "../Math/Vec3.h"` (needed for the `Vec3{...}` construction
  inside the new `/instantiate_primitive` handler).
- `RegisterRoutes()`'s `(void)commandBridge;` placeholder (Phase 4) was
  removed and replaced with two new registrations, added immediately after
  the existing `RegisterCaptureRoute(...)` calls, implemented **exactly**
  per the phase document's own reference design (verbatim, apart from
  reflowing the "not-found" comment for line length):
  - `server.Post("/instantiate_primitive", ...)` — parses the body via
    `ParseInstantiatePrimitiveRequest()` (`400` on failure), checks
    `commandBridge == nullptr` (`503`), builds an `EngineCommandRequest`
    (`kind = InstantiatePrimitive`), calls `SubmitAndWait()`, maps
    `alreadyPending` → `503`, `timedOut` → `504`, otherwise inspects the real
    `InstantiatePrimitiveOutcome` and responds `200`/`400` via
    `BuildInstantiatePrimitiveResponseJson()`.
  - `server.Post("/delete_entity", ...)` — same shape, via
    `ParseDeleteEntityRequest()`/`DeleteEntityOutcome`/
    `BuildDeleteEntityResponseJson()`, responding `200`/`404` (a not-found
    delete is a distinct, well-known status from a generic bad request, per
    the phase document's own reasoning — an empty-name `400` is unreachable
    here in practice since the parser already rejects that case).
  - The two routes were deliberately **not** collapsed into one shared
    helper (per the phase document's own Step 2 — different request parser,
    different response builder, different failure-status mapping for the
    not-found case each).
- Both new handler lambdas capture `commandBridge` by value (a raw,
  non-owning pointer — cheap to copy, exactly like `captureBridge` in the
  existing `RegisterCaptureRoute()` helper) — no engine subsystem
  (`Registry`/`Renderer`/`Game`/`AssetDatabase`) is ever touched directly by
  either handler, preserving this campaign's Cross-Phase Invariant #1 (see
  `PHASE0_MASTER_STRATEGY.md`).

### 2. `src/Network/NetworkServer.h` (constructor doc comment extended)

Extended the constructor's existing `commandBridge` doc comment (previously
only "non-null in production... passes its address") to also explain
`nullptr`'s contract identically to `captureBridge`'s own comment: "POST
/instantiate_primitive and POST /delete_entity... both respond 503 rather
than crashing" — matching the phase document's own explicit instruction not
to leave the new parameter under-documented while the old one stays fully
documented.

### 3. `README.md`/`AGENTS.md`

Deliberately **not** touched this phase — per the phase document's own Step
3.4 ("Update README.md is Phase 6's job, not this phase's"), Phase 6 owns the
single, final documentation pass across the whole campaign.

### 4. Tests — `tests/Network/EngineCommandEndpointsEndToEndTests.cpp` (new file)

Mirrors `tests/Network/CaptureEndpointsEndToEndTests.cpp`'s exact proven
shape: a real `gte::EngineCommandBridge` + a real
`gte::Network::NetworkServer`, wired together exactly like `gte::Application`
does in production, hit over a real loopback socket via `httplib::Client`. A
new `FakeEngineCommandStandIn` (mirroring `FakeMainThreadStandIn`) stands in
for `Application::Run()`'s real per-frame
`TryPeekPendingCommandRequest()`/`FulfillCommand()` drain (and, transitively,
for `Game::InstantiatePrimitive()`/`DeleteEntityByName()` themselves) — it
tracks a small in-memory set of "live" names purely so a fake
instantiate-then-delete round trip can be proven, deliberately **not**
replicating `Game`'s own real shape-name-validation/auto-dedup/parent-lookup
logic (that logic is Phase 2/3's own, independently Tier-1-tested
responsibility).

Eight new tests:

- `InstantiatePrimitiveValidPayloadReturns200AndSuccess` — a fully valid
  request returns `200`, and the response JSON parses with `success == true`
  and the expected `name`.
- `InstantiatePrimitiveMalformedJsonReturns400`.
- `DeleteEntityForUnknownNameReturns404`.
- `DeleteEntityMalformedJsonReturns400`.
- `FullInstantiateThenDeleteRoundTripBothReturn200` — instantiates a
  uniquely-named primitive, then deletes that exact name, confirming `200`
  both times — the phase document's own explicitly-called-out strongest
  regression proof of the cross-thread plumbing.
- `SecondConcurrentCommandReturns503Immediately` — a first request
  (`/instantiate_primitive`) held genuinely pending (via the stand-in's own
  gating) while a second, **different-kind** request (`/delete_entity`)
  returns `503` immediately — proves the single GLOBAL slot (not per-kind).
- `TimedOutSubmissionIsWhatTheHttpRouteMapsToA504` — calls
  `EngineCommandBridge::SubmitAndWait()` directly with a short timeout
  override while gated (mirroring `CaptureEndpointsEndToEndTests.cpp`'s own
  `TimeoutSurfacesAs504` precedent — never waiting out the real 3-second
  production default over an actual HTTP round trip), confirming
  `timedOut == true` — the exact flag the HTTP route maps verbatim to `504`.
- `EngineCommandEndpointsNoBridgeTests.CommandBridgeNullptrReturns503ForBothRoutes`
  — a `NetworkServer` constructed with `commandBridge == nullptr` responds
  `503` for both new routes.

Added `Network/EngineCommandEndpointsEndToEndTests.cpp` to
`tests/CMakeLists.txt`'s always-built source list, immediately after the
existing `Network/CaptureEndpointsEndToEndTests.cpp` line.

**Per the phase document's own second-iteration review (confirmed again this
phase): no test harness anywhere in this suite stands up a real, live
`Renderer`/`Game`** — `Game::InstantiatePrimitive()`/`DeleteEntityByName()`
themselves (Phase 3) remain in the accepted, documented "Tier 2, no automated
coverage yet" bucket (`AGENTS.md`/`TESTING.md`). What this phase's new test
file proves, for real, is everything genuinely NEW here that isn't ordinary
ECS/GPU plumbing: the `EngineCommandBridge` <-> `NetworkServer` cross-thread
handshake, the HTTP status-code mapping, and the real JSON request/response
bodies round-tripping over a real socket.

## Verification performed

1. **Fast compile check** (`cmake --build build --config Debug --target
   GreatTamanaEngineTests`, from the repo root): full build succeeded with
   zero errors/warnings related to this change — `gte_core` (including the
   modified `NetworkServer.cpp`) and `GreatTamanaEngineTests.exe`
   (including the new test file) built and linked successfully. No other
   translation unit broke.
2. **Targeted test run** (`ctest -C Debug -R
   "EngineCommandEndpoints|InstantiatePrimitive|DeleteEntity|BuildResponseJson|NetworkServer"
   --output-on-failure`, from `build/`): **44/44 tests passed** — every new
   test from this phase (8) plus every pre-existing `NetworkServerTests`/
   `ParseInstantiatePrimitiveRequestTests`/`ParseDeleteEntityRequestTests`/
   `BuildResponseJsonTests` case (36), confirming zero regressions in
   anything this phase's change touches or depends on.

No full build (`cmake --build build` targeting the `GreatTamanaEngine`
executable itself), no full `ctest` regression pass over the entire suite,
and no `run_app_background` manual smoke test against a real, running engine
instance were performed this phase — per this task's own workflow rules ("No
Full Build: Do not run a full build or full regression test yet... unless
the Current task explicitly says to do full build. Usually on the last
one"), matching the exact same, already-established precedent
`PHASE3_COMPLETION_REPORT.md`/`PHASE4_COMPLETION_REPORT.md` both documented
for the identical reason. The phase document's own suggested manual
`gte_send_request` smoke test against a live, running `GreatTamanaEngine.exe`
(`POST /instantiate_primitive` → `POST /delete_entity` → `GET
/get_game_view` visual confirmation) is therefore **deferred to Phase 6**,
which explicitly calls for a full clean build + full `ctest` regression pass
and is the natural place to also perform this end-to-end runtime smoke test
against the freshly built executable.

## Deviations from the phase document

None in substance. Every locked decision, file, handler shape, and status-code
mapping was implemented exactly as specified. Two purely cosmetic
adjustments made while transcribing the reference design into the actual
file: (1) the not-found comment block above `/delete_entity`'s status-code
line was reflowed to fit this file's existing line-length convention (no
wording changed in meaning); (2) the manual, real, running-engine smoke test
the phase document suggests performing "right now, even before Phase 6" was
instead deferred to Phase 6 itself, for the workflow-rule reason explained
above (matching Phase 3/4's own already-established precedent) rather than
building/running the engine executable mid-campaign.

## Next phase

`PHASE6_AUTOMATED_TESTS_DOCS_AND_REGRESSION_SAFETY.md` — fills any remaining
test gaps, updates `AGENTS.md` ("Networking")/`README.md` ("Status"), a full
clean build + full `ctest` regression pass, and the real end-to-end smoke
test against a running engine instance via `gte_send_request` POST (deferred
here, see above) — the campaign's final verification/documentation pass.
