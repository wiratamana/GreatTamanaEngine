# PHASE2_IMPORT_ASSET_NETWORK_ENDPOINT.md

**Campaign:** `stl-parser-2` (parent: `PHASE0_MASTER_STRATEGY.md` — READ FIRST)
**Branch:** `feature/stl-parser-impl`
**Depends on:** `PHASE1` (the `AssetImportCommandBridge` + `Application`
wiring must already exist and compile).

## Step 1: The Goal (Where are we going?)

A real, working `POST /import_asset` HTTP route, built entirely on top of
`PHASE1`'s bridge — the FIRST time this campaign's work becomes reachable
over the network at all.

## Step 2: The Situation (Where are we now?)

- `src/Network/NetworkRoutes.h`/`.cpp` is the engine's existing "pure,
  httplib-independent route handler logic" module — every request-parsing
  function (`ParseInstantiatePrimitiveRequest()`, `ParseSetEntityTrsRequest()`,
  ...) and every response-building function
  (`BuildInstantiatePrimitiveResponseJson()`, `BuildGenericErrorResponseJson()`,
  ...) lives here, built on the already-vendored `nlohmann::json` — see that
  file's own header comment for the full "why JSON parsing lives here, and
  only here" rationale.
- `src/Network/NetworkServer.cpp`'s `server.Post("/instantiate_primitive",
  ...)` (and its three POST siblings) is the exact template this phase's own
  route registration copies: parse the body → 400 on invalid JSON → check
  the relevant bridge pointer for `nullptr` → 503 → build the bridge request →
  `SubmitAndWait()` → `alreadyPending` → 503 → `timedOut` → 504 → map the
  outcome to a status code + JSON body.
- After `PHASE1`, `Application` already owns a working
  `AssetImportCommandBridge m_assetImportCommandBridge` and passes its
  address into `NetworkServer`'s constructor call site — but
  `NetworkServer.h`/`.cpp`'s own constructor signature does not accept a
  sixth pointer parameter yet, and no route reads it. This phase closes both
  gaps.

## Step 3: The Plan

### 3.1 — `src/Network/NetworkServer.h` changes

Forward-declare the new bridge type, right after the existing
`FrameDebuggerCommandBridge` forward-declare:

```cpp
// Forward-declared for the same cheap-header reason as the four bridges
// above - task_manager/stl-parser-2 campaign, PHASE2.
namespace gte { class AssetImportCommandBridge; }
```

Extend the constructor signature with a FIFTH defaulted, non-owning
pointer, appended AFTER `frameDebuggerCommandBridge` (so every existing
call site — including every `NetworkServer server;` no-argument test
construction — keeps compiling unchanged):

```cpp
explicit NetworkServer(FrameCaptureBridge* captureBridge = nullptr,
    EngineCommandBridge* commandBridge = nullptr,
    EditorUiCommandBridge* uiCommandBridge = nullptr,
    FrameDebuggerCommandBridge* frameDebuggerCommandBridge = nullptr,
    AssetImportCommandBridge* assetImportCommandBridge = nullptr);
```

Add the matching private member, `AssetImportCommandBridge*
m_assetImportCommandBridge = nullptr;`, right after
`m_frameDebuggerCommandBridge`, with the same "non-owning, Application owns
the real instance" doc comment every sibling member already has.

### 3.2 — `src/Network/NetworkServer.cpp` changes

- `#include "../Application/AssetImportCommandBridge.h"`.
- Update the constructor's implementation (wherever it stores the four
  existing pointers into members / passes them into `RegisterRoutes()`) to
  also store/forward the fifth pointer.
- Whatever internal `RegisterRoutes(...)`-style function receives the other
  four bridge pointers today must also receive
  `AssetImportCommandBridge* assetImportCommandBridge` — check the exact
  existing signature before assuming its shape, and extend it consistently
  (same parameter-ordering convention as the constructor above: appended
  last).

Add the new route, placed in the same file section as the other
bridge-backed POST routes (near `/instantiate_primitive`):

