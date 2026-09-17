# PHASE5 — `POST /save_scene` and `POST /load_scene` Network Endpoints

_Part of `task_manager/scene-serialization-2/`. **Parent: `PHASE0_MASTER_STRATEGY.md` — read it first.**_
Depends on: PHASE1–PHASE4 (the save/load system must already be fully
correct before it is exposed over HTTP).
Branch: `feature/scene-serialization`.

## Step 1: The Goal

Add `POST /save_scene` and `POST /load_scene` as a thin network bridge to
the now-fully-working `Editor/SceneIO.h` `SaveScene()`/`LoadScene()`
functions — exactly the same shape every other ECS-mutating endpoint in this
engine already has (`/instantiate_primitive`, `/instantiate_asset`, ...). No
new engine logic here — this phase is 100% wiring.

## Step 2: The Situation / The Problem

Studied directly (`src/Application/EngineCommandBridge.h`,
`EngineCommandDispatch.cpp`, `src/Network/NetworkRoutes.h`,
`NetworkServer.cpp`) before writing this — the exact, established pattern
every prior endpoint follows:

1. `NetworkRoutes.h`/`.cpp` gets a `ParseXxxRequest(jsonBody)` (pure,
   `httplib`/`Registry`/`Game`-independent) and a `BuildXxxResponseJson(...)`.
2. `EngineCommandBridge.h` gets a new `EngineCommandKind` enumerator, a
   plain request-payload struct, and a plain outcome struct added to the
   existing `EngineCommandRequest`/`EngineCommandResult` tagged unions.
3. `EngineCommandDispatch.cpp` gets a new `case` branch that calls the
   real `Game`/`Editor` function.
4. `NetworkServer.cpp`'s `RegisterRoutes()` gets a new `server.Post(...)`
   lambda: parse -> bridge-null check (503) -> build request -> `SubmitAndWait()`
   -> map `alreadyPending`/`timedOut`/outcome to a status code + response.

`Editor/SceneIO.h`'s `SaveScene()`/`LoadScene()` are compiled ONLY when
`GTE_ENABLE_EDITOR` is ON (they depend on `Editor/ProjectRootPath.h`).
`EngineCommandDispatch.cpp` is a CORE, always-compiled file (it is included
in both `GTE_ENABLE_EDITOR=ON` and `=OFF` builds) — so its new `case`
branches for Save/Load scene must be conditionally compiled, exactly the
same shape `POST /import_asset`'s own `ImportExternalFileOutcome::
projectAvailable` flag already establishes for the "Editor/Project panel
not available" 503 case (see `NetworkServer.cpp`'s `/import_asset` route,
lines already read while researching PHASE0).

## Step 3: The Plan

### 3.1 — `src/Game/EngineCommandResults.h`: two new outcome structs

```cpp
// task_manager/scene-serialization-2 campaign, PHASE5 - see
// Editor/SceneIO.h's SaveScene()/LoadScene() for the actual work these
// wrap. `editorAvailable == false` means this build was compiled with
// GTE_ENABLE_EDITOR=OFF - SceneIO.h does not even exist in that
// configuration, so `success` is always false and `errorMessage` explains
// why, mirroring ImportExternalFileOutcome::projectAvailable's own
// precedent (see NetworkRoutes.h/NetworkServer.cpp's existing
// /import_asset route).
struct SaveSceneOutcome {
    bool success = false;
    bool editorAvailable = true;
    std::string errorMessage;
    std::string resolvedPath; // the ABSOLUTE path actually written to, meaningful only when success == true.
};

struct LoadSceneOutcome {
    bool success = false;
    bool editorAvailable = true;
    std::string errorMessage;
    std::string resolvedPath; // the ABSOLUTE path actually read from, meaningful only when success == true.
};
```

### 3.2 — `src/Application/EngineCommandBridge.h`: two new command kinds

Add `SaveScene`, `LoadScene` to `EngineCommandKind` (append at the end,
exactly like `InstantiateMeshAsset` was appended for `stl-parser-2` — never
reorder/renumber the existing values). Add:

```cpp
struct SaveSceneCommand {
    std::string path; // "" means "use Editor::SceneIO.h's own DefaultScenePath()".
};
struct LoadSceneCommand {
    std::string path; // "" means "use DefaultScenePath()".
};
```

Add `SaveSceneCommand saveScene;`/`LoadSceneCommand loadScene;` to
`EngineCommandRequest`, and `SaveSceneOutcome saveScene;`/
`LoadSceneOutcome loadScene;` to `EngineCommandResult` (mirrors every
existing field in both structs exactly).

### 3.3 — `Editor/SceneIO.h`/`.cpp`: add explicit-path overloads

The EXISTING zero-argument `SaveScene(Game&)`/`LoadScene(Game&, Renderer&)`
(still used by `Editor/DockLayout.cpp`'s Ctrl+S/Ctrl+O — DO NOT change
their signature or behavior) each get a sibling overload taking an explicit
path:

```cpp
bool SaveScene(Game& game, const std::filesystem::path& scenePath);
bool SaveScene(Game& game); // unchanged - forwards to the overload above with DefaultScenePath().

