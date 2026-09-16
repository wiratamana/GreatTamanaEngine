# PHASE4_INSTANTIATE_ASSET_NETWORK_ENDPOINT.md

**Campaign:** `stl-parser-2` (parent: `PHASE0_MASTER_STRATEGY.md` — READ FIRST)
**Branch:** `feature/stl-parser-impl`
**Depends on:** `PHASE3` (the `InstantiateMeshAsset` engine command must
already exist and compile).

## Step 1: The Goal (Where are we going?)

A real, working `POST /instantiate_asset` HTTP route, built entirely on top
of `PHASE3`'s new engine command and the ALREADY-EXISTING
`EngineCommandBridge` (no new bridge, per `PHASE0`'s Locked Design Decision
9). Unlike `/import_asset`, this route works in EVERY build configuration
(`GTE_ENABLE_EDITOR` ON or OFF) — it has no dependency on the Editor/
"Project" panel at all, exactly like `/instantiate_primitive` already
doesn't.

## Step 2: The Situation (Where are we now?)

- `server.Post("/instantiate_primitive", ...)` (`NetworkServer.cpp`) is the
  exact template this phase's own route copies — same
  `commandBridge == nullptr` → 503 check, same `SubmitAndWait()` →
  `alreadyPending`/`timedOut` → 503/504 mapping, same final
  `outcome.success ? 200 : 400` split.
- `ParseInstantiatePrimitiveRequest()`/`BuildInstantiatePrimitiveResponseJson()`
  (`NetworkRoutes.h`/`.cpp`) are the closest existing templates for this
  phase's own new parse/response functions — this endpoint's request is
  even SIMPLER (a single required string field, no optional groups at all).

## Step 3: The Plan

### 3.1 — `src/Network/NetworkRoutes.h` changes

Add a new section:

```cpp
// --- task_manager/stl-parser-2 campaign, PHASE4 - POST /instantiate_asset.
// Every function below stays PURE, same discipline as everything else in
// this file.

// Parsed, VALIDATED result of a POST /instantiate_asset request body:
// {"gta_path": "..."}. `valid == false` means `errorMessage` explains
// exactly why - every other field is meaningless in that case.
struct ParsedInstantiateAssetRequest {
    bool valid = false;
    std::string errorMessage;
    std::string gtaPath;
};

// Parses `jsonBody` (the raw POST body) for POST /instantiate_asset.
// Validation rules (checked in this order):
//   1. Same "must parse as a JSON object" rule as every other parser in
//      this file - "malformed JSON body" / "request body must be a JSON
//      object".
//   2. "gta_path" must be present, a JSON STRING, and non-empty -
//      otherwise "missing or invalid required field: gta_path". This
//      function does NOT check the path actually exists, is a valid Mesh
//      *.gta, or is absolute - see PHASE0_MASTER_STRATEGY.md's Locked
//      Design Decision #3's own clarifying note on why path resolution is
//      deliberately the CALLER's responsibility (pass an absolute path -
//      e.g. exactly what /import_asset's own response already returns as
//      "final_absolute_path" - PHASE2). Game::InstantiateMeshAssetFromGtaFile()
//      (PHASE3) is what actually reports a resolution/parse failure, via
//      its own errorMessage, mapped to a 400 by this endpoint's route
//      handler exactly like every other "well-formed request, semantic
//      failure downstream" case in this file.
// Unrecognized extra JSON fields are silently ignored.
ParsedInstantiateAssetRequest ParseInstantiateAssetRequest(const std::string& jsonBody);

// Builds POST /instantiate_asset's response body for its SUCCESS path only
// (failures reuse BuildGenericErrorResponseJson() directly at the route
// handler's own call site, exactly like BuildSetEntityTrsResponseJson()'s
// own documented split):
//   {"success":true,"entity":{"index":<uint>,"generation":<uint>},"name":"<resolvedName>"}
std::string BuildInstantiateAssetResponseJson(
    std::uint32_t entityIndex, std::uint32_t entityGeneration, const std::string& resolvedName);
```

### 3.2 — `src/Network/NetworkRoutes.cpp` changes

Implement both functions, following `ParseDeleteEntityRequest()`'s own
(simplest existing) pattern for the parser (a single required string
field), and `BuildDeleteEntityResponseJson()`'s own pattern for the
response builder (a single `"entity":{...}` object, plus this endpoint's
own extra `"name"` field).

### 3.3 — `src/Network/NetworkServer.cpp` changes

Add the new route, placed in the same file section as the other
`EngineCommandBridge`-backed POST routes:

```cpp
// task_manager/stl-parser-2 campaign, PHASE4 - POST /instantiate_asset.
// Reuses the SAME EngineCommandBridge/commandBridge every other
// /instantiate_* route already uses - see
// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #9 for why this is
// correct (an ECS/Renderer-mutating spawn, not an Editor/Project-panel
// concern - this route has NO dependency on assetImportCommandBridge at
// all, and works identically whether GTE_ENABLE_EDITOR is ON or OFF).
server.Post("/instantiate_asset", [commandBridge](const httplib::Request& req, httplib::Response& res) {
    const ParsedInstantiateAssetRequest parsed = ParseInstantiateAssetRequest(req.body);
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
    request.kind = EngineCommandKind::InstantiateMeshAsset;
    request.instantiateMeshAsset.absoluteGtaPath = parsed.gtaPath;

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

    const InstantiateMeshAssetOutcome& outcome = submit.result->instantiateMeshAsset;
    if (!outcome.success) {
        res.status = 400;
        res.set_content(BuildGenericErrorResponseJson(outcome.errorMessage), "application/json");
        return;
    }
    res.status = 200;
    res.set_content(BuildInstantiateAssetResponseJson(outcome.entityIndex, outcome.entityGeneration, outcome.resolvedName),
        "application/json");
});
```

