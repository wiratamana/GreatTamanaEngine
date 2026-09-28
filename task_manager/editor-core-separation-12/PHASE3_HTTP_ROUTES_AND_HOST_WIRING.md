# editor-core-separation-12 — PHASE3: HTTP Routes + NetworkServer/EditorHost
Wiring

Parent: `PHASE0_MASTER_STRATEGY.md` — read it first.
Prior phase report: read PHASE2's own completion report before starting —
it deliberately leaves `EditorHost.cpp` line 408 broken (a missing-argument
compile error); this phase's Step 1 is fixing that same call site.

Depends on: PHASE1, PHASE2 (needs `EditorHotReloadDebugCapability` and the
extended `ExecuteEngineCommand()` signature).
Blocks: PHASE4 (needs a fully-linking `GreatTamanaEditor.exe`).

---

## Step 1: The Goal

Register all 7 new HTTP routes, wire the new capability into
`NetworkServer`/`EditorHost` exactly like every prior capability/bridge, fix
the one broken call site PHASE2 intentionally left, and get the whole
engine linking and running again.

## Step 2: The Situation

`src/Network/NetworkServer.h`'s constructor takes 7 defaulted, non-owning
pointers today, each documented as "appended AFTER the previous one" —
confirmed current text, lines 140-146 declare the constructor,
lines 199-237 declare the matching private members. `RegisterRoutes()` is a
free function inside the anonymous namespace of `NetworkServer.cpp`, called
once from the constructor body (confirmed lines 1240-1241) with the exact
same parameter list, in the exact same order, as the constructor itself.

`src/Network/NetworkRoutes.h`/`.cpp` hold every route's PURE parse/build
helper functions (confirmed pattern: `ParsedGetLogsQuery
ParseGetLogsQuery(...)`, `std::string BuildGetLogsResponseJson(...)`) —
`NetworkServer.cpp` itself only ever calls these plus the relevant
bridge/capability, never building JSON text inline. This phase follows that
same split exactly: every parse/build function goes in
`NetworkRoutes.h/.cpp` (Tier-1-testable, add matching tests under
`tests/Network/NetworkRoutesTests.cpp` — confirmed this file already
exists and already follows a test-per-function pattern), and
`NetworkServer.cpp`'s own route lambdas stay thin orchestration exactly
like `GET /get_logs`'s existing lambda (lines 1179-1198).

IMPORTANT header-dependency note, confirmed by actually reading
`src/Network/NetworkRoutes.h` in full before writing anything below:
that file does **not** currently `#include "../Core/EditorCapabilities.h"`
anywhere (it only includes `../Core/EditorPanelRegistry.h`, `../Core/
Logging.h`, and `../Renderer/RenderGraph/RenderGraphMetadata.h`). The new
functions this phase adds there take `const IHotReloadDebugCapability::
Status&` and `const IHotReloadDebugCapability::LedgerEntry&` BY VALUE-ish
reference — both are NESTED types of `IHotReloadDebugCapability`
(`src/Core/EditorCapabilities.h`, PHASE1), and naming a nested type requires
the OUTER class's COMPLETE definition, not merely a forward declaration of
the class. Without adding this include, `NetworkRoutes.h` fails to compile
the moment these two new declarations are added — this is Step 3.2's very
first sub-step below, done BEFORE writing the declarations themselves, for
exactly this reason.

`src/Editor/EditorHost.h`'s constructor member-initializer list already
threads 7 bridge/capability addresses into `m_networkServer`'s own
constructor call (confirmed lines 169-170) — this phase adds the 8th.
`EditorHost.cpp` line 408 is the ONE call site `ExecuteEngineCommand()` has
— PHASE2 changed that function's signature; this phase fixes this one call.

## Step 3: The Plan

### 3.1 — Fix the one broken call site FIRST

`src/Editor/EditorHost.cpp`, line 408, change:
```cpp
const EngineCommandResult result = ExecuteEngineCommand(m_game, m_renderer, m_sceneIOCapability, *request);
```
to:
```cpp
const EngineCommandResult result =
    ExecuteEngineCommand(m_game, m_renderer, m_sceneIOCapability, &s_editorHotReloadDebugCapability, *request);
```
(`s_editorHotReloadDebugCapability` is the new namespace-scope static this
phase adds in Step 3.6 below — do this step AFTER 3.6, or the build will
still be red in between; either order is fine as long as both land in the
same commit.) Do this step first in your own working session anyway, so you
can immediately confirm PHASE2's own new code was correct via a real
compile error round-trip, before writing any of the new route code below.

