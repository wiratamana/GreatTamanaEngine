# PHASE4 — Completion Report: Automated Tests & Regression Safety

Parent: `PHASE0_MASTER_STRATEGY.md`. Phase document executed:
`PHASE4_AUTOMATED_TESTS_AND_REGRESSION_SAFETY.md`. This is also the **final
phase of the `network-impl-1` campaign** — see
`NETWORK_IMPL_1_CAMPAIGN_COMPLETION_REPORT.md` (same folder) for the
campaign-level summary cross-referencing all four phases.

## What was done

Re-read `PHASE3_COMPLETION_REPORT.md` first, per this task manager's own
working agreement — it confirmed `Application` already auto-starts
`Network::NetworkServer` on `127.0.0.1:8080` (gated by `GTE_ENABLE_NETWORK`),
`AGENTS.md`/`README.md` already document the feature, and that nothing had
touched `tests/CMakeLists.txt` or added any `tests/Network/` file yet — Phase
4 could proceed exactly as planned with no adjustments to the locked design.

1. **`tests/Network/NetworkRoutesTests.cpp`** — created exactly as sketched in
   the Phase 4 plan: one Tier-1 test, `HandleHelloWorldReturnsExactContractedString`,
   asserting `gte::Network::HandleHelloWorld() == "hello world"`.
2. **`tests/Network/NetworkServerTests.cpp`** — created with the five tests
   from the Phase 4 plan (`HelloWorldEndpointRespondsOverARealSocket`,
   `UnknownRouteFallsThroughToDefault404`, `StopIsSafeToCallWithoutAPriorStart`,
   `DestructorStopsTheServerWithoutHanging`,
   `BindFailureIsNonFatalAndLeavesServerNotRunning`) — with **one required
   deviation** from the plan's own sketch, described below.
3. **`tests/CMakeLists.txt`** — added both new files to `GTE_TEST_SOURCES`
   (unconditionally, no `if(GTE_ENABLE_NETWORK)` guard, immediately after the
   existing `Jobs/JobSystemSdlClockThreadSafetyTests.cpp` line), plus a new
   descriptive comment block in the file's own "Test taxonomy" section
   (inserted right before the pre-existing `Physics/VerletIntegrationTests.cpp`
   entry), matching the style/detail level of every neighboring entry.

### Required deviation: `BindFailureIsNonFatalAndLeavesServerNotRunning`

The Phase 4 plan's own sketch occupied the port with a **second, ordinary
`gte::Network::NetworkServer`** and expected the "collider" server's
`Start()` call on that same port to fail. When first implemented exactly as
sketched, **the test failed** — both servers ended up `IsRunning() == true`
on the same port, with the log showing `"NetworkServer: listening on
127.0.0.1:<port>"` printed TWICE.

Root cause: `httplib::Server`'s own `default_socket_options()` (see
`third_party/httplib/httplib.h`) sets `SO_REUSEADDR` on every socket it
creates. Windows' `SO_REUSEADDR` is notoriously permissive — a second socket
that also sets `SO_REUSEADDR` can successfully bind to an address:port
**already bound and actively listening** by a first `SO_REUSEADDR` socket (a
well-known Windows-specific "port hijacking" quirk). This does **not**
reproduce the reliable `EADDRINUSE` failure the same scenario would produce
on Linux/macOS — so two real `NetworkServer` instances can never be used to
deterministically reproduce a bind failure on this platform.

