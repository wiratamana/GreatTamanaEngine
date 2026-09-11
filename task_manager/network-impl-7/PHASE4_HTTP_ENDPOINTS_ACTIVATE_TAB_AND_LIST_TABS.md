# PHASE4 — HTTP Endpoints: `GET /activate_tab` and `GET /list_tabs`

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it first (especially its own
"Endpoint contract" section — this phase must produce EXACTLY that
contract, status codes included).
**Also read:** `PHASE1_COMPLETION_REPORT.md`, `PHASE2_COMPLETION_REPORT.md`,
`PHASE3_COMPLETION_REPORT.md` before starting.

## Step 1: The Goal

Add the actual HTTP surface: two new, pure, Tier-1-tested functions in
`NetworkRoutes.h/.cpp` (query parsing + response-JSON building), and two new
route registrations in `NetworkServer.cpp` that call into Phase 1's
`EditorUiCommandBridge` (for `/activate_tab`) or need no bridge at all (for
`/list_tabs`, per PHASE0's own locked decision that it is answerable from
compile-time-fixed data alone).

## Step 2: The Situation / The Problem

- `src/Network/NetworkRoutes.h/.cpp` already establishes the exact pattern
  to follow: every route's REQUEST parsing and RESPONSE building lives here
  as small, pure, `httplib`-independent functions (Tier-1-tested in
  `tests/Network/NetworkRoutesTests.cpp`); `NetworkServer.cpp` is only ever
  a thin wiring layer that calls these functions and forwards results into
  `httplib::Response::set_content()` — it must never compose response text
  itself (see `NetworkRoutes.h`'s own file header comment).
- The closest existing precedent for a `GET` endpoint with ONE required
  query parameter, validated with a clean error message, THEN a bridge
  round-trip, is `GET /get_texture` (`ParseGetTextureQuery()` +
  `RegisterGetTextureRoute()` in `NetworkServer.cpp`) — read both in full
  before starting; this phase's own `ParseActivateTabQuery()` /
  route-registration should mirror that shape closely.
- The closest existing precedent for a `GET` endpoint needing NO bridge at
  all (pure function of build configuration/already-known data) is
  `GET /list_textures`'s OWN sibling shape is close but not identical (it
  DOES use the bridge, since its data is genuinely dynamic/per-session) —
  `GET /list_tabs` is actually simpler than any existing precedent: its
  data (the panel name catalog) is fixed at COMPILE time (see Phase 1's
  `EditorPanelCatalog.h`), so its route handler needs no bridge, no
  `Application`-owned state, nothing beyond the catalog header itself.

## Step 3: The Plan

### 3.1 — `src/Network/NetworkRoutes.h`: new declarations

Add `#include "../Editor/EditorPanelCatalog.h"` to this file's includes
(safe and correct regardless of `GTE_ENABLE_EDITOR` — see Phase 1's own
Section 3.1 for why this specific header compiles in every configuration).

Add the following new declarations (placed after the existing
`network-impl-6`-era declarations, at the end of the file, before the
closing `} // namespace gte::Network`):

```cpp
// --- network-impl-7 campaign - GET /activate_tab and GET /list_tabs.
// Every function below stays PURE - no httplib/socket/thread/Registry/Game/
// Renderer/ImGui dependency of any kind, exactly like everything above -
// EditorPanelCatalog.h (included above) is a plain, compile-time-fixed data
// header with the same "safe to depend on from anywhere" property as
// <cstdint>/<string>, not an Editor/ImGui dependency in the sense this
// file's own header comment warns against.

// Parsed, validated GET /activate_tab query parameters. `valid == false`
// means `errorMessage` explains exactly why (a 400 response); `notFound ==
// true` (only meaningful when `valid == true`) means the name did not
// match anything in EditorPanelCatalog.h's known panel list (a 404
// response, NOT a 400 - the request itself was well-formed, the NAME it
// asked about just isn't one this engine knows about - see
// PHASE0_MASTER_STRATEGY.md's own locked endpoint contract for the exact
// status-code mapping this distinction feeds into).
struct ParsedActivateTabQuery {
    bool valid = false;
    std::string errorMessage;
    bool notFound = false;
    std::string tabName;
};