**Note on default bridge timeout for this route:** unlike `/import_asset`,
this route uses `EngineCommandBridge`'s own EXISTING default timeout
(3000ms, unchanged) — spawning an already-decoded Mesh `*.gta` (parsing
already happened at import time; this is "just" a GPU upload + ECS entity
creation) is not expected to need anywhere near `/import_asset`'s
120-second allowance. If `PHASE5`'s real smoke test against the
1,045,458-triangle `terrain.stl` reveals this default is too short in
practice (a genuine possibility for a mesh this size — 3,136,374 vertices
uploaded to the GPU is real work), `PHASE5` itself is responsible for
noticing and reporting this, and MAY pass an explicit longer timeout
DIRECTLY at the `SubmitAndWait()` call site above if this phase's own
manual verification (Step 3.4 below) already shows it's necessary — do not
preemptively enlarge it without first observing a real timeout.

### 3.4 — Tests

New file `tests/Network/NetworkRoutesInstantiateAssetTests.cpp` (Tier 1):
valid request, malformed JSON, non-object JSON, missing `gta_path`, empty
`gta_path`, non-string `gta_path`, and a full assertion of
`BuildInstantiateAssetResponseJson()`'s exact JSON shape.

New end-to-end test file
`tests/Network/InstantiateAssetEndpointEndToEndTests.cpp`, mirroring
`tests/Network/EngineCommandEndpointsEndToEndTests.cpp`'s EXACT shape —
**confirmed the correct template during this campaign's Iteration 2
double-check (not `ActivateTabEndpointEndToEndTests.cpp`, an earlier draft's
guess — that file exercises the structurally DIFFERENT `EditorUiCommandBridge`,
with its own 404/409 status-code semantics that don't apply to
`/instantiate_asset` at all)**: `EngineCommandEndpointsEndToEndTests.cpp`'s
own `FakeEngineCommandStandIn` already runs a `switch` over every
`EngineCommandKind` and drains `EngineCommandBridge::TryPeekPendingCommandRequest()`/
`FulfillCommand()` exactly the way `Application::Run()` really does — add a
new `case EngineCommandKind::InstantiateMeshAsset:` branch there (or a small,
separate stand-in following the identical shape, if kept in the new file
instead) plus a real `gte::EngineCommandBridge` + real
`gte::Network::NetworkServer`, started on an ephemeral port, exactly mirroring
that file's existing `EngineCommandEndpointsEndToEndTest` fixture. Cover:
nullptr bridge → 503; a stand-in that fulfills with `success = true` → 200
with the exact JSON body; a stand-in that fulfills with `success = false` →
400 with the `errorMessage` echoed; already-pending → 503; timed out → 504.

Register every new test file in `tests/CMakeLists.txt`.

### 3.5 — `docs/conventions/networking.md` changes

Add a bullet for `POST /instantiate_asset`, in the same style/location as
the existing `/instantiate_primitive`/`/instantiate_light` bullets —
explicitly note it reuses the SAME `EngineCommandBridge`/`EngineCommandKind`
enum (a fifth value), works regardless of `GTE_ENABLE_EDITOR`, and is a
bare-bones wrapper (world origin, unparented, named after the file — no
position/name/parent input, unlike its `/instantiate_primitive` sibling).
Cross-reference `task_manager/stl-parser-2/PHASE0_MASTER_STRATEGY.md`.

## Definition of Done (this phase's slice)

- `POST /instantiate_asset` is live: valid `gta_path` pointing at a real
  Mesh `*.gta` + available bridge → 200 with `entity`/`name`; malformed
  JSON/missing `gta_path` → 400; bridge null → 503; already-pending → 503;
  timed out → 504; the underlying spawn itself fails (bad path/wrong asset
  type/empty mesh) → 400 with a descriptive `error` message.
- Works identically in a `GTE_ENABLE_EDITOR=OFF` build (verify this with an
  actual compile+link check in that configuration, not just by inspection).
- Every new `NetworkRoutes.h`/`.cpp` function is Tier-1 tested; the
  end-to-end test proves the full real-bridge/real-HTTP-server wiring
  works.
- `docs/conventions/networking.md` documents the new endpoint.
- `cmake --build build` succeeds; `ctest -C Debug --output-on-failure`
  passes with zero regressions.
- Manual sanity check (NOT the full `terrain.stl` smoke test yet — that is
  `PHASE5`): with a real built+running engine, and a small already-imported
  Mesh `*.gta` sitting in "Project" (e.g. from `PHASE2`'s own manual
  check, or a small PMX fixture), `POST /instantiate_asset` with its
  absolute path successfully spawns it, visible via `GET /get_swapchain`.
