# PHASE4_COMPLETION_REPORT.md — Instantiate Asset Network Endpoint

**Phase:** `PHASE4_INSTANTIATE_ASSET_NETWORK_ENDPOINT.md`
**Campaign:** `stl-parser-2` (parent: `PHASE0_MASTER_STRATEGY.md`)
**Branch:** `feature/stl-parser-impl` (unchanged, as required)

## Summary

Implemented a real, working `POST /instantiate_asset` HTTP route, built
entirely on top of `PHASE3`'s already-shipped `EngineCommandKind::InstantiateMeshAsset`
engine command and the ALREADY-EXISTING `EngineCommandBridge` (no new bridge,
per `PHASE0`'s Locked Design Decision 9). Every step of this phase document's
own "Step 3: The Plan" (3.1 through 3.5) was implemented exactly as
specified, re-verified against the real, current state of every file it
references before editing, per this task's own instructions.

### Files changed

- `src/Network/NetworkRoutes.h` — added a new campaign-delimited section
  (mirroring every existing one in this file, placed right after the
  `PHASE2`/`/import_asset` section) with `ParsedInstantiateAssetRequest`,
  `ParseInstantiateAssetRequest()`, and `BuildInstantiateAssetResponseJson()` —
  every doc comment copied verbatim from the phase document's own Section 3.1
  code block.
- `src/Network/NetworkRoutes.cpp` — implemented both new functions following
  `ParseDeleteEntityRequest()`'s own (simplest existing) single-required-
  string-field pattern for the parser, and `BuildDeleteEntityResponseJson()`'s
  own pattern (a single `"entity":{...}` object plus this endpoint's own extra
  `"name"` field) for the response builder — exactly per the phase document's
  own Section 3.2 instruction.
- `src/Network/NetworkServer.cpp` — added the new
  `server.Post("/instantiate_asset", ...)` route inside `RegisterRoutes()`,
  placed immediately after `/import_asset` (the last existing bridge-backed
  POST route) — parse via `ParseInstantiateAssetRequest()` → 400 on invalid
  JSON/missing `gta_path` → `commandBridge == nullptr` → 503 → build an
  `EngineCommandRequest` with `kind = EngineCommandKind::InstantiateMeshAsset`
  → `SubmitAndWait()` (no timeout override — `EngineCommandBridge`'s own
  unchanged 3000ms default is used, per the phase document's own Section 3.3
  note) → `alreadyPending` → 503 → `timedOut` → 504 → `!outcome.success` →
  400 with `outcome.errorMessage` → `200` with
  `BuildInstantiateAssetResponseJson(...)`. Matches the phase document's own
  Section 3.3 code block exactly — no `AssetImportCommandBridge` dependency
  anywhere in this new route, confirming it works identically regardless of
  `GTE_ENABLE_EDITOR`.
- `docs/conventions/networking.md` — added a new bullet documenting
  `POST /instantiate_asset`, in the same style/location as the existing
  `/import_asset` bullet immediately above it: the fifth `EngineCommandKind`
  reuse (not a new bridge), the bare-bones spawn contract (world origin,
  unparented, named after the file), the unchanged 3000ms bridge timeout
  (contrasted with `/import_asset`'s own 120-second allowance and why), and a
  cross-reference to `task_manager/stl-parser-2/PHASE0_MASTER_STRATEGY.md`.
- `tests/CMakeLists.txt` — registered both new test files (below) in
  `GTE_TEST_SOURCES`, immediately after the existing
  `Network/ImportAssetEndpointEndToEndTests.cpp` entry.

### Files added

- `tests/Network/NetworkRoutesInstantiateAssetTests.cpp` — Tier 1, no
  httplib/thread involved, mirroring `NetworkRoutesImportAssetTests.cpp`'s
  existing structure/naming conventions. Covers: a fully valid payload, a
  malformed JSON body, a top-level JSON array, a missing `gta_path`, an
  empty-string `gta_path`, a non-string `gta_path`, an ignored extra field,
  a full assertion of `BuildInstantiateAssetResponseJson()`'s exact JSON
  shape (including a name containing a quote round-tripping correctly
  through JSON escaping), and a re-confirmation that
  `BuildGenericErrorResponseJson()` is the shared builder this endpoint's
  own failure path reuses directly.
- `tests/Network/InstantiateAssetEndpointEndToEndTests.cpp` — mirrors
  `tests/Network/EngineCommandEndpointsEndToEndTests.cpp`'s exact shape (the
  template this campaign's own Iteration 2 double-check confirmed correct,
  NOT `ActivateTabEndpointEndToEndTests.cpp` — see `PHASE0_MASTER_STRATEGY.md`'s
  own "Double-Check Pass" note 3). A real `gte::EngineCommandBridge` + a real
  `gte::Network::NetworkServer` bound to an ephemeral port, hit over a REAL
  loopback socket via `httplib::Client`. Per the phase document's own Section
  3.4 ("or a small, separate stand-in following the identical shape, if kept
  in the new file instead"), this file defines its OWN small,
  dedicated `FakeInstantiateMeshAssetStandIn` — a deliberate choice over
  reaching into `EngineCommandEndpointsEndToEndTests.cpp`'s own
  `FakeEngineCommandStandIn`, which lives in that OTHER file's own anonymous
  namespace and is therefore not reachable from a different translation
  unit at all (not a stylistic preference — the only two real options were
  "duplicate a switch case into a different file's private class" — which is
  literally impossible across translation units without exposing that class
  first — or "write this file's own small, narrow stand-in", and the phase
  document's own wording already anticipated exactly this). Covers: a
  nullptr bridge → 503; a stand-in that fulfills with `success = true` → 200
  with the exact JSON body (`entity.index`/`entity.generation`/`name`); a
  stand-in that fulfills with `success = false` → 400 with the
  `errorMessage` echoed as `error`; a missing `gta_path` → 400; malformed
  JSON → 400; two concurrent requests → the second one gets `alreadyPending`
  → 503; a request that never gets serviced within a short, explicit
  `SubmitAndWait()` timeout override (never the real 3000ms default) → 504.

## Deviations from the strategy doc

None. Every element of this phase document's own Step 3 (3.1 through 3.5)
was implemented exactly as specified: the exact `ParsedInstantiateAssetRequest`/
response-builder shapes and validation rules, the exact route-handler code
block (parse → 400 → nullptr-bridge → 503 → `SubmitAndWait()` →
`alreadyPending`/`timedOut` → 503/504 → `!outcome.success` → 400 →
`200`), the unchanged 3000ms bridge timeout, and the exact test coverage
this phase's own Section 3.4 lists.

One implementation-detail choice worth calling out explicitly (not a
deviation, since the phase document itself offered this as one of two
equally-acceptable options): the end-to-end test file's own stand-in is a
brand-new, small, dedicated class (`FakeInstantiateMeshAssetStandIn`) rather
than an added `case` inside the pre-existing `EngineCommandEndpointsEndToEndTests.cpp`'s
own `FakeEngineCommandStandIn` switch — see "Files added" above for why that
was the only realistic option once cross-translation-unit visibility is
considered. This also means `EngineCommandEndpointsEndToEndTests.cpp` itself
was left completely untouched by this phase, which was independently
re-verified (via the regression run below) to still pass unchanged.

## Build & test results (this machine)

### Targeted build

```
cmake --build build --target GreatTamanaEngineTests
cmake --build build --target GreatTamanaEngine
```

Both targets built successfully with no new warnings from
`NetworkRoutes.h`/`.cpp`, `NetworkServer.cpp`, or the two new test files.
`build/CMakeCache.txt` was confirmed to have `GTE_ENABLE_EDITOR=ON` before
starting, matching every previous phase's own build configuration.

### `GTE_ENABLE_EDITOR=OFF` compile+link check (per this phase's own Definition of Done)

Per this phase document's own explicit instruction ("verify this with an
actual compile+link check in that configuration, not just by inspection"),
the pre-existing `build-editor-off` directory (`GTE_ENABLE_EDITOR:BOOL=OFF`,
confirmed via its own `CMakeCache.txt`) was rebuilt:

```
cmake --build build-editor-off --target GreatTamanaEngine
```

Succeeded cleanly (47 objects rebuilt/relinked, including the changed
`NetworkRoutes.cpp`/`NetworkServer.cpp`), confirming `POST /instantiate_asset`
compiles and links with zero dependency on any Editor-only code, exactly as
this route's own design requires (it never touches `assetImportCommandBridge`
at all).

### Targeted test run (new tests only)

```
--gtest_filter=ParseInstantiateAssetRequestTests.*:BuildInstantiateAssetResponseJsonTests.*:InstantiateAssetEndpointEndToEndTest.*:InstantiateAssetEndpointNullBridgeTests.*:BuildGenericErrorResponseJsonTests.*

[==========] Running 18 tests from 5 test suites.
...
[==========] 18 tests from 5 test suites ran. (555 ms total)
[  PASSED  ] 18 tests.
```

(`BuildGenericErrorResponseJsonTests.*` intentionally also re-runs
`PHASE2`'s own pre-existing `UsedDirectlyForImportAssetFailurePath` case
alongside this phase's new `UsedDirectlyForInstantiateAssetFailurePath` case
— both passed.)

### Adjacent regression spot-check (broader `Network`/`EngineCommand`/`ImportAsset` suites)

Since `NetworkServer.cpp`'s `RegisterRoutes()` body gained a new route right
next to every existing bridge-backed route, and this is the FIFTH value on
the shared `EngineCommandBridge`/`EngineCommandKind` enum, a broader
spot-check beyond just the new tests was run for extra confidence (a full
`ctest` run is still deferred to `PHASE5`, per the top-level workflow rules):

```
--gtest_filter=*EngineCommand*:*Network*:*ImportAsset*:*InstantiateAsset*

[==========] Running 91 tests from 18 test suites.
...
[==========] 91 tests from 18 test suites ran. (4899 ms total)
[  PASSED  ] 91 tests.
```

Zero regressions. In particular, every pre-existing
`EngineCommandEndpointsEndToEndTest` case (`InstantiatePrimitive`/
`DeleteEntity`/`SetEntityTrs`/`InstantiateLight`, including the full
round-trip test) and every `ImportAssetEndpointEndToEndTest` case still pass
unchanged, confirming this phase's new route/enum value did not disturb any
of the other four.

No full `ctest` regression run was performed in this phase (per the
top-level workflow rules: PHASE1–PHASE4 only get a targeted/filtered test
run; the full suite is `PHASE5`'s own job).

### Manual sanity check (live, running engine)

Per this phase's own Definition of Done — NOT the full `terrain.stl` smoke
test yet (that is `PHASE5`'s own job) — launched the real, built
`GreatTamanaEngine.exe` in the background and drove it purely over HTTP:

1. Confirmed the server was up: `GET /http_hello_world` → `200 "hello world"`.
2. Created a tiny, throwaway, single-triangle ASCII `.stl` fixture under the
   gitignored `_reference/` folder
   (`_reference/phase4_smoke_triangle.stl`) and imported it via
   `POST /import_asset` (no `destination_folder`):
   ```
   200 {"...,"final_absolute_path":"...\\build\\Project\\phase4_smoke_triangle.gta",
        "converted_to_mesh_asset":true,"mesh_source_format":"stl",
        "mesh_vertex_count":3,"mesh_triangle_count":1,"success":true}
   ```
3. `POST /instantiate_asset` with `gta_path` set to that exact
   `final_absolute_path`:
   ```
   200 {"entity":{"generation":1,"index":1},"name":"phase4_smoke_triangle","success":true}
   ```
4. `GET /get_swapchain` confirmed, visually, that a real triangle mesh now
   sits in the Scene — both the "Scene" and "Game" Editor panels show it,
   correctly lit (matching the late-afternoon directional light already in
   `TestScene.gtscene`), and the "Hierarchy" panel shows the new
   `phase4_smoke_triangle` entity (with its own child `Entity 2`, the actual
   mesh-renderer node `CreateMeshEntityFromGtaFile()` creates underneath the
   named root) sitting alongside the pre-existing `Entity 0 (Camera)`. The
   "Project" panel also shows the freshly-imported `phase4_smoke_triangle.gta`
   file.
5. `POST /instantiate_asset` with a `gta_path` that does not exist on disk
   (`"C:\\does\\not\\exist.gta"`) → `400`,
   `"failed to load or spawn a Mesh asset from \"C:\\does\\not\\exist.gta\" -
   the file may be missing, not a valid Mesh *.gta, or empty (zero
   vertices/triangles)"` — confirming a semantic spawn failure correctly maps
   to `400`, not a crash or a 5xx.
6. Cleanly terminated the engine process afterward; deleted both the
   throwaway source `.stl` and its imported `.gta` copy (neither location is
   tracked by git — `_reference/` and `build/` are both gitignored — so this
   was purely workspace hygiene, confirmed via `git status` showing no
   unexpected changes/untracked files from this check).

## Definition-of-done checklist (this phase's slice)

- [x] `POST /instantiate_asset` is live: valid `gta_path` pointing at a real
  Mesh `*.gta` + available bridge → 200 with `entity`/`name`; malformed
  JSON/missing `gta_path` → 400; bridge null → 503; already-pending → 503;
  timed out → 504; the underlying spawn itself fails (bad path/wrong asset
  type/empty mesh) → 400 with a descriptive `error` message — every one of
  these confirmed by either an automated test or the live manual check
  above.
- [x] Works identically in a `GTE_ENABLE_EDITOR=OFF` build — confirmed via an
  actual compile+link check (`build-editor-off`), not just by inspection.
- [x] Every new `NetworkRoutes.h`/`.cpp` function is Tier-1 tested; the
  end-to-end test proves the full real-bridge/real-HTTP-server wiring works.
- [x] `docs/conventions/networking.md` documents the new endpoint.
- [x] `cmake --build build` succeeds for both `GreatTamanaEngineTests` and
  `GreatTamanaEngine`; a targeted/filtered test run of the new tests (18)
  plus a broader adjacent regression spot-check (91) both pass with zero
  regressions. A full `ctest` run is deferred to `PHASE5` per the top-level
  workflow rules.
- [x] Manual sanity check performed against a real, live, running engine: a
  small throwaway STL fixture was imported and then instantiated purely over
  HTTP, visually confirmed via `GET /get_swapchain`, plus a
  nonexistent-`gta_path` request was confirmed to correctly fail with `400`.

## Git

Source/test changes plus this report are committed together in one commit on
`feature/stl-parser-impl` (branch unchanged, per the workflow rules).
