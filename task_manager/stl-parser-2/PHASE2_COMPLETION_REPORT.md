# PHASE2_COMPLETION_REPORT.md — Import Asset Network Endpoint

**Phase:** `PHASE2_IMPORT_ASSET_NETWORK_ENDPOINT.md`
**Campaign:** `stl-parser-2` (parent: `PHASE0_MASTER_STRATEGY.md`)
**Branch:** `feature/stl-parser-impl` (unchanged, as required)

## Summary

Implemented a real, working `POST /import_asset` HTTP route, built entirely on
top of `PHASE1`'s already-shipped `AssetImportCommandBridge` — the FIRST time
this campaign's work becomes reachable over the network at all. Every step of
this phase document's own "Step 3: The Plan" (3.1 through 3.7) was
implemented, re-verified against the real, current state of every file it
references before editing (per this task's own instructions), with one
genuine finding noted below (`Application.cpp`'s own Section 3.5 wiring
already existed, done ahead of schedule by `PHASE1` — see "Deviations").

### Files changed

- `src/Network/NetworkServer.h` — **no change needed.** `PHASE1` already added
  the forward-declare (`namespace gte { class AssetImportCommandBridge; }`),
  the fifth defaulted constructor parameter, and the matching private member
  exactly as this phase's own Section 3.1 asks for — confirmed by direct
  re-read of the real file before touching anything else, per this task's own
  instruction to re-verify against what's actually on disk.
- `src/Network/NetworkServer.cpp`:
  - Added `#include "../Application/AssetImportCommandBridge.h"` to the
    existing alphabetically-ordered bridge include block.
  - Extended `RegisterRoutes(...)`'s signature with a sixth parameter,
    `AssetImportCommandBridge* assetImportCommandBridge`, appended last (same
    convention the constructor itself already uses), and updated its one call
    site (inside `NetworkServer`'s own constructor) to pass
    `m_assetImportCommandBridge` — replacing the stale "not yet passed into
    RegisterRoutes()" comment `PHASE1` had left there.
  - Added the new `server.Post("/import_asset", ...)` route, placed
    immediately after `/instantiate_light` (the last of the existing
    bridge-backed POST routes) inside `RegisterRoutes()`'s body — parse via
    `ParseImportAssetRequest()` → 400 on invalid JSON/missing `source_path` →
    `assetImportCommandBridge == nullptr` → 503 → build an
    `AssetImportCommandRequest` → `SubmitAndWait()` (explicitly no second
    argument, so the bridge's own 120000ms default is used, never shortened)
    → `alreadyPending` → 503 → `timedOut` → 504 → `!outcome.projectAvailable`
    → 503 → field-by-field copy into a local `ImportedAssetResponseView` →
    `outcome.success ? 200 : 400`, mirroring the phase document's own code
    block exactly (including its own "do not skip the field-by-field copy"
    warning).
- `src/Network/NetworkRoutes.h` — added a new campaign-delimited section
  (mirroring every existing one in this file) with `ParsedImportAssetRequest`,
  `ParseImportAssetRequest()`, `ImportedAssetResponseView`, and
  `BuildImportAssetResponseJson()` — every doc comment copied verbatim from
  the phase document's own Section 3.3 code block.
- `src/Network/NetworkRoutes.cpp` — implemented both new functions following
  the exact `nlohmann::json` patterns `ParseSetEntityTrsRequest()`/
  `BuildSetEntityTrsResponseJson()` already use in this same file (parse via
  `ParseJsonNoThrow()`, `.is_discarded()`/`.is_object()` checks, field-by-field
  validation in the documented order, `nlohmann::json` object + `.dump()` for
  the response).
- `src/Application/Application.cpp` — **no change needed.** `PHASE1` already
  passed `&m_assetImportCommandBridge` as `NetworkServer`'s fifth constructor
  argument (confirmed by direct re-read of the real file before assuming this
  phase's own Section 3.5 still had work to do — see "Deviations" below).
- `docs/conventions/networking.md` — added a new bullet documenting
  `POST /import_asset` in the same style as the existing
  `/instantiate_primitive`/`/activate_tab` bullets: the new fifth bridge, the
  request/response shape, the 503 "Project panel not available" case, the
  120-second default timeout and why it's larger than every other bridge's,
  and a cross-reference to this campaign's
  `task_manager/stl-parser-2/PHASE0_MASTER_STRATEGY.md`.
- `tests/CMakeLists.txt` — registered both new test files (below) in
  `GTE_TEST_SOURCES`, immediately after the existing
  `Network/ActivateTabEndpointEndToEndTests.cpp` entry.

### Files added

- `tests/Network/NetworkRoutesImportAssetTests.cpp` — Tier 1, no
  httplib/thread involved, mirroring `NetworkRoutesTests.cpp`'s existing
  structure/naming conventions. Covers every case this phase's own Section
  3.7 lists: a fully valid request, `destination_folder` omitted (defaults to
  `""`), explicit JSON `null`, empty string, malformed JSON, non-object JSON,
  missing/empty/non-string `source_path`, a non-string `destination_folder`,
  an ignored extra field, and a full round-trip of
  `BuildImportAssetResponseJson()`'s exact JSON shape (including the
  "`converted_to_mesh_asset == false` still legally produces
  `mesh_source_format == ""`" case, and a message containing a quote
  round-tripping correctly through JSON escaping).
- `tests/Network/ImportAssetEndpointEndToEndTests.cpp` — mirrors
  `tests/Network/ActivateTabEndpointEndToEndTests.cpp`'s exact shape: a real
  `gte::AssetImportCommandBridge` + a real `gte::Network::NetworkServer` bound
  to an ephemeral port, a small `FakeAssetImportStandIn` stand-in thread that
  services the bridge the way `Application::Run()`'s real drain loop would,
  real HTTP requests via `httplib::Client`. Covers: a request against a
  nullptr bridge → 503; a request while the stand-in reports
  `projectAvailable = false` → 503; a request the stand-in fulfills with
  `success = true` → 200 with the exact JSON body; a request the stand-in
  fulfills with `success = false` → 400; a missing `source_path`/malformed
  JSON → 400 (short-circuiting before the bridge is ever touched); a request
  that never gets serviced within a short, explicit `SubmitAndWait()` timeout
  override (never the real 120000ms default) → 504; two concurrent requests →
  the second one gets `alreadyPending` → 503.

## Deviations from the strategy doc

One genuine, documented deviation — not a mistake in the strategy doc itself,
but a finding from re-verifying "what's actually on disk right now" per this
task's own instructions:

- **Section 3.1 (`NetworkServer.h`) and Section 3.5 (`Application.cpp`) both
  turned out to already be fully done, ahead of schedule, by `PHASE1`.**
  `PHASE1`'s own completion report explicitly documents that it added a real,
  defaulted, fifth `AssetImportCommandBridge*` constructor parameter/member on
  `NetworkServer` itself (`PHASE0_MASTER_STRATEGY.md`'s own table row for
  `PHASE1` says exactly this), and that `Application.cpp`'s constructor
  initializer list already passes `&m_assetImportCommandBridge` as
  `NetworkServer`'s fifth argument. Both were confirmed, by direct re-read of
  the real files (not merely trusted from the report), to already match this
  phase's own Sections 3.1/3.5 word-for-word before any edit was made — so no
  change was needed in either file this phase. This is exactly the scenario
  the task's own instructions anticipated ("an earlier phase may have already
  changed something referenced here") and is not a defect in `PHASE2`'s own
  document — its Section 3.2 explicitly says `NetworkServer.cpp`'s
  `RegisterRoutes(...)`-style function (not `NetworkServer.h`/`Application.cpp`)
  is what genuinely still needed this phase's own work, which matches what
  was actually true on disk.

No other deviation. Every other element of this phase's own Step 3 (3.2
through 3.7) was implemented exactly as specified: the route's exact
parse → 400 → nullptr-bridge → 503 → `SubmitAndWait()` →
`alreadyPending`/`timedOut` → 503/504 → `!projectAvailable` → 503 →
field-by-field copy → `200`/`400` status-code mapping; the exact
`ParsedImportAssetRequest`/`ImportedAssetResponseView` struct shapes and
validation-order doc comments; and the exact test coverage this phase's own
Section 3.7 lists for both new test files.

## Build & test results (this machine)

### Targeted build

```
cmake --build build --target GreatTamanaEngineTests
cmake --build build --target GreatTamanaEngine
```

Both targets built successfully with no new warnings from
`NetworkServer.h`/`.cpp`, `NetworkRoutes.h`/`.cpp`, or the two new test files.
`build/CMakeCache.txt` was confirmed to have both `GTE_ENABLE_EDITOR=ON` and
`GTE_ENABLE_PROJECT_PANEL=ON` before starting, so the real `ImGuiEditorLayer`/
`ProjectPanel` code path this endpoint ultimately drives (via `PHASE1`'s
bridge) was genuinely compiled and reachable at runtime, not skipped.

### Targeted test run (new tests only)

```
--gtest_filter=ParseImportAssetRequestTests.*:BuildImportAssetResponseJsonTests.*:BuildGenericErrorResponseJsonTests.UsedDirectlyForImportAssetFailurePath:ImportAssetEndpointEndToEndTest.*:ImportAssetEndpointNullBridgeTests.*

[==========] Running 23 tests from 5 test suites.
...
[==========] 23 tests from 5 test suites ran. (508 ms total)
[  PASSED  ] 23 tests.
```

### Adjacent regression spot-check (broader `Network`/bridge-backed suites)

Since `NetworkServer.cpp`'s `RegisterRoutes()` signature/call-site changed and
a new route was added right next to every existing bridge-backed route, the
existing Network test suites were re-run to confirm zero regression from this
phase's own changes (per this phase's workflow — a full `ctest` run is
deferred to `PHASE5`, but a broader spot-check beyond just the new tests was
warranted given the touched files):

```
--gtest_filter=NetworkServerTest.*:NetworkRoutesTests.*:ActivateTabEndpoint*:EngineCommandEndpoints*:CaptureEndpoints*:ParseInstantiate*:ParseDeleteEntity*:ParseSetEntityTrs*:ParseInstantiateLight*:BuildResponseJsonTests.*:AssetImportCommandBridgeTest.*

[==========] Running 90 tests from 13 test suites.
...
[==========] 90 tests from 13 test suites ran. (3341 ms total)
[  PASSED  ] 90 tests.
```

No full `ctest` regression run was performed in this phase (per the top-level
workflow rules: PHASE1–PHASE4 only get a targeted/filtered test run; the full
suite is `PHASE5`'s own job).

### Manual sanity check (live, running engine)

Per this phase's own Definition of Done — NOT the full `terrain.stl` smoke
test yet (that is `PHASE5`'s own job) — launched the real, built
`GreatTamanaEngine.exe` in the background and drove it purely over HTTP:

1. Confirmed the server was up: `GET /http_hello_world` → `200 "hello world"`.
2. `POST /import_asset` with a small throwaway external `.txt` file (living
   under the gitignored `_reference/` folder, well outside "Project" — the
   exact "import a file from outside Project" scenario this endpoint exists
   for) with no `destination_folder`:
   ```
   {"converted_to_ktx2":false,"converted_to_mesh_asset":false,"converted_to_motion_asset":false,
    "final_absolute_path":"C:\\Users\\F5954\\Documents\\TAMANA\\GreatTamanaEngine\\build\\Project\\phase2_smoke_test_throwaway.txt",
    "final_relative_path":"phase2_smoke_test_throwaway.txt","guid":"","mesh_source_format":"",
    "mesh_triangle_count":0,"mesh_vertex_count":0,
    "message":"Imported \"phase2_smoke_test_throwaway.txt\".","success":true}
   ```
   `200`, and the file was confirmed to have genuinely landed at
   `build/Project/phase2_smoke_test_throwaway.txt` on disk (directly browsed
   the real "Project" folder to confirm — the Editor's "Project" panel would
   show it on its own next rescan/frame, same as any other drag-and-dropped
   file).
3. `POST /import_asset` with a `destination_folder` of `"../escape"` (a
   path-traversal attempt) → `400`, `"destination_folder must not escape the
   Project root (no \"..\" segments)."` — confirming `PHASE1`'s hardening is
   genuinely wired all the way through this new route.
4. `POST /import_asset` with a `source_path` that does not exist on disk →
   `400`, `"The source file does not exist: ..."` — confirming a semantic
   import failure correctly maps to `400`, not a crash or a 5xx.
5. Cleanly terminated the engine process afterward; deleted both the
   throwaway source file and its imported copy in `build/Project/` so no
   stray artifact was left behind (neither location is tracked by git —
   `_reference/` and `build/` are both gitignored — so this was purely
   workspace hygiene, not a git-visible change).

## Definition-of-done checklist (this phase's slice)

- [x] `POST /import_asset` is live: valid request + available bridge + Project
  panel present → 200 with the documented JSON body; malformed
  JSON/missing `source_path` → 400; bridge null → 503; Project panel
  unavailable → 503; already-pending → 503; timed out → 504; import itself
  fails (bad file, path traversal, ...) → 400 — every one of these confirmed
  by either an automated test or the live manual check above.
- [x] Every new `NetworkRoutes.h`/`.cpp` function is Tier-1 tested; the
  end-to-end test file proves the full real-bridge/real-HTTP-server wiring
  works, mirroring `ActivateTabEndpointEndToEndTests.cpp`'s own proven
  pattern.
- [x] `docs/conventions/networking.md` documents the new endpoint.
- [x] `cmake --build build` succeeds for both `GreatTamanaEngineTests` and
  `GreatTamanaEngine`; a targeted/filtered test run of the new tests (23) plus
  a broader adjacent regression spot-check (90) both pass with zero
  regressions. A full `ctest` run is deferred to `PHASE5` per the top-level
  workflow rules.
- [x] Manual sanity check performed against a real, live, running engine: a
  small throwaway external file was genuinely imported into "Project" over
  HTTP, plus both a path-traversal attempt and a missing-source-file request
  were confirmed to correctly fail with `400`.

## Git

Source/test changes plus this report are committed together in one commit on
`feature/stl-parser-impl` (branch unchanged, per the workflow rules).