### 3.2 — `src/Network/NetworkRoutes.h` — new pure functions

FIRST, add the new include this file needs (see Step 2's own note above for
exactly why this is required, not optional) — add it next to the file's
existing `#include "../Core/EditorPanelRegistry.h"` line:
```cpp
#include "../Core/EditorPanelRegistry.h"
#include "../Core/EditorCapabilities.h" // IHotReloadDebugCapability::Status/LedgerEntry - editor-core-separation-12 campaign, PHASE3.
#include "../Core/Logging.h"
```
`IHotReloadDebugCapability` lives in `namespace gte` (not `gte::Network`) —
exactly like `EditorPanelRegistry` above it, it is used UNQUALIFIED from
inside `namespace gte::Network` below (nested-namespace lookup already
finds it, the same way `EditorPanelRegistry::Instance()` is already called
unqualified throughout this file today) - no `gte::` prefix needed anywhere
in the declarations below.

THEN add these declarations, grouped under a new comment block mirroring
the existing `GET /get_logs` block's own style (append after the last
existing declaration in the file):

```cpp
// --- editor-core-separation-12 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 1) - 7 new routes: 5 OBSERVE (status/ledger/loaded_assemblies/
// component_types/scene_snapshot), 2 TRIGGER (compile_only/hot_reload).

// GET /project_assembly/hot_reload/status - no query params, no parsing
// needed. Builds:
//   {"phase":"...","project_name":"...","cycle_id":N,
//    "phase_elapsed_ms":N,"last_outcome":"...","last_error_message":"..."}
std::string BuildHotReloadStatusResponseJson(const IHotReloadDebugCapability::Status& status);

// Shared by GET /project_assembly/debug/ledger, POST
// /project_assembly/debug/compile_only, and POST /project_assembly/hot_reload
// - all three take a REQUIRED `name` query parameter. `valid == false`
// means `errorMessage` explains why (missing/empty `name`).
struct ParsedProjectNameQuery {
    bool valid = false;
    std::string errorMessage;
    std::string projectName;
};
ParsedProjectNameQuery ParseProjectNameQuery(const std::string& nameParam);

// GET /project_assembly/debug/ledger?name=<X> - builds:
//   {"project_name":"...","render_pass_names":[...],"panel_names":[...],
//    "component_type_names":[...]}
std::string BuildLedgerEntryResponseJson(
    const std::string& projectName, const IHotReloadDebugCapability::LedgerEntry& entry);

// GET /project_assembly/debug/loaded_assemblies - builds:
//   {"dll_file_names":[...]}
std::string BuildLoadedAssembliesResponseJson(const std::vector<std::string>& dllFileNames);

// GET /project_assembly/debug/component_types - builds:
//   {"type_names":[...]}
std::string BuildComponentTypeNamesResponseJson(const std::vector<std::string>& typeNames);

// GET /project_assembly/debug/scene_snapshot has NO dedicated build
// function - NetworkServer.cpp sets the raw GetSceneSnapshotOutcome::sceneJson
// string directly as the response body (see this feature's own BIG-STEP 1
// design doc, Section 5 - "the raw SerializeSceneDocument() JSON string,
// returned directly as the response body").

// POST /project_assembly/debug/compile_only?name=<X> - builds:
//   {"started":true} or {"started":false,"reason":"..."}
// (200 either way - "already building" is a normal, non-error outcome).
std::string BuildCompileOnlyTriggerResponseJson(bool started, const std::string& reason);
```

### 3.3 — `src/Network/NetworkRoutes.cpp` — implementations

