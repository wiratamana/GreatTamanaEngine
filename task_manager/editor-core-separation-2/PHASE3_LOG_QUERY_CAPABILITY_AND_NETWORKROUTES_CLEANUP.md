# PHASE3 — `ILogQueryCapability`: Closing Defect C

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it in full first, especially
Locked Design Decisions #2 and #3 and Defect C in Step 2. Also read
`src/Editor/EditorSceneIOCapability.h/.cpp` in full before starting — it is
the exact, proven template this phase mirrors.

## Step 1: The Goal (Where are we going?)

Today, `src/Network/NetworkServer.cpp` (compiled into `gte_core`) directly
`#include`s `"../Editor/Logger.h"` and calls five real, `gte_editor`-only
static methods (`Logger::Query()`, `Logger::Clear()`, `Logger::EntryCount()`,
`Logger::IsEnabled()`, `Logger::LatestEntryId()`) from its
`GET /get_logs`/`POST /clear_logs` route handlers. `src/Network/NetworkRoutes.cpp`
ALSO `#include`s `"../Editor/Logger.h"`, purely to read the compile-time
constant `Logger::kCapacity` when clamping `GET /get_logs`'s `limit` query
parameter. Both are real, live violations of "`gte_core` never includes
anything under `src/Editor/` except `EditorLayer.h`".

The goal: introduce `ILogQueryCapability` (mirroring `ISceneIOCapability`
exactly), wire a real implementation in through `NetworkServer`'s
constructor exactly the way every other bridge already is, and relocate
`Logger::kCapacity`'s VALUE into `src/Core/Logging.h` so `NetworkRoutes.cpp`
never needs `Editor/Logger.h` at all.

**IMPORTANT, CONFIRMED consequence of the `NetworkServer`-constructor
approach that Step 2/Step 3.6 below exist specifically to handle**: making
`GET /get_logs`/`POST /clear_logs` depend on a nullable, DEFAULTED
(`nullptr` by default) constructor pointer means every EXISTING caller that
constructs a bare `Network::NetworkServer server;` with no arguments now
gets a `503` from both routes instead of real logger-backed behavior. One
real, pre-existing test file does EXACTLY that and asserts on real `200`
responses today — `tests/Network/LogEndpointsEndToEndTests.cpp` — see Step
2 item 10 and Step 3.6 below. Do not skip this file; it WILL start failing
the moment Step 3.4's null-check lands, and per `AGENTS.md`'s testability
rules a newly-failing test is a real regression to fix in the SAME change,
not something to discover only at PHASE5's own final `ctest` pass.

## Step 2: The Situation (Where are we now?)

Re-run `search_in_dir(src, "Logger::")` and `search_in_dir(src,
"#include \"../Editor/Logger.h\"")` yourself before editing, to reconfirm
every call site below. Also run `search_in_dir(tests, "get_logs")` and
`search_in_dir(tests, "clear_logs")` to reconfirm item 10 below.