```cpp
// task_manager/stl-parser-2 campaign, PHASE2 - POST /import_asset. See
// PHASE0_MASTER_STRATEGY.md's own locked endpoint contract for the exact
// status-code mapping implemented below, and AGENTS.md's "Networking" for
// why this handler still never touches AssetDatabase/ProjectPanel
// directly - only AssetImportCommandBridge::SubmitAndWait().
server.Post("/import_asset", [assetImportCommandBridge](const httplib::Request& req, httplib::Response& res) {
    const ParsedImportAssetRequest parsed = ParseImportAssetRequest(req.body);
    if (!parsed.valid) {
        res.status = 400;
        res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
        return;
    }
    if (assetImportCommandBridge == nullptr) {
        res.status = 503;
        res.set_content(BuildGenericErrorResponseJson("asset import command bridge not available"), "application/json");
        return;
    }

    AssetImportCommandRequest request;
    request.kind = AssetImportCommandKind::ImportExternalFile;
    request.importExternalFile.sourceAbsolutePath = parsed.sourcePath;
    request.importExternalFile.destinationRelativeFolder = parsed.destinationFolder;

    // PHASE0's Locked Design Decision #6 - this bridge's own 120000ms
    // default (see AssetImportCommandBridge.h) is used here EXPLICITLY
    // (pass no second argument to SubmitAndWait()) - do not shorten it to
    // match the other bridges' 3000ms default.
    const AssetImportCommandBridge::SubmitResult submit = assetImportCommandBridge->SubmitAndWait(request);
    if (submit.alreadyPending) {
        res.status = 503;
        res.set_content(BuildGenericErrorResponseJson("another asset import is already in progress"), "application/json");
        return;
    }
    if (submit.timedOut) {
        res.status = 504;
        res.set_content(BuildGenericErrorResponseJson("asset import timed out"), "application/json");
        return;
    }

    const ImportExternalFileOutcome& outcome = submit.result->importExternalFile;
    if (!outcome.projectAvailable) {
        res.status = 503;
        res.set_content(BuildGenericErrorResponseJson(
            "the Editor's \"Project\" panel is not available in this build (GTE_ENABLE_EDITOR/"
            "GTE_ENABLE_PROJECT_PANEL is OFF)"), "application/json");
        return;
    }

    // NetworkRoutes.h's own "must never depend on src/Application/" rule
    // means BuildImportAssetResponseJson() takes an ImportedAssetResponseView
    // (this file's OWN type, Section 3.3 below), never the Application-layer
    // ImportExternalFileOutcome directly - copy field-by-field first:
    ImportedAssetResponseView view;
    view.message = outcome.message;
    view.finalRelativePath = outcome.finalRelativePath;
    view.finalAbsolutePath = outcome.finalAbsolutePath;
    view.guid = outcome.guid;
    view.convertedToMeshAsset = outcome.convertedToMeshAsset;
    view.meshSourceFormat = outcome.meshSourceFormat;
    view.convertedToKtx2 = outcome.convertedToKtx2;
    view.convertedToMotionAsset = outcome.convertedToMotionAsset;
    view.meshVertexCount = outcome.meshVertexCount;
    view.meshTriangleCount = outcome.meshTriangleCount;
    res.status = outcome.success ? 200 : 400;
    res.set_content(outcome.success ? BuildImportAssetResponseJson(view) : BuildGenericErrorResponseJson(outcome.message),
        "application/json");
});
```

### 3.3 — `src/Network/NetworkRoutes.h` changes

Add a new section (mirroring the existing campaign-delimited sections in
this file), with:

```cpp
// --- task_manager/stl-parser-2 campaign, PHASE2 - POST /import_asset.
// Every function below stays PURE - no httplib/socket/thread/Registry/Game/
// Renderer/AssetDatabase/ProjectPanel dependency of any kind, exactly like
// everything else in this file (see this file's own header comment).
// NetworkServer.cpp (Phase 2's own Section 3.2) is the one place that
// converts a parsed request into a real AssetImportCommandRequest and
// calls AssetImportCommandBridge::SubmitAndWait().

// Parsed, VALIDATED result of a POST /import_asset request body:
// {"source_path": "...", "destination_folder": "..." (optional)}.
// `valid == false` means `errorMessage` explains exactly why - every other
// field is meaningless in that case.
struct ParsedImportAssetRequest {
    bool valid = false;
    std::string errorMessage;
    std::string sourcePath;
    std::string destinationFolder; // "" when omitted - means "the Project root itself".
};

// Parses `jsonBody` (the raw POST body) for POST /import_asset. Validation
// rules (checked in this order - the FIRST failure found is what
// `errorMessage` reports):
//   1. `jsonBody` must parse as valid JSON at all, and the top-level value
//      must be a JSON OBJECT - otherwise "malformed JSON body" / "request
//      body must be a JSON object" (same exact message convention as
//      ParseSetEntityTrsRequest() above).
//   2. "source_path" must be present, a JSON STRING, and non-empty -
//      otherwise "missing or invalid required field: source_path". This
//      function does NOT check the path actually exists on disk, is
//      readable, or names a file rather than a directory - that is
//      ProjectPanel::ImportExternalFile()'s own job (PHASE1), reported
//      back through the bridge's own outcome/message, mapped to a 400 by
//      NetworkServer.cpp's own route handler (Section 3.2) exactly like
//      any other "well-formed request, semantic failure" case elsewhere in
//      this file.
//   3. "destination_folder" is OPTIONAL. Absent, JSON null, or an empty
//      string all mean "" (the Project root itself). Any other JSON type
//      (number/bool/object/array) is a validation FAILURE:
//      "destination_folder must be a string". This function does NOT
//      itself reject a path-traversal value (e.g. "../../outside") - that
//      hardening lives in ProjectPanel::ImportExternalFile() (PHASE1),
//      exactly mirroring point 2 above's own "semantic checks happen
//      downstream" split.
// Unrecognized extra JSON fields are silently ignored, same
// forward-compatible convention as everywhere else in this file.
ParsedImportAssetRequest ParseImportAssetRequest(const std::string& jsonBody);

// A plain, AssetImportCommandBridge-independent view of one /import_asset
// outcome, for BuildImportAssetResponseJson() below - NetworkServer.cpp is
// the one place that copies a real ImportExternalFileOutcome
// (src/Application/AssetImportCommandBridge.h) into this struct, one field
// at a time, mirroring TextureListEntryView's/TransformSnapshotView's own
// "a struct crossing a layer boundary is never accepted directly here"
// precedent. NOTE: unlike those two examples, this one is DELIBERATELY THE
// SAME SHAPE as ImportExternalFileOutcome (nothing is dropped/renamed) -
// still kept as its own, separate type for the same "this file must never
// depend on src/Application/" reason.
struct ImportedAssetResponseView {
    std::string message;
    std::string finalRelativePath;
    std::string finalAbsolutePath;
    std::string guid;
    bool convertedToMeshAsset = false;
    std::string meshSourceFormat;
    bool convertedToKtx2 = false;
    bool convertedToMotionAsset = false;
    std::uint64_t meshVertexCount = 0;
    std::uint64_t meshTriangleCount = 0;
};

// Builds POST /import_asset's response body for its SUCCESS path only
// (`view.success` does not exist on this struct on purpose - NetworkServer.cpp's
// own route handler, Section 3.2, calls BuildGenericErrorResponseJson()
// DIRECTLY for every failure case, exactly like BuildSetEntityTrsResponseJson()'s
// own documented split convention):
//   {"success":true,"message":"...",
//    "final_relative_path":"...","final_absolute_path":"...","guid":"...",
//    "converted_to_mesh_asset":bool,"mesh_source_format":"stl"|"pmx"|"",
//    "converted_to_ktx2":bool,"converted_to_motion_asset":bool,
//    "mesh_vertex_count":uint64,"mesh_triangle_count":uint64}
std::string BuildImportAssetResponseJson(const ImportedAssetResponseView& view);
```

**Double-check note:** `NetworkServer.cpp`'s route handler in 3.2 above
already shows the corrected, final shape — `BuildImportAssetResponseJson()`
is called with a freshly-populated `ImportedAssetResponseView` (this file's
OWN type), never the `Application`-layer `ImportExternalFileOutcome`
directly, since this file must never depend on `src/Application/` (same rule
`BuildSetEntityTrsResponseJson()` already follows). Do not skip the
field-by-field copy shown in 3.2 when actually implementing this.

### 3.4 — `src/Network/NetworkRoutes.cpp` changes