Follow the existing file's own established JSON-building style: EVERY
`BuildXxxResponseJson()` function in this file (confirmed by reading
`NetworkRoutes.cpp` in full, e.g. `BuildGetLogsResponseJson()`'s real body)
constructs an `nlohmann::json` OBJECT via `body["key"] = value;` assignments
and returns `body.dump()` — the ONLY exception anywhere in this file is
`BuildCaptureJsonBody()`, which hand-formats a raw string because it predates
this file's `nlohmann::json` dependency and only ever carries integers/
base64 text (see that function's own doc comment for why that trick doesn't
generalize). Write all five new functions below using the SAME
`nlohmann::json` object-construction idiom `BuildGetLogsResponseJson()`
uses — never manual string concatenation, and never a second, different
JSON-library usage pattern.

`ParseProjectNameQuery()`:
```cpp
ParsedProjectNameQuery ParseProjectNameQuery(const std::string& nameParam)
{
    ParsedProjectNameQuery parsed;
    if (nameParam.empty()) {
        parsed.errorMessage = "'name' query parameter is required";
        return parsed;
    }
    parsed.projectName = nameParam;
    parsed.valid = true;
    return parsed;
}
```

### 3.4 — `src/Network/NetworkServer.h` — 8th constructor parameter

Forward-declare (mirroring the existing block at the top of the file):
```cpp
// Forward-declared for the same cheap-header reason as
// FrameCaptureBridge/.../RenderGraphControlCommandBridge above -
// editor-core-separation-12 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 1).
namespace gte { class IHotReloadDebugCapability; }
```

Add the 8th constructor parameter (append, never insert):
```cpp
    explicit NetworkServer(FrameCaptureBridge* captureBridge = nullptr,
        EngineCommandBridge* commandBridge = nullptr,
        EditorUiCommandBridge* uiCommandBridge = nullptr,
        FrameDebuggerCommandBridge* frameDebuggerCommandBridge = nullptr,
        AssetImportCommandBridge* assetImportCommandBridge = nullptr,
        ILogQueryCapability* logQueryCapability = nullptr,
        RenderGraphControlCommandBridge* renderGraphControlCommandBridge = nullptr,
        IHotReloadDebugCapability* hotReloadDebugCapability = nullptr);
```

This class's own constructor doc comment (immediately above the
constructor, confirmed current text) documents each parameter's addition
ordinally — "SECOND", "THIRD", ... up through `renderGraphControlCommandBridge`
being called out as the "SEVENTH defaulted, non-owning pointer" — every
single past campaign that added a parameter here extended this SAME running
comment rather than leaving it stale. Extend it one more time, appending an
"EIGHTH defaulted, non-owning pointer" paragraph mirroring the SEVENTH
one's exact shape:
```cpp
    // `hotReloadDebugCapability` (editor-core-separation-12 campaign,
    // Project Assembly Hot Reload plan, BIG-STEP 1) is an EIGHTH defaulted,
    // non-owning pointer, appended AFTER `renderGraphControlCommandBridge`
    // so every existing call site keeps compiling unchanged. Non-null in
    // production (EditorHost owns the real EditorHotReloadDebugCapability
    // and passes its address) - `nullptr` means "every GET/POST
    // /project_assembly/* route this campaign adds responds 503 rather
    // than crashing" - the exact same "nullptr degrades gracefully to a
    // 503, never a crash" contract every other bridge above already
    // documents.
```

Add the matching private member (append, after
`m_renderGraphControlCommandBridge`):
```cpp
    // Non-owning - same lifetime contract as m_captureBridge above
    // (editor-core-separation-12 campaign). Consulted by every
    // GET/POST /project_assembly/* route this campaign adds
    // (RegisterRoutes() below).
    IHotReloadDebugCapability* m_hotReloadDebugCapability = nullptr;
```

### 3.5 — `src/Network/NetworkServer.cpp` — constructor, `RegisterRoutes()`, and the 7 route lambdas

Update the anonymous-namespace `RegisterRoutes()` free function's own
parameter list to accept the new pointer (append, name it
`hotReloadDebugCapability` — every lambda below captures it by that exact
name), update its ONE caller (the constructor body) to pass
`m_hotReloadDebugCapability` through, and update the constructor's own
initializer list and parameter list to match `NetworkServer.h`'s new 8th
parameter — mirroring, line for line, exactly how
`renderGraphControlCommandBridge` was threaded through in
editor-core-separation-8's own PHASE5 (visible today as the 7th parameter
everywhere in this same file).

Add the 7 new route registrations at the END of `RegisterRoutes()`'s own
body (after the existing `POST /clear_logs` block, before its own closing
brace):

