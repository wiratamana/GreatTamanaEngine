# PHASE5 — End-to-End Smoke Test and Full Validation

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on ALL prior phases — read
`PHASE1_COMPLETION_REPORT.md` through `PHASE4_COMPLETION_REPORT.md` before
starting.

**Use `ask_questions` for any genuine ambiguity this document doesn't already
resolve.** If this phase itself delegates any further sub-task, that
delegation prompt must repeat this same instruction.

**This is the ONLY phase in this campaign allowed to run a full build and a
full `ctest` regression pass** — every prior phase deliberately used a fast,
targeted incremental check instead (see `PHASE0`'s own working agreement).

---

## Step 1: The Goal (Where are we going?)

Prove the entire `logger-1` feature works end-to-end, for real, over a real
HTTP round trip against a real, ephemeral-port `NetworkServer` instance —
exactly the rigor `tests/Network/NetworkServerTests.cpp` already applies to
every other endpoint — then run the full build and full regression suite,
and close out every remaining documentation/changelog obligation
`PHASE0`'s "Definition of Done for the whole campaign" lists.

## Step 2: The Situation (Where are we now?)

- `tests/Network/NetworkServerTests.cpp` already establishes the exact
  pattern this phase's new test cases must follow: construct a real
  `gte::Network::NetworkServer`, call `Start(0)` (port `0` = "let the OS
  assign a free ephemeral port", read back via `BoundPort()`), use
  `tests/Network/NetworkTestHelpers.h`'s `WaitUntilAcceptingConnections(...)`
  to avoid a connect-before-listen race, then drive it with a real
  `httplib::Client` pointed at `127.0.0.1:<BoundPort()>`.
- Because `GET /get_logs`/`POST /clear_logs` (Phase 3) need NO bridge, the
  `NetworkServer` this phase's test constructs needs no bridge pointers
  wired up at all — the simplest possible constructor call,
  `gte::Network::NetworkServer server;` (mirroring the file's own existing
  "seven no-argument `NetworkServer server;` constructions" default-bridge
  precedent quoted in that class's own header comment).
- Because `Logger` is process-global static state (Phase 1), and this
  test process is the SAME process as `GreatTamanaEngineTests.exe`'s other
  test cases (including `LoggerTests.cpp`'s own, from Phase 1, and possibly
  the `NetworkRoutesTests.cpp` `#if GTE_ENABLE_EDITOR` cases from Phase 3),
  **this phase's own new test file must call `gte::Logger::Clear()` at the
  very start of every one of its own test bodies**, exactly like every
  other test in this campaign already does, so test execution order never
  matters and no test observes another test's leftover entries.
- **Correction, checked directly against `tests/CMakeLists.txt`**: this new
  test file does NOT need `GTE_ENABLE_NETWORK` to be ON, and neither does any
  EXISTING `Network/*Tests.cpp` file. `tests/CMakeLists.txt`'s own file-level
  comment for `Network/NetworkServerTests.cpp` says so explicitly: "Always
  built - NetworkServer/NetworkRoutes always compile regardless of
  GTE_ENABLE_NETWORK (only Application's own production call site is gated)".
  Confirmed structurally too: every `Network/*Tests.cpp` file (including
  `NetworkServerTests.cpp`/`CaptureEndpointsEndToEndTests.cpp`/
  `ActivateTabEndpointEndToEndTests.cpp`/etc.) is listed in the master
  `GTE_TEST_SOURCES` list completely UNCONDITIONALLY - never inside an
  `if(GTE_ENABLE_NETWORK)` guard of any kind anywhere in that file. This
  new file DOES still need `GTE_ENABLE_EDITOR`, though (same as every
  `Editor/*Tests.cpp` file), since it calls `gte::Logger::Log()`/
  `Query()`/`Clear()` directly and those only have real behavior in that
  configuration - so simply add it inside the EXISTING
  `if(GTE_ENABLE_EDITOR) list(APPEND GTE_TEST_SOURCES Editor/EditorCameraTests.cpp
  ...)` block (the SAME block `PHASE1`'s `Editor/LoggerTests.cpp` was already
  added to), with no `GTE_ENABLE_NETWORK` condition at all - do NOT nest a
  second `if()` for it, and do NOT introduce a combined
  `if(GTE_ENABLE_EDITOR AND GTE_ENABLE_NETWORK)` guard; that would be new,
  unprecedented gating this codebase's own existing Network test files
  deliberately do not have.

## Step 3: The Plan

### 3.1 New test file: `tests/Network/LogEndpointsEndToEndTests.cpp`

Mirror `tests/Network/CaptureEndpointsEndToEndTests.cpp`'s own file-level
shape (this project's existing precedent for "a SECOND end-to-end test file
alongside `NetworkServerTests.cpp` itself, for one specific endpoint
family", per that file's own doc comment cited in
`NetworkServerTests.cpp`). Cover, each as its own `TEST(...)`, using a
FRESH `gte::Logger::Clear()` at the start of each:

1. **Basic fetch**: `Logger::Log(...)` a handful of entries with known
   levels/categories/messages (call `Logger::SetCurrentFrame(...)` between
   some of them so they land on different frame numbers) directly in the
   test body (no need to go through a real `Application`/`Game` — this
   test is about the HTTP <-> Logger plumbing, not about real engine
   lifecycle events). Start a real server, `GET /get_logs` with no query
   parameters at all, parse the JSON response (`nlohmann::json::parse` —
   already a test dependency elsewhere in this suite), assert
   `"logging_enabled" == true`, `"count"` matches the number of entries
   logged, and each entry's fields round-trip correctly.
2. **`since_id` cursor**: log entries, `GET /get_logs` once to learn the
   real `id` of, say, the 2nd entry from the response body itself (do not
   hard-code an assumed id — read it back), then `GET
   /get_logs?since_id=<that id>` and assert only the later entries come
   back.
3. **`min_level` filter**: log one entry per level, `GET
   /get_logs?min_level=warning` (any casing — also try `Warning`/`WARNING`
   once to confirm case-insensitivity) and assert only Warning/Error come
   back.
4. **`category` filter**: log entries across two different categories,
   `GET /get_logs?category=<one of them>` returns only that category's
   entries; confirm an unknown category returns an empty `"entries"` array
   with `"count": 0` (not an error).
5. **`keyword` filter**: log an entry containing a distinctive substring,
   `GET /get_logs?keyword=<that substring, different casing than logged>`
   matches it.
6. **`frame_min`/`frame_max` range**: `SetCurrentFrame(...)` before several
   `Log()` calls across a few different frame numbers, confirm the
   inclusive range filter returns exactly the expected subset.
7. **`limit` + clamping**: log more than the default 200-entry limit worth
   of entries (or simply pass an explicit small `limit`), confirm the
   response contains exactly that many, and are the NEWEST ones (assert on
   message/frame content, not just count); separately, pass a `limit`
   larger than `Logger::kCapacity` (2000) and confirm the request still
   succeeds (200, not 400) with the response silently capped, per Phase
   3's documented clamping behavior.
8. **Malformed query parameter -> 400**: `GET /get_logs?since_id=notanumber`
   (and similarly for a garbage `min_level`/`frame_min`/`frame_max`/`limit`)
   each return HTTP 400 with a JSON body matching
   `BuildGenericErrorResponseJson(...)`'s own shape.
9. **`POST /clear_logs`**: log a few entries, `POST /clear_logs`, assert
   the response's `"cleared_count"` matches how many were actually present,
   then `GET /get_logs` again and confirm `"count": 0`; log one more entry
   afterward and confirm its `id` is still strictly increasing (never
   reset), matching `LoggerTests.cpp`'s own Phase 1 `Clear()` assertion,
   just proven again over the real HTTP path this time.
10. **Combined filters**: at least one test exercising two or more filters
    together (e.g. `min_level` + `category` + a `frame` range at once) to
    confirm they compose as a logical AND, matching `LogQueryFilter`'s own
    documented semantics.

### 3.2 `tests/CMakeLists.txt` wiring

Add `Network/LogEndpointsEndToEndTests.cpp` inside the EXISTING
`if(GTE_ENABLE_EDITOR) list(APPEND GTE_TEST_SOURCES ...)` block only (see
Step 2's correction above) - no `GTE_ENABLE_NETWORK` condition needed or
wanted, matching every other `Network/*Tests.cpp` file's own unconditional
(with respect to that switch) treatment.

### 3.3 Full build + full regression run

Only now, in this phase:

```
cmake --build build
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```

If anything fails: diagnose it directly if it's small/obviously related to
this campaign's own new code; if it's a larger/unrelated pre-existing
regression, or the fix is non-trivial, use `delegate_task` to spin off a
dedicated fix task rather than trying to fix everything inline in this
already-large phase — **that delegated task must itself be told to use
`ask_questions`, and to pass that same instruction forward to anything it
further delegates**, matching this whole campaign's standing rule.

### 3.4 Manual/visual confirmation (encouraged, using the available tools)

- `run_app_background` the built `GreatTamanaEngine.exe`.
- Issue a few real `gte_send_request` calls against `/get_logs` with
  different query strings and inspect the raw JSON/text response.
- Take a screenshot via `gte_send_request`'s image-returning endpoints
  (`/get_swapchain` or `/get_game_view`) to visually confirm the "Log"
  panel (Phase 4) is present, populated, and correctly colored/filterable
  in a real running session.
- `stop_app_background` when done.

### 3.5 Close out `PHASE0`'s campaign-level Definition of Done

- `README.md`: add a new bullet to the `## Status` section (following the
  exact style/verbosity of the existing bullets there — see the
  `render-pass-3` bullet for the level of detail/cross-referencing
  expected), describing the whole `logger-1` campaign: the Editor-only
  Logger, the four levels, the 2000-entry ring buffer, the `GET /get_logs`/
  `POST /clear_logs` endpoints and their filters, and the new "Log" panel.
  Cross-link `task_manager/logger-1/PHASE0_MASTER_STRATEGY.md` the same way
  every other bullet cross-links its own campaign's `PHASE0`.
- `docs/CHANGELOG.md`: add the matching entry at the top (reverse-
  chronological), if this file follows a "one entry per landed campaign"
  convention (confirm by reading its most recent existing entries first).
- Do a final read-through of `AGENTS.md`'s new `## Logging` section and
  `docs/conventions/logging.md` (both written in `PHASE3`) to confirm they
  still accurately describe the FINAL shipped behavior after `PHASE4`'s UI
  landed (e.g. that the "Log" panel is real and named exactly `"Log"`) —
  fix any drift directly, it's a documentation-only change.

### 3.6 Write the whole-campaign completion report

Write `CAMPAIGN_COMPLETION_REPORT.md` (not `PHASE5_COMPLETION_REPORT.md` —
mirror `task_manager/frame-debugger-1/CAMPAIGN_COMPLETION_REPORT.md`'s/
`task_manager/network-impl-1/NETWORK_IMPL_1_CAMPAIGN_COMPLETION_REPORT.md`'s
own precedent of a final, whole-campaign summary distinct from each
individual phase's own report) next to this file, summarizing: what was
built, any Locked Design Decision that had to change along the way and why,
the final full-build/full-`ctest` result, and a pointer to every new/changed
file across all five phases.

## Definition of Done for this phase (and the whole campaign)

- Full `cmake --build build` succeeds.
- Full `ctest -C Debug --output-on-failure` passes, including every new
  test file from Phases 1, 3, and 4, plus this phase's own
  `LogEndpointsEndToEndTests.cpp`.
- `README.md`, `docs/CHANGELOG.md`, `AGENTS.md`, and
  `docs/conventions/logging.md`/`docs/conventions/networking.md`/
  `docs/README.md` all accurately describe the final, shipped feature.
- `CAMPAIGN_COMPLETION_REPORT.md` is written.
- `git add`/`git commit` (this phase's own changes + both reports) with a
  clear message.