bool LoadScene(Game& game, Renderer& renderer, const std::filesystem::path& scenePath);
bool LoadScene(Game& game, Renderer& renderer); // unchanged - forwards to the overload above with DefaultScenePath().
```

Move the existing bodies into the new explicit-path overloads verbatim,
replacing every internal use of `DefaultScenePath()` with the `scenePath`
parameter; the zero-argument overloads become one-line forwarders.

### 3.4 — `src/Application/EngineCommandDispatch.cpp`: dispatch + the `GTE_ENABLE_EDITOR`-off path

This file currently includes only `Game.h`/`Renderer.h` (both core,
always-compiled). Add, guarded:

```cpp
#if GTE_ENABLE_EDITOR
#include "../Editor/SceneIO.h"
#endif
```

New `case` branches inside `ExecuteEngineCommand()`:

```cpp
case EngineCommandKind::SaveScene: {
#if GTE_ENABLE_EDITOR
    const std::filesystem::path path = request.saveScene.path.empty()
        ? DefaultScenePath()
        : std::filesystem::path(request.saveScene.path);
    result.saveScene.success = SaveScene(game, path);
    result.saveScene.resolvedPath = path.string();
    if (!result.saveScene.success) {
        result.saveScene.errorMessage = "failed to write scene file (I/O error) - see engine log";
    }
#else
    result.saveScene.editorAvailable = false;
    result.saveScene.errorMessage =
        "scene save/load requires the Editor module (GTE_ENABLE_EDITOR is OFF in this build)";
#endif
    break;
}
case EngineCommandKind::LoadScene: {
#if GTE_ENABLE_EDITOR
    const std::filesystem::path path = request.loadScene.path.empty()
        ? DefaultScenePath()
        : std::filesystem::path(request.loadScene.path);
    result.loadScene.success = LoadScene(game, renderer, path);
    result.loadScene.resolvedPath = path.string();
    if (!result.loadScene.success) {
        result.loadScene.errorMessage =
            "failed to load scene file - it may not exist, or failed to parse (see engine log)";
    }
#else
    result.loadScene.editorAvailable = false;
    result.loadScene.errorMessage =
        "scene save/load requires the Editor module (GTE_ENABLE_EDITOR is OFF in this build)";
#endif
    break;
}
```

`#include <filesystem>` at the top of this file if not already present.

### 3.5 — `src/Network/NetworkRoutes.h`/`.cpp`: parse + response-building

```cpp
// Parsed, VALIDATED result of a POST /save_scene or POST /load_scene
// request body: {"path": "..."} (optional). `valid == false` means
// `errorMessage` explains why. Validation: if "path" is present, it must
// be a JSON STRING (may be empty - an explicitly empty string is treated
// identically to the key being absent entirely: "use the default path").
// Any OTHER JSON type for "path" (number/bool/object/array) is a
// validation failure: "path must be a string". An entirely EMPTY request
// body (not even valid JSON, e.g. a genuinely empty POST) is NOT an error
// here - both endpoints treat a body that fails to parse as JSON at all
// (or parses to something other than an object) the SAME as "path was
// simply omitted" (path = ""), UNLIKE every other POST route in this file
// - this is the ONE deliberate exception, because both endpoints'
// single-optional-field-only shape makes "no body at all" a completely
// reasonable, common, valid request (e.g. a caller just wants "save/load
// the default scene"), not a malformed one.
struct ParsedScenePathRequest {
    bool valid = false;
    std::string errorMessage;
    std::string path; // "" means "use the default path".
};
ParsedScenePathRequest ParseScenePathRequest(const std::string& jsonBody);

// Success shape: {"success":true,"resolved_path":"<resolvedPath>"}
// Failure shape: identical to BuildGenericErrorResponseJson() below.
std::string BuildScenePathResponseJson(bool success, const std::string& errorMessage, const std::string& resolvedPath);
```

Implement `ParseScenePathRequest()` to NOT fail on unparsable/empty JSON
(per the doc comment above — this is genuinely different from every other
`ParseXxxRequest()` in this file, call this out with an inline comment at
the top of the function body so a future reader doesn't "fix" it to match
the others). `BuildScenePathResponseJson()` follows the exact same
`if (!success) return BuildGenericErrorResponseJson(errorMessage);` early-
return shape `BuildSetEntityTrsResponseJson()` already established.