Files that change:
1. **`src/Core/Logging.h`** — gains a new
   `inline constexpr std::size_t kLogCapacity = 2000;` (re-confirm `2000`
   is still the real, current value of `Logger::kCapacity` in
   `src/Editor/Logger.h` before hardcoding it here — copy the real value,
   do not assume this document's number is still current).
2. **`src/Editor/Logger.h`** — `static constexpr std::size_t kCapacity = 2000;`
   becomes `static constexpr std::size_t kCapacity = kLogCapacity;` (needs
   `#include "../Core/Logging.h"`, which this file already has).
3. **`src/Core/EditorCapabilities.h`** — gains a new `ILogQueryCapability`
   interface, declared alongside `ISceneIOCapability`. Needs
   `#include "Logging.h"` (for `LogEntry`/`LogQueryFilter`, both already
   `gte_core`-owned types per `editor-core-separation-1`'s own PHASE3).
4. **`src/Editor/EditorLogQueryCapability.h` — NEW FILE.** Mirrors
   `EditorSceneIOCapability.h`'s exact shape (declaration-only, zero
   dependency on `Logger` itself in the header — only the `.cpp` needs
   `Editor/Logger.h`).
5. **`src/Editor/EditorLogQueryCapability.cpp` — NEW FILE.** The real
   implementation, delegating to `Logger::Query()`/`Clear()`/`EntryCount()`/
   `IsEnabled()`/`LatestEntryId()`.
6. **`src/Network/NetworkServer.h`** — gains a SIXTH defaulted constructor
   pointer parameter, `ILogQueryCapability* logQueryCapability = nullptr`,
   and a matching new private member `ILogQueryCapability*
   m_logQueryCapability = nullptr;`. Needs a forward declaration
   `namespace gte { class ILogQueryCapability; }` (mirroring every other
   bridge's own forward-declared pointer, e.g. `FrameCaptureBridge`).
7. **`src/Network/NetworkServer.cpp`** — drops
   `#include "../Editor/Logger.h"` entirely. `RegisterRoutes()`'s own
   signature gains a `ILogQueryCapability* logQueryCapability` parameter,
   threaded through from `NetworkServer`'s constructor exactly like every
   other bridge pointer already is. `GET /get_logs`/`POST /clear_logs`'s
   lambdas capture `logQueryCapability` and call through it instead of
   calling `Logger::` directly, with a null-check (see Step 3.3 for the
   exact response contract).
8. **`src/Network/NetworkRoutes.h`/`NetworkRoutes.cpp`** — `NetworkRoutes.cpp`
   drops `#include "../Editor/Logger.h"` entirely; its one use of
   `Logger::kCapacity` becomes `kLogCapacity` (from `Core/Logging.h`, already
   included via whatever this file already needs for `GTE_LOG_*`/`LogEntry`
   — confirm `Core/Logging.h` is already `#include`d here, add it if not).
9. **`src/Editor/EditorHost.cpp`** — gains a `static EditorLogQueryCapability
   s_editorLogQueryCapability;` (mirroring the existing
   `static EditorSceneIOCapability s_editorSceneIOCapability;` right above
   it) and passes `&s_editorLogQueryCapability` as the sixth argument to
   `m_networkServer`'s constructor call in its own member-initializer list.
   Needs `#include "EditorLogQueryCapability.h"`.
10. **`tests/Network/LogEndpointsEndToEndTests.cpp`** — this file ALREADY
    EXISTS (logger-1 campaign, PHASE5) and constructs a bare, no-argument
    `Network::NetworkServer server;`/`std::make_unique<Network::NetworkServer>()`
    in its `LogEndpointsEndToEndTest::SetUp()`, then asserts real `200`
    responses with real, Logger-backed JSON bodies from BOTH `GET /get_logs`
    and `POST /clear_logs` across ten `TEST_F` cases. Once Step 3.4's
    null-check lands, a capability-less `NetworkServer` makes BOTH routes
    return `503` unconditionally — every one of those ten `TEST_F` bodies
    will start failing (a real, confirmed regression, not a hypothetical
    one) unless this file is updated in the SAME phase. See Step 3.6 below
    for the exact fix (wire a real, member `EditorLogQueryCapability` into
    the fixture's own `NetworkServer` construction) plus a new,
    ADDITIONAL null-capability test mirroring this codebase's own existing
    `ActivateTabEndpointNullBridgeTests`/`EngineCommandEndpointsNoBridgeTests`
    convention (`tests/Network/ActivateTabEndpointEndToEndTests.cpp`/
    `EngineCommandEndpointsEndToEndTests.cpp`) for "what a nullptr bridge/
    capability does" coverage.

Files that do NOT change: every OTHER call site of `Logger::Query()`/
`Clear()`/etc. inside `gte_editor`-tier code (e.g.
`src/Editor/LogPanelData.cpp`, `src/Editor/EditorHost.cpp`'s own
`Logger::SetCurrentFrame()` call) — those are legitimate, direct,
same-tier uses of the real `Logger` class and are completely unaffected;
this phase ONLY fixes the `gte_core`-tier `NetworkServer.cpp`/
`NetworkRoutes.cpp` call sites. `tests/Network/NetworkRoutesTests.cpp` also
does NOT change — its own `GetLogsEndToEndTests`/`ClearLogsEndToEndTests`/
`ParseGetLogsQueryTests` cases call `NetworkRoutes.h`'s pure
`ParseGetLogsQuery()`/`BuildGetLogsResponseJson()`/`BuildClearLogsResponseJson()`
functions and the real `Logger` class DIRECTLY, never through a
`NetworkServer`/HTTP round trip, so they never observe the new capability
pointer at all — confirm this yourself by re-reading that file's own
`#include "Editor/Logger.h"` doc comment before assuming otherwise.

## Step 3: The Plan — exact steps

### Step 3.1 — `Core/Logging.h` and `Editor/Logger.h`

Re-read both files in full first. Add `kLogCapacity` to `Logging.h` (near
wherever `LogLevel`/`LogEntry`/`LogQueryFilter` already live, with a doc
comment explaining it exists so `NetworkRoutes.cpp` — a `gte_core`-tier file
— can clamp its own `limit` query parameter without needing to `#include`
the `gte_editor`-only `Logger` class just for one compile-time constant).
Change `Logger::kCapacity`'s definition in `Logger.h` to reference it.
Confirm every EXISTING call site of `Logger::kCapacity` elsewhere in the
codebase (e.g. inside `Logger.cpp` itself, ring-buffer eviction logic) still
compiles unchanged — it should, since the VALUE and the qualified name
`Logger::kCapacity` are both unchanged, only its definition's right-hand side
changed.

### Step 3.2 — `ILogQueryCapability`

In `src/Core/EditorCapabilities.h`, add (mirroring `ISceneIOCapability`'s own
doc-comment style and method shape):

```cpp
// editor-core-separation-2 campaign, PHASE3 - closes the real, pre-existing
// gte_core -> gte_editor-only-symbol dependency editor-core-separation-1
// left open (NetworkServer.cpp calling Logger::Query()/Clear()/EntryCount()/
// IsEnabled()/LatestEntryId() directly - see that campaign's own
// CAMPAIGN_COMPLETION_REPORT.md, "What remains genuinely open", option (b)).
// Mirrors ISceneIOCapability exactly: gte_core-tier code (NetworkServer.cpp)
// holds only a nullable pointer to this interface, asking a plain runtime
// null-check instead of a compile-time #if - a future Player host that
// never registers a real implementation gets a safe "logging unavailable"
// answer for free.
class ILogQueryCapability {
public:
    virtual ~ILogQueryCapability() = default;

    virtual std::vector<LogEntry> Query(const LogQueryFilter& filter) = 0;
    virtual void Clear() = 0;
    virtual std::size_t EntryCount() const = 0;
    virtual bool IsEnabled() const = 0;
    virtual std::uint64_t LatestEntryId() const = 0;
};
```

Cross-check `Logger::Query()`/`Clear()`/`EntryCount()`/`IsEnabled()`/
`LatestEntryId()`'s REAL, current signatures in `Logger.h` before finalizing
this (in particular, `Logger::IsEnabled()` is `static constexpr` returning
`true` unconditionally today — the interface method should be a plain
`virtual bool IsEnabled() const = 0;`, and the REAL implementation
(`EditorLogQueryCapability::IsEnabled()`) simply returns
`Logger::IsEnabled()`'s value; a `nullptr` capability pointer at the
`NetworkServer.cpp` call site is what represents "this build has no logger
at all", not a `false` return from this method — do not conflate the two).

### Step 3.3 — `EditorLogQueryCapability`

Create `src/Editor/EditorLogQueryCapability.h`/`.cpp`, copying
`EditorSceneIOCapability.h`/`.cpp`'s exact shape:

```cpp
// EditorLogQueryCapability.h
#pragma once
#include "../Core/EditorCapabilities.h"

namespace gte {
class EditorLogQueryCapability : public ILogQueryCapability {
public:
    std::vector<LogEntry> Query(const LogQueryFilter& filter) override;
    void Clear() override;
    std::size_t EntryCount() const override;
    bool IsEnabled() const override;
    std::uint64_t LatestEntryId() const override;
};
}
```
```cpp
// EditorLogQueryCapability.cpp
#include "EditorLogQueryCapability.h"
#include "Logger.h"

namespace gte {
std::vector<LogEntry> EditorLogQueryCapability::Query(const LogQueryFilter& filter) { return Logger::Query(filter); }
void EditorLogQueryCapability::Clear() { Logger::Clear(); }
std::size_t EditorLogQueryCapability::EntryCount() const { return Logger::EntryCount(); }
bool EditorLogQueryCapability::IsEnabled() const { return Logger::IsEnabled(); }
std::uint64_t EditorLogQueryCapability::LatestEntryId() const { return Logger::LatestEntryId(); }
}
```

Add both new files to `gte_editor`'s `target_sources()` list in the root
`CMakeLists.txt` (right next to `EditorSceneIOCapability.h/.cpp`).

### Step 3.4 — `NetworkServer.h`/`.cpp` wiring

Add the sixth constructor parameter/member exactly mirroring
`assetImportCommandBridge`'s own existing doc-comment pattern (a "SIXTH
defaulted, non-owning pointer, appended AFTER `assetImportCommandBridge` so
every existing call site keeps compiling unchanged" — this is CRITICAL:
`tests/Network/NetworkServerTests.cpp` has multiple no-argument
`NetworkServer server;` constructions that must keep compiling unchanged,
exactly like every prior bridge addition already preserved). Thread it
through `RegisterRoutes()`'s own parameter list the same way every other
bridge already is.

For `GET /get_logs`/`POST /clear_logs`'s lambdas: replace
`Logger::Query(parsed.filter)` with a null-checked call, e.g.:

```cpp
server.Get("/get_logs", [logQueryCapability](const httplib::Request& req, httplib::Response& res) {
    const ParsedGetLogsQuery parsed = ParseGetLogsQuery(...);
    if (!parsed.valid) { /* unchanged 400 branch */ }
    if (logQueryCapability == nullptr) {
        res.status = 503;
        res.set_content(BuildGenericErrorResponseJson("log query capability not available"), "application/json");
        return;
    }
    const std::vector<LogEntry> entries = logQueryCapability->Query(parsed.filter);
    res.set_content(
        BuildGetLogsResponseJson(entries, logQueryCapability->IsEnabled(), logQueryCapability->LatestEntryId()),
        "application/json");
});

server.Post("/clear_logs", [logQueryCapability](const httplib::Request&, httplib::Response& res) {
    if (logQueryCapability == nullptr) {
        res.status = 503;
        res.set_content(BuildGenericErrorResponseJson("log query capability not available"), "application/json");
        return;
    }
    const std::size_t clearedCount = logQueryCapability->EntryCount();
    logQueryCapability->Clear();
    res.set_content(BuildClearLogsResponseJson(clearedCount), "application/json");
});
```

**Ask yourself (and call `ask_questions` if genuinely unsure, do not just
guess)**: is a 503 the right status code for "no log capability wired up"
here, matching every other bridge's existing "nullptr → 503" convention in
this exact file? This strategy believes yes (consistent with
`/get_texture`/`/instantiate_primitive`/etc.'s own identical convention
already in this file) — but confirm by re-reading this file's own existing
503 conventions in full before finalizing.

### Step 3.5 — `EditorHost.cpp` wiring

Add `#include "EditorLogQueryCapability.h"`. Add the static instance and
pass its address as the constructor's sixth argument, mirroring the EXACT
existing `s_editorSceneIOCapability` precedent's OWN "function-local
`static`" mechanism (this class is pure delegation with no state of its
own — re-read `EditorHost.cpp`'s own existing comment on
`s_editorSceneIOCapability` to match its exact wording style for the new
one). Note that `ISceneIOCapability`'s own wiring destination differs from
this one's (it is stored on an `EditorHost`-owned `m_sceneIOCapability`
member and threaded into `ExecuteEngineCommand()` by hand, since scene
save/load is dispatched through `EngineCommandBridge`, not `NetworkServer`
directly) — do NOT add a parallel `m_logQueryCapability` member to
`EditorHost` itself; `ILogQueryCapability`'s own wiring destination is
`NetworkServer`'s constructor argument list directly (mirroring
`m_captureBridge`/`m_commandBridge`/etc.'s own "pass `&member` straight into
`m_networkServer`'s member-initializer list" shape instead), since
`GET /get_logs`/`POST /clear_logs` are real `NetworkServer`-owned routes,
never dispatched through `EngineCommandBridge`.

### Step 3.6 — Fix the pre-existing `tests/Network/LogEndpointsEndToEndTests.cpp` regression

Re-read this file in full first (it already exists — logger-1 campaign,
PHASE5). Its `LogEndpointsEndToEndTest` fixture's `SetUp()` currently does
`m_server = std::make_unique<Network::NetworkServer>();` (every constructor
argument defaulted, including the new, sixth `logQueryCapability`) and every
one of its ten `TEST_F` bodies asserts real `200` responses with real
Logger-backed JSON. Once Step 3.4 lands, this construction makes BOTH
`GET /get_logs` and `POST /clear_logs` respond `503` — every one of those
ten tests will fail. Fix this in the SAME phase, not later:

1. Add `#include "Editor/EditorLogQueryCapability.h"` to this test file.
2. Add a new fixture member, `EditorLogQueryCapability m_logQueryCapability;`
   (declared BEFORE `m_server` in the class body, so it is already
   constructed by the time `SetUp()` builds `m_server` — mirrors this
   engine's own established "declare a dependency before the thing that
   needs its address" member-ordering convention, e.g. `EditorHost.h`'s own
   bridge-before-`m_networkServer` ordering).
3. Change `SetUp()`'s construction to pass `&m_logQueryCapability` as the
   sixth constructor argument (the first five stay `nullptr`, e.g.
   `std::make_unique<Network::NetworkServer>(nullptr, nullptr, nullptr,
   nullptr, nullptr, &m_logQueryCapability)`), so every existing `TEST_F`
   body keeps observing real, Logger-backed `200` behavior completely
   unchanged.
4. Add ONE new, separate `TEST` (not `TEST_F` — no real capability wired,
   mirroring `ActivateTabEndpointNullBridgeTests`/
   `EngineCommandEndpointsNoBridgeTests`'s own exact convention in sibling
   files under this same directory) confirming the null-capability-degrade
   path this phase's own Step 3.4 introduces, e.g.
   `TEST(LogEndpointsNullCapabilityTests, GetLogsAndClearLogsReturn503WhenCapabilityIsNull)`:
   construct a bare, zero-argument `Network::NetworkServer server;`, `Start(0)`,
   confirm `GET /get_logs` and `POST /clear_logs` both return `503` with a
   `{"success":false, ...}` body, matching this file's sibling
   `ActivateTabEndpointNullBridgeTests`'s own assertion shape exactly.
5. Re-confirm `tests/CMakeLists.txt` already lists
   `Network/LogEndpointsEndToEndTests.cpp` (it does — no `CMakeLists.txt`
   change needed for this file, only its own source edits).

## Step 4: Compile check

1. `cmake --build build --target gte_core` — confirms `Core/Logging.h`/
   `Core/EditorCapabilities.h` changes compile standalone.
2. `cmake --build build --target gte_editor` — confirms
   `EditorLogQueryCapability`/`EditorHost.cpp` wiring compiles.
3. `cmake --build build --target GreatTamanaEditor` (still via the
   not-yet-removed `$<LINK_GROUP:RESCAN,...>` — PHASE4 removes it) — full
   executable compile+link check.
4. `cmake --build build --target GreatTamanaEngineTests`, then run ONLY
   this phase's own relevant tests with a filter (never the full suite —
   that stays PHASE5's own job per `PHASE0`'s Universal Rule 4), e.g.
   `build\GreatTamanaEngineTests.exe --gtest_filter=LogEndpointsEndToEndTest.*:LogEndpointsNullCapabilityTests.*`
   — every one of these must pass. This is the ONE mechanical check that
   actually proves Step 3.6's fix works; do not skip it and rely on
   PHASE5's own later full run to discover a regression this phase itself
   introduced.
5. `run_app_background` the built executable. `gte_send_request`:
   - `GET /get_logs?limit=20` — confirm `200`, real entries, an
     `"enabled":true` field (or whatever `BuildGetLogsResponseJson()`'s real
     field name is — re-read `NetworkRoutes.cpp` to confirm the exact JSON
     shape before asserting on it).
   - `POST /clear_logs` (empty body) — confirm `200` and a real
     `clearedCount`.
   - `GET /get_logs?limit=20` again — confirm the entries are now different
     (post-clear) — proving the capability wiring genuinely reaches the
     real `Logger` singleton end-to-end, not a stub.
   `stop_app_background` when done.

## Step 5: Wrap-up

- Write `PHASE3_COMPLETION_REPORT.md`: files touched (including the test
  file fix from Step 3.6), the interface's final shape, the compile-check
  result, the targeted `ctest`/`gtest_filter` result from Step 4.4, and the
  `GET /get_logs`/`POST /clear_logs` smoke-test result (paste real JSON).
- `git_add` + `git_commit`.
- Call `ask_questions` for any genuine ambiguity (e.g. the 503-vs-something-
  else status code question in Step 3.4, if you are not confident reading
  the existing file's convention resolves it).
