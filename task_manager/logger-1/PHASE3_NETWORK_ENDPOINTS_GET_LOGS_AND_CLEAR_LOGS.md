# PHASE3 — Network Endpoints: `GET /get_logs` and `POST /clear_logs`

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: `PHASE1_CORE_LOGGER_MODULE.md`,
`PHASE2_ENGINE_INTEGRATION_AND_FRAME_STAMPING.md` (read both completion
reports first).

**Use `ask_questions` for any genuine ambiguity this document doesn't already
resolve.** If this phase itself delegates any further sub-task, that
delegation prompt must repeat this same instruction.

---

## Step 1: The Goal (Where are we going?)

Expose the Phase 1 `Logger` over HTTP: `GET /get_logs`, supporting every
filter `PHASE0` locked (`since_id`, `min_level`, `category`, `keyword`,
`frame_min`/`frame_max`, `limit`), and `POST /clear_logs`. Both follow this
engine's existing `Network/NetworkRoutes.h` (pure logic) +
`Network/NetworkServer.cpp` (thin httplib wiring) split exactly, and both
call `Logger::Query()`/`Logger::Clear()` **directly**, with **no new
cross-thread bridge** — document this exception explicitly (Locked Design
Decision 14 in `PHASE0`).

## Step 2: The Situation (Where are we now?)

- `Network/NetworkRoutes.h` is `namespace gte::Network`, already
  `#include`s `"../Editor/EditorPanelCatalog.h"` (line 6) — this is the
  exact precedent for this phase's own new
  `#include "../Editor/Logger.h"` in the same file.
- Every existing request-parsing function in this file returns a small
  `Parsed*Query`/`Parsed*Request`-style struct with (at least) a `bool
  valid` and a `std::string errorMessage` — e.g.
  `ParseActivateTabQuery(const std::string& name)` returns a
  `ParsedActivateTabQuery{ valid, errorMessage, tabName, notFound }`. This
  phase's `ParseGetLogsQuery(...)` follows the identical shape.
- `Network/NetworkServer.cpp`'s `RegisterRoutes(...)` function is the one
  hand-written route table — `GET /list_tabs` is the closest existing
  precedent for "needs no bridge at all": a plain `server.Get("/list_tabs",
  [](const httplib::Request&, httplib::Response& res) { res.set_content(
  BuildListTabsResponseJson(), "application/json"); });` with no bridge
  pointer captured at all. `GET /activate_tab` is the closest precedent for
  "parse query params, validate, respond with the right status code" (see
  its exact status-code cascade: 400 for a bad/missing param, 404/409 for
  semantic failures, then the actual bridge call, 503/504 for bridge
  failures) — this phase's `GET /get_logs` mirrors the "parse + 400 on bad
  input" shape but has **no** bridge-related status codes at all (503/504
  never apply here, since there is no bridge — see Locked Design Decision
  14).
- JSON responses already use vendored `nlohmann::json` (see e.g.
  `BuildListTabsResponseJson()`'s own implementation in
  `NetworkRoutes.cpp` for the exact style/formatting to mirror:
  `nlohmann::json j; j["some_field"] = value; return j.dump();`).
- `AGENTS.md`'s existing `## Networking` section and
  `docs/conventions/networking.md` are both where every past network
  feature added its own bullet/subsection — this phase adds to both, plus a
  brand-new `## Logging` section in `AGENTS.md` (a short summary + link) and
  a brand-new `docs/conventions/logging.md` (the full write-up), following
  `docs/README.md`'s existing "## Conventions" index format (each entry is
  a `- **[Title](conventions/file.md)** — one-sentence summary.` bullet,
  inserted alphabetically-by-topic-ish among the existing ones — exact
  position doesn't matter, just add it as one more bullet in that list).

## Step 3: The Plan

### 3.1 `src/Network/NetworkRoutes.h` additions

Add near the top (alongside the existing `#include
"../Editor/EditorPanelCatalog.h"`):

```cpp
#include "../Editor/Logger.h"
```