### 3.6 — `src/Network/NetworkServer.cpp`: route registration

Inside `RegisterRoutes()`, alongside the other `EngineCommandBridge`-backed
POST routes:

```cpp
server.Post("/save_scene", [commandBridge](const httplib::Request& req, httplib::Response& res) {
    const ParsedScenePathRequest parsed = ParseScenePathRequest(req.body);
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
    request.kind = EngineCommandKind::SaveScene;
    request.saveScene.path = parsed.path;

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

    const SaveSceneOutcome& outcome = submit.result->saveScene;
    if (!outcome.editorAvailable) {
        res.status = 503;
        res.set_content(BuildGenericErrorResponseJson(outcome.errorMessage), "application/json");
        return;
    }
    res.status = outcome.success ? 200 : 500;
    res.set_content(BuildScenePathResponseJson(outcome.success, outcome.errorMessage, outcome.resolvedPath), "application/json");
});

server.Post("/load_scene", [commandBridge](const httplib::Request& req, httplib::Response& res) {
    // Mirrors /save_scene exactly, substituting EngineCommandKind::LoadScene,
    // request.loadScene.path, and submit.result->loadScene. A failed load
    // (missing/malformed file - see LoadScene()'s own doc comment) is a 400
    // (a caller-actionable "that scene file wasn't found or was invalid"),
    // NOT a 500 (which is reserved for save's own I/O-failure case, an
    // environment problem rather than a bad request) - this ONE status-code
    // difference between the two routes' otherwise-identical shape must be
    // implemented deliberately, not copy-pasted identically.
});
```

Double-check the exact bind-site plumbing: `RegisterRoutes()`'s own
signature and `NetworkServer`'s constructor already receive `commandBridge`
— no NEW bridge parameter is needed for this phase (reuses the existing
`EngineCommandBridge`, exactly like `/instantiate_asset` did for
`stl-parser-2` — see PHASE0's Locked Design Decision context). Confirm this
by re-reading `NetworkServer.cpp`'s constructor before editing.

### 3.7 — `docs/conventions/networking.md` update

Add a new bullet (after the existing `/instantiate_asset` bullet) describing
`POST /save_scene`/`POST /load_scene` in the same voice/detail level as the
existing bullets — status codes (`200`/`400`/`503`/`504`, plus the `500` vs
`400` distinction from 3.6), the optional `path` field's exact semantics
(3.5's doc comment), and a cross-reference to
`task_manager/scene-serialization-2/PHASE0_MASTER_STRATEGY.md`. Also update
the SAME doc's existing "JSON parsing now exists via a vendored
nlohmann/json" bullet (search for that exact phrase) to note that, as of
this campaign, `nlohmann::json` is ALSO the on-disk scene file format, not
only a network-parsing exception — see PHASE0's Locked Design Decision #1
for the exact wording to use.

## Definition of Done

- [ ] `EngineCommandResults.h`: `SaveSceneOutcome`/`LoadSceneOutcome` added.
- [ ] `EngineCommandBridge.h`: `SaveScene`/`LoadScene` kinds + command
      structs + result/request fields added.
- [ ] `Editor/SceneIO.h/.cpp`: explicit-path overloads added; zero-argument
      overloads unchanged in behavior (Ctrl+S/Ctrl+O still work identically).
- [ ] `EngineCommandDispatch.cpp`: both new `case` branches, correctly
      `#if GTE_ENABLE_EDITOR`-gated.
- [ ] `NetworkRoutes.h/.cpp`: `ParseScenePathRequest()`/
      `BuildScenePathResponseJson()` added and unit-tested (new/extended
      `tests/Network/NetworkRoutesTests.cpp` cases — empty body, `{}`,
      `{"path":"C:\\some\\path.gtscene"}`, `{"path":123}` rejected, malformed
      JSON treated as "no path" per 3.5's deliberate exception).
- [ ] `NetworkServer.cpp`: both routes registered, with the `500`-vs-`400`
      distinction implemented correctly for Save vs Load failures.
- [ ] `docs/conventions/networking.md` updated.
- [ ] Manual end-to-end check (via `gte_send_request` or a raw HTTP POST):
      start the engine, `POST /save_scene` with an empty body, confirm
      `200` + a real `resolved_path`; `POST /load_scene` the same way;
      confirm a `GTE_ENABLE_EDITOR=OFF` build (if buildable in this
      environment) returns `503` from both routes instead of failing to
      compile or crashing.
- [ ] Fast compile check passes for BOTH `GTE_ENABLE_EDITOR=ON` and, if
      practical to verify locally, `=OFF`.
- [ ] `PHASE5_COMPLETION_REPORT.md` written, `git add`/`git commit`.