// Validation rules (checked in this order):
//   1. `nameParam` must be non-empty - otherwise "missing or empty required
//      query parameter: name" (valid = false).
//   2. `nameParam` must exactly (case-sensitive) match one entry of
//      gte::kKnownEditorPanelNames (EditorPanelCatalog.h's
//      IsKnownEditorPanelName()) - otherwise valid = true, notFound = true,
//      tabName = nameParam (the caller/route handler is expected to build a
//      404 response quoting this name - see BuildActivateTabResponseJson()
//      below).
//   3. Otherwise valid = true, notFound = false, tabName = nameParam.
ParsedActivateTabQuery ParseActivateTabQuery(const std::string& nameParam);

// Builds GET /activate_tab's response body for every outcome EXCEPT the
// 400 (malformed request) and 503 (bridge unavailable) cases, which reuse
// BuildGenericErrorResponseJson() directly at the route-handler call site,
// same split convention BuildSetEntityTrsResponseJson()'s own doc comment
// already documents for its own endpoint.
//   - success == true  -> {"success":true,"activated_tab":"<tabName>"}
//   - success == false && tabExists == false (a KNOWN panel name, but no
//     live window with that name existed this session yet) ->
//     {"success":false,"error":"panel '<tabName>' has no live window yet
//     this session - try again after the Editor has rendered at least one
//     frame"} (409 - see the route handler, Section 3.3)
std::string BuildActivateTabResponseJson(bool success, bool tabExists, const std::string& tabName);

// Builds the 404 response body for a `name` that is well-formed but not a
// known panel (ParsedActivateTabQuery::notFound == true):
// {"success":false,"error":"unknown tab name '<tabName>' - see GET
// /list_tabs for the currently known names"}
std::string BuildUnknownTabNameResponseJson(const std::string& tabName);

// Builds GET /list_tabs's entire response body - needs no request/query
// input at all, since the panel catalog is fixed at compile time (see
// EditorPanelCatalog.h): {"tabs":["Hierarchy","Inspector","Scene","Game",
// "Memory","Profiler","Render Graph","Jobs","Atmosphere"]} (plus "Project"
// appended at the end when GTE_ENABLE_PROJECT_PANEL is ON - reads directly
// from gte::kKnownEditorPanelNames, so this list can never drift out of
// sync with what GET /activate_tab itself accepts).
std::string BuildListTabsResponseJson();
```

### 3.2 — `src/Network/NetworkRoutes.cpp`: implementations

Implement each function using `nlohmann::json` for correct string escaping
(a tab name is caller-controlled input for the 404/409 messages, so it must
be escaped exactly like every existing entity/parent NAME already is in
this file — see e.g. `BuildInstantiatePrimitiveResponseJson()`'s own use of
`nlohmann::json` for exactly this reason, never hand-formatted string
concatenation).

```cpp
ParsedActivateTabQuery ParseActivateTabQuery(const std::string& nameParam)
{
    ParsedActivateTabQuery result;
    if (nameParam.empty()) {
        result.valid = false;
        result.errorMessage = "missing or empty required query parameter: name";
        return result;
    }
    result.valid = true;
    result.tabName = nameParam;
    result.notFound = !IsKnownEditorPanelName(nameParam);
    return result;
}

std::string BuildActivateTabResponseJson(bool success, bool tabExists, const std::string& tabName)
{
    nlohmann::json body;
    body["success"] = success;
    if (success) {
        body["activated_tab"] = tabName;
    } else {
        body["error"] = "panel '" + tabName +
            "' has no live window yet this session - try again after the Editor has rendered at least one frame";
    }
    return body.dump();
}

std::string BuildUnknownTabNameResponseJson(const std::string& tabName)
{
    nlohmann::json body;
    body["success"] = false;
    body["error"] = "unknown tab name '" + tabName + "' - see GET /list_tabs for the currently known names";
    return body.dump();
}