Fix: the test now occupies the port with a **raw, platform `SOCKET`** set to
`SO_EXCLUSIVEADDRUSE` (`ExclusivePortOccupant`, a small RAII helper local to
the test file) — the one Windows socket option strong enough to block *any*
later bind to the same address:port regardless of the later socket's own
`SO_REUSEADDR` setting. With that occupant in place, a real
`gte::Network::NetworkServer`'s `Start()` call on the same port now reliably
fails exactly as documented (`IsRunning() == false`, `BoundPort() == 0`), and
a fresh, healthy `NetworkServer` can bind and serve normally on that same
port once the occupant is released — preserving the original test's full
intent (bind failure is non-fatal, leaves the server in its default
not-running state, and doesn't corrupt anything else) while being
deterministic on this platform. This is a pure test-file fix — no production
code (`NetworkServer.cpp`) needed any change; its own documented,
already-correct "log and continue" bind-failure behavior is what the test
now actually exercises.

This was **not** a tool malfunction (no `bug_report` filed) — it was a
genuine, environment-specific behavior difference the Phase 4 strategy
document's own "Notes for whoever implements this" section anticipated could
happen ("adjust to match the real API/behavior... the important behavior is
[the documented contract]").

## Verification

- **Default (`GTE_ENABLE_NETWORK=ON`) configuration**, existing `build/`
  directory:
  - `cmake --build build` — succeeded (only the two new test `.obj`s and the
    `GreatTamanaEngineTests.exe` link were rebuilt; `GreatTamanaEngine.exe`/
    `libgte_core.a` were already up to date from Phase 3, confirmed by
    timestamp — this phase touched no `src/` file).
  - `ctest -C Debug --output-on-failure` in `build/`: **100% tests passed,
    1018/1018** (plus the one pre-existing `PmxLoaderRealModelSmokeTest`
    machine-gated skip, unrelated to this campaign). This includes both new
    `Network/` files' 6 test cases.
  - `GreatTamanaEngineTests.exe --gtest_filter=NetworkServerTests.*:NetworkRoutesTests.*
    --gtest_repeat=5` — run directly against the built exe: **all 6 tests
    passed on every one of 5 repeats**, including
    `BindFailureIsNonFatalAndLeavesServerNotRunning` (the one test coordinating
    two separate sockets/ports, and therefore the most likely to expose a
    latent timing assumption) — no flakiness observed.
- **`-DGTE_ENABLE_NETWORK=OFF` configuration**, a fresh, separate
  `build_network_off/` directory (leaving `build/` untouched, still in its
  normal ON state):
  - `cmake -S . -B build_network_off -G Ninja -DGTE_ENABLE_NETWORK=OFF` —
    configured successfully.
  - `cmake --build build_network_off` — a full clean build succeeded,
    producing both `GreatTamanaEngine.exe` and `GreatTamanaEngineTests.exe`
    with zero errors — `NetworkServer`/`NetworkRoutes` (and their test files)
    compiled identically to the ON configuration, confirming the "class
    always compiles, only `Application`'s own call site is gated" contract.
  - `ctest -C Debug --output-on-failure` in `build_network_off/`: **100%
    tests passed, 1018/1018** (same single pre-existing machine-gated skip) —
    proving the two new `Network/` test files (which construct
    `NetworkServer` directly, never through `Application`) behave completely
    identically regardless of the `GTE_ENABLE_NETWORK` switch, exactly as
    `PHASE0_MASTER_STRATEGY.md`'s locked decision #4 requires.

## Full Test Results Summary

| Configuration | Build | Tests Run | Passed | Skipped | Failed |
|---|---|---|---|---|---|
| `build/` (`GTE_ENABLE_NETWORK=ON`, default) | Success | 1018 | 1018 | 1 (pre-existing, machine-gated) | 0 |
| `build_network_off/` (`GTE_ENABLE_NETWORK=OFF`) | Success | 1018 | 1018 | 1 (pre-existing, machine-gated) | 0 |
| `NetworkServerTests.*`/`NetworkRoutesTests.*` × 5 repeats (ON build) | — | 30 (6 × 5) | 30 | 0 | 0 |

## Definition of Done — campaign-wide check

Per `PHASE0_MASTER_STRATEGY.md`'s own Definition of Done:

- [x] `cmake --build build` succeeds with `GTE_ENABLE_NETWORK` at its default
      (`ON`).
- [x] A separate `-DGTE_ENABLE_NETWORK=OFF` configuration also builds
      cleanly.
- [x] `ctest` passes, including the two new `Network/` test files, in BOTH
      configurations.
- [x] `AGENTS.md` has a "Networking" section (Phase 3); `README.md`'s
      "Status" section documents the feature (Phase 3).
- [x] The endpoint contract (`GET /http_hello_world` → `hello world`) is now
      proven automatically, end-to-end, over a real socket
      (`NetworkServerTests.HelloWorldEndpointRespondsOverARealSocket`) — no
      manual browser/curl step required ever again, closing the loop
      `PHASE0_MASTER_STRATEGY.md`'s Step 1 originally called for.

No further work is required for this campaign. The whole `network-impl-1`
campaign (Phases 1-4) is now **complete**.