```cpp
    // --- editor-core-separation-12 campaign (Project Assembly Hot Reload
    // plan, BIG-STEP 1) - see that campaign's PHASE3 doc for the full
    // design. All 5 OBSERVE routes below (status/ledger/loaded_assemblies/
    // component_types) EXCEPT scene_snapshot bypass EngineCommandBridge
    // entirely (mirrors GET /get_logs's own "no bridge needed" shape) -
    // scene_snapshot is the ONE exception, routed through commandBridge
    // instead, because it touches the live ECS Registry (see
    // Core/EditorCapabilities.h's own IHotReloadDebugCapability doc comment
    // for why).

    server.Get("/project_assembly/hot_reload/status",
        [hotReloadDebugCapability](const httplib::Request&, httplib::Response& res) {
        if (hotReloadDebugCapability == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("hot reload debug capability not available"), "application/json");
            return;
        }
        res.set_content(BuildHotReloadStatusResponseJson(hotReloadDebugCapability->GetHotReloadStatus()), "application/json");
    });

    server.Get("/project_assembly/debug/ledger",
        [hotReloadDebugCapability](const httplib::Request& req, httplib::Response& res) {
        const ParsedProjectNameQuery parsed = ParseProjectNameQuery(req.get_param_value("name"));
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (hotReloadDebugCapability == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("hot reload debug capability not available"), "application/json");
            return;
        }
        res.set_content(
            BuildLedgerEntryResponseJson(parsed.projectName, hotReloadDebugCapability->GetLedgerEntry(parsed.projectName)),
            "application/json");
    });

    server.Get("/project_assembly/debug/loaded_assemblies",
        [hotReloadDebugCapability](const httplib::Request&, httplib::Response& res) {
        if (hotReloadDebugCapability == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("hot reload debug capability not available"), "application/json");
            return;
        }
        res.set_content(
            BuildLoadedAssembliesResponseJson(hotReloadDebugCapability->GetLoadedAssemblyFileNames()), "application/json");
    });

    server.Get("/project_assembly/debug/component_types",
        [hotReloadDebugCapability](const httplib::Request&, httplib::Response& res) {
        if (hotReloadDebugCapability == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("hot reload debug capability not available"), "application/json");
            return;
        }
        res.set_content(
            BuildComponentTypeNamesResponseJson(hotReloadDebugCapability->GetRegisteredComponentTypeNames()), "application/json");
    });

    // scene_snapshot is the ONE OBSERVE route routed through
    // EngineCommandBridge (see this campaign's PHASE0 doc, Correction 1) -
    // mirrors GET-via-POST-style /save_scene//load_scene's own
    // SubmitAndWait() shape exactly (lines ~1130-1165 above), even though
    // this route is itself a GET.
    server.Get("/project_assembly/debug/scene_snapshot",
        [commandBridge](const httplib::Request&, httplib::Response& res) {
        if (commandBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("engine command bridge not available"), "application/json");
            return;
        }
        EngineCommandRequest request;
        request.kind = EngineCommandKind::GetSceneSnapshot;
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
        const GetSceneSnapshotOutcome& outcome = submit.result->getSceneSnapshot;
        if (!outcome.editorAvailable) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson(outcome.errorMessage), "application/json");
            return;
        }
        if (!outcome.success) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(outcome.errorMessage), "application/json");
            return;
        }
        res.set_content(outcome.sceneJson, "application/json");
    });

    server.Post("/project_assembly/debug/compile_only",
        [hotReloadDebugCapability](const httplib::Request& req, httplib::Response& res) {
        const ParsedProjectNameQuery parsed = ParseProjectNameQuery(req.get_param_value("name"));
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (hotReloadDebugCapability == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("hot reload debug capability not available"), "application/json");
            return;
        }
        const bool started = hotReloadDebugCapability->TriggerCompileOnly(parsed.projectName);
        res.set_content(
            BuildCompileOnlyTriggerResponseJson(started, started ? "" : "a build for this project is already in progress"),
            "application/json");
    });

    // editor-core-separation-12 campaign, PHASE3 - the AGREED, STABLE route
    // contract for a future BIG-STEP 3 campaign. Currently ALWAYS answers
    // 501 - TriggerHotReload()'s own PHASE2 body always returns false, so
    // this handler's shape already correctly degrades once a future
    // campaign changes ONLY that method's body to sometimes return true -
    // this handler's own code will need a small follow-up then (a 200
    // success path), but its ROUTE/METHOD/QUERY-PARAM CONTRACT never
    // changes. The final `res.status = 500;` below is UNREACHABLE for this
    // whole campaign's lifetime (TriggerHotReload() never returns true
    // until a future BIG-STEP 3 campaign changes it) - it exists purely as
    // a defensive, explicit status code so this branch never silently
    // answers with httplib's default 200 alongside an "unexpected" error
    // body if that invariant is ever accidentally broken later.
    server.Post("/project_assembly/hot_reload",
        [hotReloadDebugCapability](const httplib::Request& req, httplib::Response& res) {
        const ParsedProjectNameQuery parsed = ParseProjectNameQuery(req.get_param_value("name"));
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (hotReloadDebugCapability == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("hot reload debug capability not available"), "application/json");
            return;
        }
        const bool started = hotReloadDebugCapability->TriggerHotReload(parsed.projectName);
        if (!started) {
            res.status = 501;
            res.set_content(
                BuildGenericErrorResponseJson("hot reload orchestrator not yet wired - see BIG-STEP 3"), "application/json");
            return;
        }
        res.status = 500;
        res.set_content(BuildGenericErrorResponseJson("unexpected: TriggerHotReload() reported success in a build with no real orchestrator"), "application/json");
    });
```