Implement the two new functions, following the exact `nlohmann::json`
patterns `ParseSetEntityTrsRequest()`/`BuildSetEntityTrsResponseJson()`
already use in this same file (parse via `nlohmann::json::parse(jsonBody,
nullptr, false)`, check `.is_discarded()`/`.is_object()`, field-by-field
`.contains()`/`.is_string()`/etc checks in the documented order, build the
response via a `nlohmann::json` object then `.dump()`).

### 3.5 — `src/Application/Application.cpp` changes

Update the `m_networkServer(...)` constructor-initializer-list call (or
wherever `NetworkServer`'s constructor is actually invoked) to also pass
`&m_assetImportCommandBridge` as the fifth argument.

### 3.6 — `docs/conventions/networking.md` changes

Add a new bullet documenting `POST /import_asset`, in the same style as the
existing `/instantiate_primitive`/`/instantiate_light` bullets — mention:
the new `AssetImportCommandBridge` (fifth bridge), the request/response
shape, the 503 "Project panel not available" case, the 120-second default
timeout and why it's larger than every other bridge's, and a cross-reference
to this campaign's `task_manager/stl-parser-2/PHASE0_MASTER_STRATEGY.md`
for the full writeup (matching every existing bullet's own
"see `task_manager/.../PHASE0_MASTER_STRATEGY.md` for the full N-phase
campaign writeup" closing sentence).

### 3.7 — Tests

New file `tests/Network/NetworkRoutesImportAssetTests.cpp` (Tier 1, no
httplib/thread involved) covering `ParseImportAssetRequest()`/
`BuildImportAssetResponseJson()` — mirror
`tests/Network/NetworkRoutesTests.cpp`'s existing structure/naming
conventions for `ParseInstantiatePrimitiveRequest`/
`ParseSetEntityTrsRequest`. Cover at minimum: valid full request, valid
request with `destination_folder` omitted (defaults to `""`), malformed
JSON, non-object JSON, missing `source_path`, empty `source_path`,
non-string `source_path`, `destination_folder` present as a non-string
type, `destination_folder` explicitly `null` (behaves like omitted), and a
full round-trip of `BuildImportAssetResponseJson()`'s exact JSON shape
(assert every key/value, including that `converted_to_mesh_asset == false`
still legally produces `mesh_source_format == ""`).

New end-to-end test file `tests/Network/ImportAssetEndpointEndToEndTests.cpp`,
mirroring `tests/Network/ActivateTabEndpointEndToEndTests.cpp`'s EXACT shape
(a real `gte::AssetImportCommandBridge` + a real `gte::Network::NetworkServer`
bound to an ephemeral port, a small stand-in thread that services the bridge
the way `Application::Run()`'s real drain loop would, real HTTP requests via
whatever HTTP client that existing end-to-end test file already uses).
Cover: a request against a nullptr bridge → 503; a request while the
stand-in reports `projectAvailable = false` → 503; a request the stand-in
fulfills with `success = true` → 200 with the exact JSON body; a request
the stand-in fulfills with `success = false` → 400; a request that never
gets serviced within a SHORT test-only timeout (pass an explicit small
timeout via whatever mechanism the existing end-to-end test already uses to
avoid the real 120000ms default) → 504; two concurrent requests → the
second one gets `alreadyPending` → 503.

Register every new test file in `tests/CMakeLists.txt`.

## Definition of Done (this phase's slice)

- `POST /import_asset` is live: valid request + available bridge + Project
  panel present → 200 with the documented JSON body; malformed
  JSON/missing `source_path` → 400; bridge null → 503; Project panel
  unavailable → 503; already-pending → 503; timed out → 504; import itself
  fails (bad file, path traversal, ...) → 400.
- Every new `NetworkRoutes.h`/`.cpp` function is Tier-1 tested; the
  end-to-end test file proves the full real-bridge/real-HTTP-server wiring
  works, mirroring `ActivateTabEndpointEndToEndTests.cpp`'s own proven
  pattern.
- `docs/conventions/networking.md` documents the new endpoint.
- `cmake --build build` succeeds; `ctest -C Debug --output-on-failure`
  passes with zero regressions.
- Manual sanity check (NOT the full `terrain.stl` smoke test yet — that is
  `PHASE5`): with a real built+running engine, `POST /import_asset` with a
  small throwaway external test file (e.g. a tiny PNG) successfully lands
  it in "Project", visible in the Editor's own "Project" panel on its next
  frame.