std::string BuildListTabsResponseJson()
{
    nlohmann::json body;
    body["tabs"] = nlohmann::json::array();
    for (const char* name : kKnownEditorPanelNames) {
        body["tabs"].push_back(name);
    }
    return body.dump();
}
```

(Match this file's existing exact `nlohmann::json` usage conventions -
check how `BuildGenericErrorResponseJson()`/`BuildInstantiatePrimitiveResponseJson()`
are actually implemented in `NetworkRoutes.cpp` today and mirror that
style precisely, including whichever `#include <nlohmann/json.hpp>`
already exists at the top of the file - do not add a second, redundant
include.)

### 3.3 — `src/Network/NetworkServer.cpp`: register the two routes

Add `#include "../Application/EditorUiCommandBridge.h"` to this file's
includes (alongside the existing `EngineCommandBridge.h`/
`FrameCaptureBridge.h` includes).

Update `RegisterRoutes()`'s signature to accept the new pointer:

```cpp
void RegisterRoutes(httplib::Server& server, FrameCaptureBridge* captureBridge,
    EngineCommandBridge* commandBridge, EditorUiCommandBridge* uiCommandBridge)
```

and update its ONE call site (inside `NetworkServer`'s constructor) to pass
`m_uiCommandBridge` through, mirroring how `m_commandBridge` is already
threaded there.

Add the new `GET /list_tabs` registration (needs no bridge parameter at
all — place it near `RegisterListTexturesRoute()`'s own call site for
proximity, though it does not need its own named helper function given how
small it is; a direct `server.Get(...)` lambda is fine, matching
`/http_hello_world`'s own minimal shape):

```cpp
    // network-impl-7 campaign - GET /list_tabs. Needs NO bridge at all -
    // the panel catalog is fixed at compile time (see EditorPanelCatalog.h)
    // - this is the SIMPLEST route in this whole file: a pure function of
    // build configuration, zero runtime/thread/bridge dependency.
    server.Get("/list_tabs", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(BuildListTabsResponseJson(), "application/json");
    });
```

Add the new `GET /activate_tab` registration (needs the new bridge, mirrors
`/get_texture`'s own "parse first, THEN check bridge availability, THEN
submit-and-wait, THEN map the outcome to a status code" shape):

```cpp
    // network-impl-7 campaign - GET /activate_tab?name=<PanelName>. See
    // PHASE0_MASTER_STRATEGY.md's own locked endpoint contract for the
    // exact status-code mapping implemented below.
    server.Get("/activate_tab", [uiCommandBridge](const httplib::Request& req, httplib::Response& res) {
        const ParsedActivateTabQuery parsed = ParseActivateTabQuery(req.get_param_value("name"));
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (parsed.notFound) {
            res.status = 404;
            res.set_content(BuildUnknownTabNameResponseJson(parsed.tabName), "application/json");
            return;
        }
        if (uiCommandBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("editor UI command bridge not available"), "application/json");
            return;
        }

        EditorUiCommandRequest request;
        request.kind = EditorUiCommandKind::ActivateTab;
        request.activateTab.tabName = parsed.tabName;

        const EditorUiCommandBridge::SubmitResult submit = uiCommandBridge->SubmitAndWait(request);
        if (submit.alreadyPending) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("another editor UI command is already in progress"), "application/json");
            return;
        }
        if (submit.timedOut) {
            res.status = 504;
            res.set_content(BuildGenericErrorResponseJson("editor UI command timed out"), "application/json");
            return;
        }

        const ActivateTabOutcome& outcome = submit.result->activateTab;
        res.status = outcome.success ? 200 : 409;
        res.set_content(BuildActivateTabResponseJson(outcome.success, outcome.tabExists, parsed.tabName), "application/json");
    });
```

**IMPORTANT ordering detail**: `parsed.notFound` (404) MUST be checked
BEFORE the `uiCommandBridge == nullptr` (503) check — an unknown tab name
is always a `404` regardless of whether the bridge exists at all (the
request would fail for the SAME reason, "this name means nothing to this
engine," even if the bridge were available) — do not swap this order, and
add a regression test for exactly this ordering in Phase 5 (a
`NetworkServerTests.cpp`/end-to-end test with `uiCommandBridge == nullptr`
AND an unknown `name` must still observe `404`, never `503`).

### 3.4 — Tests (Tier 1, add in this SAME phase)

Add to `tests/Network/NetworkRoutesTests.cpp` (mirroring the existing
`ParseGetTextureQuery`/`BuildListTexturesResponseJson` test style/naming
exactly):

- `ParseActivateTabQueryRejectsEmptyName`
- `ParseActivateTabQueryAcceptsKnownName` (e.g. `"Profiler"`) — `valid ==
  true`, `notFound == false`.
- `ParseActivateTabQueryFlagsUnknownNameAsNotFoundNotInvalid` (e.g.
  `"NotARealTab"`) — `valid == true`, `notFound == true` (this is the
  precise behavior that lets the route handler distinguish 400 from 404 —
  assert this distinction explicitly, it is the whole point of this
  function's design).
- `ParseActivateTabQueryIsCaseSensitive` (e.g. `"profiler"` lowercase is
  `notFound == true`, matching `IsKnownEditorPanelName()`'s own documented
  case-sensitivity).
- `BuildActivateTabResponseJsonSuccessShape` / `...FailureShape` — parse the
  returned string back with `nlohmann::json::parse()` and assert the exact
  keys/values, mirroring how existing tests in this file already verify
  JSON shape (do not do brittle raw-string comparison against the whole
  body — check field-by-field, same convention already used for the
  existing response-builder tests in this file).
- `BuildUnknownTabNameResponseJsonShape`
- `BuildListTabsResponseJsonContainsEveryKnownPanelName` — parse the result,
  assert `"tabs"` is a JSON array containing every literal from
  `gte::kKnownEditorPanelNames` (loop-driven, so it never needs updating
  when a panel is added/removed later) and nothing else.

Add to `tests/Network/NetworkServerTests.cpp` (or a new dedicated
end-to-end file — see Phase 5 for why the true end-to-end tests belong in
their own file, mirroring `EngineCommandEndpointsEndToEndTests.cpp`): at
minimum, a same-file unit test constructing a bare `NetworkServer` with
`uiCommandBridge == nullptr` (the existing no-argument-friendly
constructor pattern) and confirming `GET /activate_tab?name=Profiler`
still responds (a real HTTP round-trip against `127.0.0.1:<BoundPort()>`,
mirroring this file's own existing pattern for testing a bridge-`nullptr`
503 path) with status `503`, AND that
`GET /activate_tab?name=NotARealTab` against that SAME nullptr-bridge
server responds `404` (proving the ordering rule from Section 3.3 above is
actually correct, not just documented). Register any new test file(s) in
`tests/CMakeLists.txt`'s `GTE_TEST_SOURCES` list, alongside the existing
`Network/*Tests.cpp` entries.

Full end-to-end tests (a real `EditorUiCommandBridge` instance,
`SubmitAndWait()` from a background thread simulating the network route,
`FulfillCommand()` from the "main thread" simulating `Application::Run()`'s
new drain step) are Phase 5's job, mirroring
`EngineCommandEndpointsEndToEndTests.cpp`'s own existing shape closely —
this phase's own tests only need to prove the PURE parsing/response-
building functions and the nullptr-bridge degrade-gracefully paths are
correct; Phase 5 proves the full request/response round-trip including a
genuinely fulfilled (non-nullptr-bridge) case.

## Step 4: Verification for this phase

- `cmake --build build` succeeds.
- New tests (Section 3.4) pass.
- Manual smoke test: `run_app_background` the built engine, then
  `gte_send_request` (or `run_shell` with `curl`) against
  `http://127.0.0.1:8080/list_tabs` and confirm the JSON body lists every
  expected panel name; then against
  `http://127.0.0.1:8080/activate_tab?name=Profiler` and confirm a `200`
  JSON success body, and against `.../activate_tab?name=NotARealTab` and
  confirm a `404`. `stop_app_background` afterward.
- Write `PHASE4_COMPLETION_REPORT.md` in this same folder, `git add`/
  `git commit` the changes + report together.