Confirm `BuildGenericErrorResponseJson()` is the exact existing helper name
(confirmed, used verbatim throughout `NetworkServer.cpp`'s existing routes,
e.g. line 1148) — reuse it, do not invent a second error-JSON shape.

### 3.6 — `src/Editor/EditorHost.cpp` — construct and wire the new capability

Add a new namespace-scope static, immediately after
`s_editorLogQueryCapability` (mirroring its own exact placement/reasoning
comment about needing to exist before the constructor's own
member-initializer list runs):

```cpp
// editor-core-separation-12 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 1) - the ONE real EditorHotReloadDebugCapability instance this
// engine ships, wired into NetworkServer's constructor below AND used
// directly by the ExecuteEngineCommand() call site (Run()'s own
// EngineCommandBridge servicing code) for GetSceneSnapshot. Same
// namespace-scope-static reasoning as s_editorLogQueryCapability
// immediately above.
EditorHotReloadDebugCapability s_editorHotReloadDebugCapability;
```

Add the matching `#include "EditorHotReloadDebugCapability.h"` near the top
of the file, alongside the existing `#include "EditorLogQueryCapability.h"`.

Update `m_networkServer`'s constructor call (the same block at lines
169-170) to pass the 8th argument, adding one more inline comment paragraph
immediately above it (mirroring the existing "sixth argument"/"seventh
argument" comments already there) documenting this as the eighth:
```cpp
    // editor-core-separation-12 campaign (Project Assembly Hot Reload plan,
    // BIG-STEP 1) - the eighth argument, &s_editorHotReloadDebugCapability,
    // so every GET/POST /project_assembly/* route can reach it.
    , m_networkServer(&m_captureBridge, &m_commandBridge, &m_uiCommandBridge, &m_frameDebuggerCommandBridge,
          &m_assetImportCommandBridge, &s_editorLogQueryCapability, &m_renderGraphControlCommandBridge,
          &s_editorHotReloadDebugCapability)
```

Apply the Step 3.1 fix (the `ExecuteEngineCommand()` call site) now that
`s_editorHotReloadDebugCapability` exists.

### 3.7 — New regression test file: `tests/Network/ProjectAssemblyHotReloadEndpointsEndToEndTests.cpp`

NOTE: this phase does NOT touch `CMakeLists.txt` at all — PHASE1 (its own
Section 3.8) and PHASE2 (its own Section 3.8) already registered every new
`.h/.cpp` pair this whole campaign creates, including
`src/Editor/EditorHotReloadDebugCapability.h/.cpp`, directly in the phase
that introduced it, per `PHASE0_MASTER_STRATEGY.md`'s own "PHASE1/PHASE2 add
their own new files there directly; do not defer this to PHASE3" rule. If
you find that file pair NOT already present in `CMakeLists.txt`'s
`gte_editor` block when you start this phase, that means PHASE2 deviated
from its own strategy doc — add it yourself and note the deviation in this
phase's own completion report, rather than silently reintroducing a second,
duplicate `CMakeLists.txt` entry for the exact same two files.

Every prior HTTP endpoint family this engine has ever shipped got its own
permanent, automated end-to-end test file exercising the real
`NetworkServer` wiring over a real loopback socket —
`tests/Network/LogEndpointsEndToEndTests.cpp` (bridge-free routes, a real
capability instance, plus a plain `NetworkServer server;` null-capability
503 test, `LogEndpointsNullCapabilityTests`), `tests/Network/
EngineCommandEndpointsEndToEndTests.cpp` (a `FakeEngineCommandStandIn`
background thread draining `EngineCommandBridge` instead of a real
`Game`/`Renderer`), `ActivateTabEndpointEndToEndTests.cpp`,
`ImportAssetEndpointEndToEndTests.cpp`, `InstantiateAssetEndpointEndToEndTests.cpp`.
PHASE4's own live `gte_send_request` pass is a valuable one-off smoke test,
but it is NOT a substitute for this: it never runs again after this
campaign closes, so a future, unrelated change that regresses e.g.
`ParseProjectNameQuery()`'s 400 path would have zero automated coverage.
Add this file now, following those two precedents:

- The 4 bridge-free OBSERVE routes that need only a capability
  (`hot_reload/status`, `debug/ledger`, `debug/loaded_assemblies`,
  `debug/component_types`) — construct a real `EditorHotReloadDebugCapability`
  instance (mirrors `LogEndpointsEndToEndTest`'s own
  `EditorLogQueryCapability m_logQueryCapability` member) and a real
  `NetworkServer` wired with its address as the 8th constructor argument;
  assert the real, honest-placeholder JSON shapes (empty lists for
  `ledger`/`loaded_assemblies`) and the real, non-empty `component_types`
  list (see PHASE4's own Step 3.2, item 5, for which type names must be
  present).
- `debug/scene_snapshot` needs a `FakeEngineCommandStandIn`-style background
  thread draining `EngineCommandBridge` (mirrors
  `EngineCommandEndpointsEndToEndTests.cpp`'s own precedent exactly) that
  answers `EngineCommandKind::GetSceneSnapshot` with a small, fixed, fake
  `GetSceneSnapshotOutcome` — this proves the bridge/route wiring, not
  `BuildSceneSnapshotJson()`'s own real body (that stays covered only by
  PHASE4's manual live smoke test, matching this campaign's own accepted
  Tier-2-adjacent scope for anything touching a real `Game&`).
- `compile_only`/`hot_reload` need only the `EditorHotReloadDebugCapability`
  instance above (no bridge) — assert the 400 (missing `name`), 200/
  `{"started":true}`, and 501 shapes.
- A second, plain `TEST` (not `TEST_F`) with a capability-less/bridge-less
  `NetworkServer server;` (mirrors `LogEndpointsNullCapabilityTests`)
  confirms every one of the 7 routes answers 503, not a crash, when nothing
  is wired up.

Register the new file in `tests/CMakeLists.txt`'s own hand-maintained list
(mirroring `Network/LogEndpointsEndToEndTests.cpp`'s own entry there).
Build and run just this one test binary/filter as this phase's own "fast
compile check" — a full `ctest` regression pass across the whole suite
stays PHASE4's job.

## Definition of Done — this phase only

- [ ] `src/Network/NetworkRoutes.h` compiles standalone with the new
      `#include "../Core/EditorCapabilities.h"` in place — a translation
      unit that `#include`s only `NetworkRoutes.h` builds cleanly (this is
      the one new dependency this phase's own new declarations require;
      confirm it BEFORE moving on to `NetworkRoutes.cpp`/`NetworkServer.cpp`,
      since every later step in this phase depends on this header actually
      compiling).
- [ ] `GreatTamanaEditor` links successfully (a full incremental build,
      not just a compile-check of individual translation units — this is
      the first phase where a genuine link is possible/meaningful, since
      PHASE2 deliberately left one call site broken).
- [ ] Every one of the 7 new routes is registered and reachable (a basic
      "does it 404 or does it respond" smoke check is enough for THIS
      phase — the full live behavioral verification, with a real running
      engine and `gte_send_request`, is PHASE4's job specifically, per this
      campaign's own workflow rule against doing that everywhere).
- [ ] `tests/Network/NetworkServerTests.cpp`'s existing no-argument
      `NetworkServer server;` constructions still compile unmodified
      (confirms the 8th parameter's default value keeps this
      backward-compatible, matching every prior bridge's own precedent).
- [ ] The new `tests/Network/ProjectAssemblyHotReloadEndpointsEndToEndTests.cpp`
      (Section 3.7) compiles and its own tests pass — running just this one
      test binary/filter is enough for this phase; a full `ctest` regression
      pass across the whole suite stays PHASE4's job.