Add (matching the file's existing doc-comment density):

```cpp
// logger-1 campaign - GET /get_logs' own parsed query parameters. Every
// httplib::Request::get_param_value(...) call returns "" for a missing
// param (this project's existing convention - see ParseActivateTabQuery())
// - an empty string for a given field below means "this filter was not
// supplied", exactly mirroring LogQueryFilter's own "empty/default means
// match everything" semantics (see Editor/Logger.h).
struct ParsedGetLogsQuery {
    // Defaults to false, matching this file's OWN existing convention for
    // every other Parsed*Query/Parsed*Request struct (e.g.
    // ParsedActivateTabQuery) - fail-closed by default, so a future field
    // added to this struct without updating every return path can never
    // silently default to "valid". ParseGetLogsQuery()'s own implementation
    // must explicitly set `valid = true` at the very end, only once every
    // one of the six parameters below has been confirmed clean.
    bool valid = false;
    std::string errorMessage;
    LogQueryFilter filter;
};

// Parses the six raw query-string values httplib handed back (all may be
// "") into a LogQueryFilter, or reports `valid == false` with a
// human-readable errorMessage for a malformed value:
//   - sinceIdParam/frameMinParam/frameMaxParam: must each be empty, or a
//     valid non-negative base-10 integer literal.
//   - minLevelParam: must be empty, or one of "debug"/"info"/"warning"/
//     "error" (case-insensitive - see TryParseLogLevel(), Editor/Logger.h).
//   - limitParam: must be empty, or a valid non-negative base-10 integer
//     literal - defaults to 200 when empty, and is SILENTLY CLAMPED to
//     Logger::kCapacity (2000) when larger, rather than treated as an
//     error (a caller asking for "too many" is harmless, unlike a
//     genuinely malformed value).
//   - categoryParam/keywordParam: always valid as-is (any string,
//     including empty, is acceptable).
ParsedGetLogsQuery ParseGetLogsQuery(const std::string& sinceIdParam, const std::string& minLevelParam,
    const std::string& categoryParam, const std::string& keywordParam, const std::string& frameMinParam,
    const std::string& frameMaxParam, const std::string& limitParam);

// Builds GET /get_logs' JSON response body:
//   {"logging_enabled": true|false, "count": N, "latest_id": M,
//    "entries": [{"id":..., "frame":..., "timestamp_seconds":...,
//                 "level":"Info", "category":"...", "message":"..."}, ...]}
// `loggingEnabled` should be Logger::IsEnabled() and `latestId` should be
// Logger::LatestEntryId() - passed in explicitly (not read here) so this
// function stays a pure, Tier-1-testable function of already-resolved
// plain values, matching every other Build*ResponseJson() in this file.
std::string BuildGetLogsResponseJson(
    const std::vector<LogEntry>& entries, bool loggingEnabled, std::uint64_t latestId);

// Builds POST /clear_logs' JSON response body:
//   {"success": true, "cleared_count": N}
std::string BuildClearLogsResponseJson(std::size_t clearedCount);
```

### 3.2 `src/Network/NetworkRoutes.cpp` implementations

- `ParseGetLogsQuery(...)`: a small integer-parsing helper is needed
  (base-10, non-negative, `std::uint64_t`) — check whether this file
  already has one (search for an existing `std::stoull`/manual-digit-parse
  helper used by another route's numeric query param before writing a new
  one; if one already exists, reuse it verbatim rather than duplicating).
  For each of `since_id`/`frame_min`/`frame_max`: empty -> leave that
  `LogQueryFilter` field at its default (`hasFrameMin`/`hasFrameMax` stay
  `false`, `sinceId` stays `0`); non-empty and parses cleanly -> set the
  field (and the matching `has*` flag for frame_min/frame_max); non-empty
  and does NOT parse cleanly (non-digit characters, empty after trimming,
  overflow) -> `valid = false` with a specific `errorMessage` naming which
  parameter was bad. `min_level`: empty -> `hasMinLevel` stays `false`;
  non-empty -> `TryParseLogLevel(...)`, and on failure `valid = false` with
  an errorMessage listing the accepted values. `limit`: empty -> `200`;
  non-empty and parses -> `std::min(parsedValue, Logger::kCapacity)`;
  non-empty and malformed -> `valid = false`. `category`/`keyword`: copied
  through verbatim, always valid. **Since `ParsedGetLogsQuery::valid`
  defaults to `false` (see 3.1's corrected struct above), the function must
  explicitly set `result.valid = true;` once every one of the six parameters
  has been checked and none of them set `valid = false` - e.g. as the very
  last statement before `return result;` (mirror `ParseActivateTabQuery()`'s
  own "valid = true;" placement at the end of its own successful path).
- `BuildGetLogsResponseJson(...)`: builds an `nlohmann::json` array from
  `entries` (one object per entry, fields exactly as documented above —
  `"level"` is `ToString(entry.level)`), wraps it with `logging_enabled`/
  `count` (== `entries.size()`)/`latest_id`, `.dump()`s it.
- `BuildClearLogsResponseJson(...)`: trivial two-field object, `.dump()`s
  it.

### 3.3 `src/Network/NetworkServer.cpp` wiring

Inside `RegisterRoutes(...)` (no new parameter needed on this function or
on the `NetworkServer` constructor — this is the one campaign-visible
difference from every previous bridge-based feature: **zero new
constructor parameters, zero new bridge class**):

```cpp
// logger-1 campaign - GET /get_logs. Needs NO bridge at all, exactly like
// GET /list_tabs above - Logger is its OWN, purpose-built, thread-safe
// store (see AGENTS.md, "Logging"), not engine state reached through the
// usual bridge rule (see AGENTS.md, "Networking" - this route is a
// documented, narrow, deliberate EXCEPTION to that rule, not a precedent
// for bypassing it elsewhere).
server.Get("/get_logs", [](const httplib::Request& req, httplib::Response& res) {
    const ParsedGetLogsQuery parsed = ParseGetLogsQuery(req.get_param_value("since_id"),
        req.get_param_value("min_level"), req.get_param_value("category"),
        req.get_param_value("keyword"), req.get_param_value("frame_min"),
        req.get_param_value("frame_max"), req.get_param_value("limit"));
    if (!parsed.valid) {
        res.status = 400;
        res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
        return;
    }
    const std::vector<LogEntry> entries = Logger::Query(parsed.filter);
    res.set_content(
        BuildGetLogsResponseJson(entries, Logger::IsEnabled(), Logger::LatestEntryId()), "application/json");
});

// logger-1 campaign - POST /clear_logs. Same "no bridge needed" shape as
// GET /get_logs above.
server.Post("/clear_logs", [](const httplib::Request&, httplib::Response& res) {
    const std::size_t clearedCount = Logger::EntryCount();
    Logger::Clear();
    res.set_content(BuildClearLogsResponseJson(clearedCount), "application/json");
});
```

Confirm `BuildGenericErrorResponseJson(...)` already exists in this file
(it is used by `/activate_tab` etc.) and reuse it verbatim for the 400
case — do not write a second, differently-shaped error JSON builder.

### 3.4 Documentation

- `AGENTS.md`: add a new `## Logging` section, positioned right after the
  existing `## Networking` section (or right before it — pick whichever
  reads better given the final heading order, and say which in the
  completion report), following the exact one-paragraph-summary-plus-link
  style every other section already uses, e.g.:
  ```
  ## Logging

  `src/Editor/Logger.h/.cpp` (`gte::Logger`) is the engine's Editor-only,
  thread-safe, in-memory log store - callable from any thread via the
  GTE_LOG_DEBUG/INFO/WARNING/ERROR macros, which compile to a true empty
  no-op (not even evaluating their arguments) whenever GTE_ENABLE_EDITOR is
  OFF. A bounded 2000-entry ring buffer, filterable by level/category/
  keyword/frame range/an incremental since_id cursor, surfaced by the
  Editor's "Log" panel and by GET /get_logs / POST /clear_logs.

  Full convention: [docs/conventions/logging.md](docs/conventions/logging.md).
  ```
  (The "Editor's Log panel" reference is forward-looking to `PHASE4` — fine
  to land now, since this doc update only needs to be true by the time the
  whole campaign finishes, not this exact phase.)
- Also update the EXISTING `## Networking` section's own endpoint-summary
  sentence (the one listing `GET /get_swapchain`/`/get_game_view`/
  `/get_texture`, ECS-mutating commands, Editor UI control) to also mention
  `GET /get_logs`/`POST /clear_logs`.
- `docs/conventions/logging.md` (new file): the full write-up — mirror
  `docs/conventions/profiling.md`'s or `docs/conventions/networking.md`'s
  own level of detail. Must explicitly cover: the dual-branch
  `#if GTE_ENABLE_EDITOR` header shape and why (zero-cost release builds);
  the four log levels and their ordinal filtering semantics; the ring
  buffer's fixed 2000-entry capacity and FIFO eviction; the monotonic,
  never-reset-by-Clear() id and how `since_id` polling is meant to be used
  by a caller; the full `GET /get_logs`/`POST /clear_logs` contract
  (every query parameter, every status code); and — IMPORTANT — a clearly
  labeled paragraph explaining that these two routes calling `Logger::`
  directly, with no bridge, is a deliberate, narrow exception specific to
  Logger's own from-day-one thread-safe design, and must NOT be read as
  permission for any OTHER future route to bypass the bridge rule for
  actual ECS/Renderer/Game state.
- `docs/conventions/networking.md`: add a new bullet (following the file's
  existing chronological-by-campaign bullet-list convention, appended near
  the end alongside the most recent campaigns already documented there)
  describing `GET /get_logs`/`POST /clear_logs` and cross-linking to the
  new `docs/conventions/logging.md` for the full Logger-side detail rather
  than duplicating it.
- `docs/README.md`: add a new
  `- **[Logging](conventions/logging.md)** — ...` bullet to the existing
  `## Conventions` list (any position is fine).

### 3.5 Tests: `tests/Network/NetworkRoutesTests.cpp`

This file already compiles unconditionally (the Network module has no
`GTE_ENABLE_EDITOR` dependency of its own) — but it now transitively
`#include`s `Editor/Logger.h`, whose real behavior only exists when
`GTE_ENABLE_EDITOR` is ON. Split the new test cases accordingly:

- **Always-compiled cases** (no `#if` needed): `ParseGetLogsQuery(...)`'s
  own pure parsing logic for every malformed-input case (bad `since_id`/
  `frame_min`/`frame_max`/`min_level`/`limit` values each produce
  `valid == false` with a sensible `errorMessage`; every empty-string
  field leaves the matching `LogQueryFilter` field at its default; a
  `limit` larger than `Logger::kCapacity` gets clamped, not rejected) and
  `BuildGetLogsResponseJson(...)`/`BuildClearLogsResponseJson(...)`'s own
  JSON-shape correctness given a hand-built `std::vector<LogEntry>` (these
  two functions never call into live `Logger::` state themselves, so they
  produce identical, checkable output in EITHER `GTE_ENABLE_EDITOR`
  configuration).
- **`#if GTE_ENABLE_EDITOR`-guarded cases**: anything that calls
  `Logger::Log(...)`/`Logger::Query(...)`/`Logger::Clear()` directly to set
  up real data before exercising `ParseGetLogsQuery()` + `Logger::Query()`
  + `BuildGetLogsResponseJson()` together end-to-end (since those calls are
  silent no-ops, and `Logger::Query()` always returns empty, when the
  switch is OFF — such an assertion would be FALSE in that configuration,
  not merely "vacuously true", so it must be compiled out entirely rather
  than left in and quietly passing for the wrong reason). Call
  `Logger::Clear()` at the start of any such test case for the same
  test-isolation reason `PHASE1`'s own tests already need it.

No new file — add these as new `TEST(...)` blocks inside the existing
`NetworkRoutesTests.cpp`.

## Definition of Done for this phase

- Fast, targeted incremental compile check (default `GTE_ENABLE_EDITOR=ON`
  configuration); run just this file's tests (e.g.
  `--gtest_filter=*GetLogs*:*ClearLogs*`) to confirm they pass.
- Write `PHASE3_COMPLETION_REPORT.md` next to this file.
- `git add`/`git commit` (this phase's own changes + the report) with a
  clear message.
