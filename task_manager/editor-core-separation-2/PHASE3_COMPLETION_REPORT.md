# PHASE3 — COMPLETION REPORT: `ILogQueryCapability` (Closing Defect C)

**Parent:** `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE3_LOG_QUERY_CAPABILITY_AND_NETWORKROUTES_CLEANUP.md` exactly, including
its Step 2 item 10 / Step 3.6 mandatory fix to the pre-existing
`tests/Network/LogEndpointsEndToEndTests.cpp` file.

## Step 0: Pre-existing completion reports

Read `PHASE1_COMPLETION_REPORT.md` and `PHASE2_COMPLETION_REPORT.md` in full
before starting. Neither reported any deviation relevant to this phase's own
work (PHASE1: pure `EditorPanelCatalog.h` relocation, zero ambiguity. PHASE2:
`IFrameDebuggerCaptureRecorder` interface, one real self-introduced
`namespace gte { #include ... }` nesting bug, found and fixed via its own
mandatory compile check). No carry-forward clue changed this phase's plan.

## Step 1: Re-confirmation against real, current source

Before editing, re-ran every `search_in_dir` query the phase doc itself
specifies:
- `search_in_dir(src, "Logger::")` → 36 matches in 10 files — confirmed the
  two real violations (`NetworkServer.cpp`'s five direct `Logger::` calls,
  `NetworkRoutes.cpp`'s one `Logger::kCapacity` use) plus every other,
  legitimate same-tier `gte_editor` use (`Logger.cpp`, `LogPanel.cpp`,
  `EditorHost.cpp`'s `SetCurrentFrame()`).
- `search_in_dir(src, "Editor/Logger.h")` → confirmed both real `#include`s
  (`NetworkRoutes.cpp` line 12, `NetworkServer.cpp` line 25, before edits).
- `search_in_dir(tests, "get_logs")` / `search_in_dir(tests, "clear_logs")` →
  confirmed `tests/Network/LogEndpointsEndToEndTests.cpp`'s exact, real shape
  (ten `TEST_F` bodies, bare `NetworkServer server;`/
  `std::make_unique<NetworkServer>()` construction) and reconfirmed
  `tests/Network/NetworkRoutesTests.cpp` never touches `NetworkServer` at all
  (calls `ParseGetLogsQuery()`/`Logger::` directly) — genuinely unaffected, as
  the phase doc predicted.
- Confirmed `Logger::kCapacity`'s real, current value is `2000` in
  `src/Editor/Logger.h` before hardcoding `kLogCapacity = 2000` in
  `Core/Logging.h`.
- Confirmed `Logger::IsEnabled()`'s real, current signature
  (`static constexpr bool IsEnabled() noexcept { return true; }`) before
  finalizing `ILogQueryCapability::IsEnabled()`'s own shape.
- Confirmed `ActivateTabEndpointNullBridgeTests`'s exact convention
  (`tests/Network/ActivateTabEndpointEndToEndTests.cpp`, plain `TEST`, bare
  `Network::NetworkServer server;`, assert `503` + `{"success":false,...}`)
  before writing the new `LogEndpointsNullCapabilityTests` case.

No line-number drift was found anywhere — every location the phase doc
predicted matched the real, current source exactly.

## Files actually touched

1. **`src/Core/Logging.h`** — gained
   `inline constexpr std::size_t kLogCapacity = 2000;`, right after
   `LogQueryFilter`'s own closing `};`.
2. **`src/Editor/Logger.h`** — `static constexpr std::size_t kCapacity = 2000;`
   → `static constexpr std::size_t kCapacity = kLogCapacity;` (unchanged
   value, unchanged qualified name/call sites everywhere else).
3. **`src/Core/EditorCapabilities.h`** — gained `#include "Logging.h"` (plus
   `<cstddef>`/`<vector>`, needed for the new interface's own signatures) and
   the new `ILogQueryCapability` interface, declared right after
   `ISceneIOCapability`.
4. **`src/Editor/EditorLogQueryCapability.h` — NEW FILE.** Declaration-only,
   mirroring `EditorSceneIOCapability.h`'s exact shape.
5. **`src/Editor/EditorLogQueryCapability.cpp` — NEW FILE.** Delegates to
   `Logger::Query()`/`Clear()`/`EntryCount()`/`IsEnabled()`/`LatestEntryId()`.
6. **`src/Network/NetworkServer.h`** — new forward declaration
   `namespace gte { class ILogQueryCapability; }`; constructor gained a
   SIXTH defaulted parameter, `ILogQueryCapability* logQueryCapability =
   nullptr`; new private member `ILogQueryCapability* m_logQueryCapability =
   nullptr;`.
7. **`src/Network/NetworkServer.cpp`** — dropped
   `#include "../Editor/Logger.h"` entirely; added
   `#include "../Core/EditorCapabilities.h"`; `RegisterRoutes()`'s own
   signature/call site and the constructor both gained the sixth parameter,
   threaded through exactly like every other bridge pointer; `GET /get_logs`/
   `POST /clear_logs` now call through `logQueryCapability` with a
   `nullptr → 503` guard, mirroring every other bridge's identical
   convention already in this file.
8. **`src/Network/NetworkRoutes.cpp`** — dropped
   `#include "../Editor/Logger.h"` entirely; `ParseGetLogsQuery()`'s clamp
   now reads `kLogCapacity` (from `Core/Logging.h`, already `#include`d via
   `NetworkRoutes.h`) instead of `Logger::kCapacity`.
9. **`src/Network/NetworkRoutes.h`** — doc-comment-only updates (two
   locations) describing the new `kLogCapacity`/capability-bridge reality
   instead of the old direct-`Logger::`-call description. No functional
   change — this file's own pure functions never touched `Logger` directly
   to begin with, confirmed unaffected.
10. **`src/Editor/EditorHost.cpp`** — added
    `#include "EditorLogQueryCapability.h"`; added a namespace-scope static
    `EditorLogQueryCapability s_editorLogQueryCapability;` (see "Deviation"
    below for why this is namespace-scope rather than literally
    function-local-inside-the-constructor-body); passed
    `&s_editorLogQueryCapability` as `m_networkServer`'s sixth
    member-initializer-list argument.
11. **`CMakeLists.txt`** — added `src/Editor/EditorLogQueryCapability.h`/
    `.cpp` to `gte_editor`'s `target_sources()` list, right after
    `EditorSceneIOCapability.h/.cpp`.
12. **`tests/Network/LogEndpointsEndToEndTests.cpp`** (Step 3.6's own
    mandatory fix) —
    - Added `#include "Editor/EditorLogQueryCapability.h"`.
    - Added fixture member `EditorLogQueryCapability m_logQueryCapability;`,
      declared BEFORE `m_server`.
    - `SetUp()` now constructs
      `std::make_unique<Network::NetworkServer>(nullptr, nullptr, nullptr,
      nullptr, nullptr, &m_logQueryCapability)` instead of the old bare,
      all-defaulted constructor — every existing `TEST_F` body's real,
      Logger-backed `200` assertions are completely unchanged.
    - Added ONE new, separate `TEST` (not `TEST_F`),
      `LogEndpointsNullCapabilityTests.GetLogsAndClearLogsReturn503WhenCapabilityIsNull`,
      constructing a bare `Network::NetworkServer server;` and confirming
      both `GET /get_logs` and `POST /clear_logs` return `503` with a
      `{"success":false,...}` body, mirroring
      `ActivateTabEndpointNullBridgeTests`'s exact convention.
    - `tests/CMakeLists.txt` reconfirmed unchanged (already listed this file).

Files that did NOT change, confirmed per the phase doc's own explicit list:
`src/Editor/LogPanelData.cpp`, `src/Editor/EditorHost.cpp`'s own
`Logger::SetCurrentFrame()` call site (still direct, legitimate same-tier
use), `tests/Network/NetworkRoutesTests.cpp` (still calls `Logger::`/pure
`NetworkRoutes.h` functions directly, never through `NetworkServer`).

## Deviation from the plan (real, necessary, documented)

Step 3.5 of the phase doc says to add the log-query-capability static
instance "mirroring the EXACT existing `s_editorSceneIOCapability`
precedent's OWN 'function-local `static`' mechanism" — i.e. declared INSIDE
`EditorHost`'s constructor BODY, exactly like `s_editorSceneIOCapability` is
today. Attempting this literally does not compile: `m_networkServer` is
constructed in `EditorHost`'s member-INITIALIZER LIST, which runs BEFORE the
constructor's body — a static variable declared inside the body is not yet
in lexical scope at that point, so its address cannot be taken there.

`s_editorSceneIOCapability` itself avoids this exact problem because its
address is only ever used LATER, inside the body (assigned to
`m_sceneIOCapability`, itself only read from `Run()`, long after the body
has executed) — never inside `EditorHost`'s own member-initializer list.
`ILogQueryCapability`'s wiring destination is different (per the phase doc's
own Step 3.5 note): it must reach `NetworkServer`'s constructor argument
list directly, which means it is needed *during* the initializer list, not
after.

**Resolution**: declared `s_editorLogQueryCapability` as a plain
NAMESPACE-scope static (in the existing anonymous namespace at the top of
`EditorHost.cpp`, alongside `IsBgraFormat()`/`ToDebugTextureColorFormatName()`)
instead of a function-local static inside the constructor body. This is safe
specifically because `EditorLogQueryCapability` holds zero data members and
its (implicit) constructor touches no other global — there is no
static-initialization-order risk to guard against, unlike a general
Meyers-singleton scenario. Documented in full, in-place, in `EditorHost.cpp`
itself (see the new comment directly above `s_editorLogQueryCapability`'s
declaration) so a future maintainer understands exactly why this one bridge
uses a different static-storage shape than `s_editorSceneIOCapability`
immediately below it in the same file.

This is the only deviation from the phase doc's plan. Everything else
(interface shape, capability-implementation shape, `NetworkServer`
constructor/route wiring, `Logging.h`/`Logger.h` relocation, the test-file
fix) was implemented exactly as specified, with no other ambiguity — no
`ask_questions` call was needed (the 503-status-code question the doc itself
flagged in Step 3.4 was confidently resolved by re-reading
`NetworkServer.cpp`'s own dozens of pre-existing, identical `nullptr → 503`
precedents in the same file, e.g. `/get_texture`, `/instantiate_primitive`,
`/activate_tab`).

## The interface's final shape (pasted in full)

`src/Core/EditorCapabilities.h`, appended after `ISceneIOCapability`:

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
//
// `IsEnabled()` mirrors Logger::IsEnabled()'s own real, current signature
// (a `static constexpr bool` returning `true` unconditionally today) - a
// `nullptr` ILogQueryCapability* at the NetworkServer.cpp call site is what
// represents "this build has no logger at all", NOT a `false` return from
// this method; the two must never be conflated.
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

`src/Editor/EditorLogQueryCapability.h` (the real implementation's
declaration):

```cpp
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
} // namespace gte
```

`src/Editor/EditorLogQueryCapability.cpp` (bodies, each one line, delegating
to the real `Logger` class):

```cpp
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

This matches the phase doc's own required shape exactly (no signature
drift).

## Compile-check result (Step 4)

1. `cmake --build build --target gte_core` — **SUCCESS**, zero errors (9
   objects compiled including `Network/NetworkRoutes.cpp.obj`,
   `Network/NetworkServer.cpp.obj`, `Core/Core.cpp.obj`;
   `libgte_core.a` linked). Confirms `Core/Logging.h`/
   `Core/EditorCapabilities.h` changes compile standalone, with zero
   `Editor/Logger.h` dependency anywhere in `gte_core` anymore.
2. `cmake --build build --target gte_editor` — **SUCCESS**, zero errors (9
   objects compiled, including the two new
   `Editor/EditorLogQueryCapability.cpp.obj` and the updated
   `Editor/EditorHost.cpp.obj`; `libgte_editor.a` linked).
3. `cmake --build build --target GreatTamanaEditor` — **SUCCESS**, full
   executable link completed (still via the not-yet-removed
   `$<LINK_GROUP:RESCAN,...>` — PHASE4's own job to remove).
4. `cmake --build build --target GreatTamanaEngineTests` — **SUCCESS**, 28
   objects compiled (including the updated
   `Network/LogEndpointsEndToEndTests.cpp.obj`), full link completed.

   Targeted gtest filter result:
   ```
   build\tests\GreatTamanaEngineTests.exe --gtest_filter=LogEndpointsEndToEndTest.*:LogEndpointsNullCapabilityTests.*

   [==========] Running 11 tests from 2 test suites.
   [----------] 10 tests from LogEndpointsEndToEndTest
   [       OK ] LogEndpointsEndToEndTest.BasicFetchReturnsRealLoggedEntriesRoundTripped (10 ms)
   [       OK ] LogEndpointsEndToEndTest.SinceIdCursorReturnsOnlyLaterEntries (8 ms)
   [       OK ] LogEndpointsEndToEndTest.MinLevelFilterIsCaseInsensitiveAndInclusiveUpward (11 ms)
   [       OK ] LogEndpointsEndToEndTest.CategoryFilterMatchesExactlyAndUnknownCategoryIsEmptyNotAnError (21 ms)
   [       OK ] LogEndpointsEndToEndTest.KeywordFilterIsCaseInsensitiveSubstringMatch (28 ms)
   [       OK ] LogEndpointsEndToEndTest.FrameMinMaxRangeIsInclusive (17 ms)
   [       OK ] LogEndpointsEndToEndTest.LimitReturnsNewestNAndAcceptsButClampsAnOverlargeLimit (35 ms)
   [       OK ] LogEndpointsEndToEndTest.MalformedQueryParametersReturn400WithGenericErrorShape (41 ms)
   [       OK ] LogEndpointsEndToEndTest.ClearLogsEmptiesBufferAndIdNeverResets (9 ms)
   [       OK ] LogEndpointsEndToEndTest.CombinedFiltersComposeAsLogicalAnd (5 ms)
   [----------] 10 tests from LogEndpointsEndToEndTest (193 ms total)
   [----------] 1 test from LogEndpointsNullCapabilityTests
   [       OK ] LogEndpointsNullCapabilityTests.GetLogsAndClearLogsReturn503WhenCapabilityIsNull (8 ms)
   [----------] 1 test from LogEndpointsNullCapabilityTests (8 ms total)
   [==========] 11 tests from 2 test suites ran. (202 ms total)
   [  PASSED  ] 11 tests.
   ```
   All 11 tests pass — this is the one mechanical proof that Step 3.6's fix
   genuinely works (the ten pre-existing tests keep observing real,
   Logger-backed behavior via the newly-wired real capability, and the one
   new test proves the null-capability-degrades-to-503 path this phase's own
   null-check introduces).

## Runtime smoke-test result (Step 4.5)

Launched `build/GreatTamanaEditor.exe` via `run_app_background` (PID 8620).

1. `GET /get_logs?limit=20` → `200`:
   ```json
   {"count":2,"entries":[{"category":"Network","frame":0,"id":1,"level":"Info","message":"listening on 127.0.0.1:8080","timestamp_seconds":1e-06},{"category":"EditorHost","frame":0,"id":2,"level":"Info","message":"EditorHost constructed: SdlContext -> Window -> Core -> CreateEditorLayer() -> Core::SetEditorLayerHook() all completed, with a genuinely non-null IEditorLayer*, every automation bridge attached, and NetworkServer started.","timestamp_seconds":2.8e-05}],"latest_id":2,"logging_enabled":true}
   ```
2. `POST /clear_logs` (empty body) → `200`:
   ```json
   {"cleared_count":2,"success":true}
   ```
3. `GET /get_logs?limit=20` again → `200`:
   ```json
   {"count":0,"entries":[],"latest_id":2,"logging_enabled":true}
   ```

`count` went from `2` → `0` after the clear, while `latest_id` stayed `2`
(never reset by `Clear()`, exactly as documented) — real, observable,
end-to-end proof that `GET /get_logs`/`POST /clear_logs` now reach the real
`Logger` singleton through the brand-new `ILogQueryCapability` bridge
(`EditorHost`'s `s_editorLogQueryCapability` → `NetworkServer`'s sixth
constructor argument → `RegisterRoutes()`'s lambdas), not a stub, and that
`EditorHost`'s own wiring (Step 3.5) is genuinely reached at real runtime,
not just at compile time.

`stop_app_background(pid: 8620)` called afterward; process confirmed
terminated.

## Scope honesty

This phase's own scope (Defect C: the `NetworkServer.cpp`/`NetworkRoutes.cpp`
`Editor/Logger.h` violations) is fully closed. `EditorPanelCatalog.h`
(PHASE1) and the Frame Debugger capture recorder (PHASE2) were already
closed by prior phases and untouched here. The `$<LINK_GROUP:RESCAN,...>`
workaround is still in place — its removal, and the permanent player-link
probe, remain PHASE4's own job, unaffected by this phase's work (this
phase's own compile checks above all still used the RESCAN workaround, per
`PHASE0_MASTER_STRATEGY.md`'s own Universal Rule 4).

No part of this phase was delegated to a sub-task (`delegate_task` was never
called), per PHASE0's own Universal Rule 8. No genuine architectural
ambiguity beyond the one documented, necessary deviation above was
encountered — `ask_questions` was not needed.

## Git

Both the code changes (including the `tests/Network/LogEndpointsEndToEndTests.cpp`
fix) and this report are committed together, in one commit, on
`feature/editor-core-separation` (branch never switched, per Universal Rule
2).
