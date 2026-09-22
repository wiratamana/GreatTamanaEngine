# PHASE1 — Core Logger Module

Parent: `PHASE0_MASTER_STRATEGY.md` (read it first — it holds every Locked
Design Decision this phase implements, and the Non-Goals this phase must not
scope-creep into).

**Use `ask_questions` for any genuine ambiguity this document doesn't already
resolve.** If this phase itself delegates any further sub-task, that
delegation prompt must repeat this same instruction.

---

## Step 1: The Goal (Where are we going?)

Land a self-contained, thread-safe, always-compiling `src/Editor/Logger.h`/
`.cpp` module — `LogLevel`, `LogEntry`, `LogQueryFilter`, the `Logger` static
class, and the `GTE_LOG_DEBUG`/`GTE_LOG_INFO`/`GTE_LOG_WARNING`/
`GTE_LOG_ERROR` macros — with **zero** engine call sites, **zero** network
wiring, and **zero** UI wiring yet. This phase's own Tier-1 tests
(`tests/Editor/LoggerTests.cpp`) are the only consumer. Every later phase
builds strictly on top of this one without changing its public API.

## Step 2: The Situation (Where are we now?)

- No `src/Editor/Logger.*` file exists yet.
- The dual-branch "always compiles, only the ON branch does real work"
  shape this phase must copy lives in `src/Profiling/ScopeTimer.h`:
  `GTE_PROFILE_SCOPE` is defined unconditionally near the top of the file,
  then `class ScopeTimer { ... }` is declared TWICE — once (full
  implementation) inside `#if GTE_ENABLE_PROFILER`, once (fully inline,
  trivial, does-nothing constructor) inside the matching `#else`. Copy this
  exact shape for `Logger`.
- The "static-method, process-global class, no instance/DI" shape this phase
  must copy lives in `src/Memory/SdlMemoryTracker.h` — **not**
  `src/Renderer/Memory/GpuMemoryTracker.h`, even though `PHASE0`'s own Step 2
  cites both together. Checked directly against the real files:
  `GpuMemoryTracker` is actually an ordinary, non-static, deliberately
  non-copyable/non-movable **instance** class — its own class comment says
  "always create exactly one and own it via `std::shared_ptr`, handing
  copies of that shared_ptr to every Buffer/RenderTexture it creates."
  `SdlMemoryTracker.h`'s own class comment even draws this exact contrast
  explicitly: *"Not an instance/RAII type like `GpuMemoryTracker`... the
  counters here are necessarily static/process-global."* `Logger` must copy
  `SdlMemoryTracker`'s shape (every method `static`, no constructor, no
  instance ever created, no `this`) — do **not** model it on
  `GpuMemoryTracker`'s instance-based shape, and do not repeat `PHASE0`'s
  loose "both are the same shape" framing when documenting this elsewhere.
- `src/Editor/EditorPanelCatalog.h` is the existing, precedented example of a
  header physically under `src/Editor/` that is safely `#include`-able from
  outside `src/Editor/` (specifically from `src/Network/NetworkRoutes.h`,
  line 6, confirmed) regardless of `GTE_ENABLE_EDITOR` — because it has no
  matching gated `.cpp`. `Logger.h` follows the same principle, but (unlike
  `EditorPanelCatalog.h`) DOES need a real out-of-line implementation for
  its ON branch, hence `Logger.cpp` — which is added to the build ONLY
  inside CMake's existing `if(GTE_ENABLE_EDITOR)` block (see below), exactly
  like every other `src/Editor/*.cpp` file.
- The root `CMakeLists.txt`'s `if(GTE_ENABLE_EDITOR)` block (the one that
  lists `target_sources(gte_core PRIVATE src/Editor/EditorContext.h ...)`)
  currently includes, among many others (confirmed against the real file):
  ```
  src/Editor/ImGuiMemoryTracker.h
  src/Editor/ImGuiMemoryTracker.cpp
  src/Editor/MemoryPanelData.h
  src/Editor/MemoryPanelData.cpp
  ```
  Add `src/Editor/Logger.h`/`src/Editor/Logger.cpp` as two new lines inside
  that same block (position doesn't matter functionally; placing them right
  before `src/Editor/MemoryPanelData.h` keeps the tracker-ish files grouped
  together, but any consistent placement inside the existing block is fine).
- `tests/CMakeLists.txt`'s own `if(GTE_ENABLE_EDITOR)` block (around its
  `list(APPEND GTE_TEST_SOURCES Editor/EditorCameraTests.cpp ...)` call)
  already lists `Editor/ImGuiMemoryTrackerTests.cpp`,
  `Editor/MemoryPanelDataTests.cpp`, etc. (confirmed against the real
  file) — add `Editor/LoggerTests.cpp` there the same way.
- `Core/Time.h`'s `Time::FrameCount()` is NOT reachable from this module —
  do not `#include "../Core/Time.h"` or otherwise couple `Logger` to `Time`/
  `EngineContext`. This phase's `Logger::SetCurrentFrame(std::uint64_t)`
  takes a plain, already-resolved integer — `PHASE2` is what actually calls
  it with `engineContext.time.FrameCount()`'s value, from `Application.cpp`.

## Step 3: The Plan

### 3.1 `src/Editor/Logger.h`

Structure (mirror the doc-comment density/style of `GpuMemoryTracker.h` —
that file is still a good STYLE reference even though its actual class
shape, per Step 2 above, is not what `Logger` should copy):

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// GTE_LOG_* macros - the ONE sanctioned way any call site anywhere in the
// engine talks to the Logger (see AGENTS.md, "Logging", and
// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #13). Defined
// unconditionally so a call site never needs its own #ifdef - see
// Profiling/ScopeTimer.h's GTE_PROFILE_SCOPE for the identical precedent.
//
// Compile to a true empty no-op (`((void)0)` - the `message`/`category`
// expressions are NEVER EVEN EVALUATED, not just discarded) when
// GTE_ENABLE_EDITOR is OFF - a full release runtime game build pays
// EXACTLY zero cost for every GTE_LOG_* call site in the entire engine,
// including the cost of building the message string itself.
#if GTE_ENABLE_EDITOR
#define GTE_LOG_DEBUG(category, message)   ::gte::Logger::Log(::gte::LogLevel::Debug,   category, message)
#define GTE_LOG_INFO(category, message)    ::gte::Logger::Log(::gte::LogLevel::Info,    category, message)
#define GTE_LOG_WARNING(category, message) ::gte::Logger::Log(::gte::LogLevel::Warning, category, message)
#define GTE_LOG_ERROR(category, message)   ::gte::Logger::Log(::gte::LogLevel::Error,   category, message)
#else
#define GTE_LOG_DEBUG(category, message)   ((void)0)
#define GTE_LOG_INFO(category, message)    ((void)0)
#define GTE_LOG_WARNING(category, message) ((void)0)
#define GTE_LOG_ERROR(category, message)   ((void)0)
#endif

namespace gte {

enum class LogLevel : std::uint8_t { Debug, Info, Warning, Error };

// "Debug"/"Info"/"Warning"/"Error" - used by the Editor Log panel and by
// GET /get_logs' own JSON "level" field. An out-of-range value falls back
// to "Unknown", never reads out of bounds.
const char* ToString(LogLevel level) noexcept;

// Case-insensitive parse of "debug"/"info"/"warning"/"error" -> LogLevel,
// used by Network/NetworkRoutes.h's ParseGetLogsQuery() (Phase 3). Returns
// false (leaving *outLevel untouched) for anything else.
bool TryParseLogLevel(const std::string& text, LogLevel* outLevel) noexcept;

struct LogEntry {
    std::uint64_t id = 0;              // Monotonic, never reused, never reset by Clear().
    std::uint64_t frameNumber = 0;     // Logger::SetCurrentFrame()'s last value at record time.
    double timestampSeconds = 0.0;     // Seconds since THIS PROCESS's first Logger::Log() call.
    LogLevel level = LogLevel::Info;
    std::string category;
    std::string message;
};

// Query parameters for Logger::Query() - every field is a filter that is
// SKIPPED (matches everything) when left at its default/empty value. All
// filters that ARE set must ALL match for an entry to be included (logical
// AND), mirroring every other multi-field filter/parse struct already in
// this codebase (e.g. Network/NetworkRoutes.h's request-parsing structs).
struct LogQueryFilter {
    std::uint64_t sinceId = 0;         // Only entries with id > sinceId. 0 = from the very start.
    bool hasMinLevel = false;
    LogLevel minLevel = LogLevel::Debug; // Inclusive-and-above by ordinal value (Debug < Info < Warning < Error).
    std::string category;              // Empty = any. Exact, case-sensitive match otherwise.
    std::string keyword;                // Empty = any. Case-insensitive substring match on `message`.
    bool hasFrameMin = false;
    std::uint64_t frameMin = 0;        // Inclusive.
    bool hasFrameMax = false;
    std::uint64_t frameMax = 0;        // Inclusive.
    std::size_t limit = 0;             // 0 = no limit. Otherwise keep only the NEWEST `limit` matches.
};

#if GTE_ENABLE_EDITOR

// Thread-safe, process-global, Editor-only in-memory log store - see
// AGENTS.md ("Logging") for the full convention. Not an instance/DI type,
// same rationale as SdlMemoryTracker (Memory/SdlMemoryTracker.h) - a
// Logger call site can be anywhere in the engine, on any thread, with no
// natural single owner to thread a reference through. (NOTE:
// GpuMemoryTracker is sometimes loosely grouped with SdlMemoryTracker as a
// similar precedent, but it is actually the OPPOSITE shape - an instance
// class explicitly owned via std::shared_ptr, never static-global - see
// SdlMemoryTracker.h's own comment contrasting itself against
// GpuMemoryTracker directly. Logger follows SdlMemoryTracker's all-static
// shape only.)
//
// EVERY public method here is safe to call concurrently from ANY thread
// (main thread, a Jobs::JobSystem worker, the Network background thread) -
// this is a REQUIRED property, not an incidental one, since real call
// sites exist on all three (see AGENTS.md). Internally guarded by one
// mutex protecting BOTH the ring buffer AND the monotonic id counter's
// assignment (see Logger.cpp - `id` is assigned only while already
// holding that mutex, specifically so "ascending id order" and "push/
// append order" can never disagree under concurrent callers).
// SetCurrentFrame()'s value is a separate, relaxed atomic read/write, not
// mutex-guarded (see Logger.cpp for why that's still correct).
class Logger {
public:
    static constexpr std::size_t kCapacity = 2000;

    // Records one entry, stamping it with the CURRENT SetCurrentFrame()
    // value and a timestamp relative to this process's first ever
    // Logger::Log() call. If the ring buffer is already at kCapacity, the
    // single oldest entry is evicted first (FIFO). Never called directly
    // by feature code - see the GTE_LOG_* macros above. Deliberately NOT
    // noexcept (see this file's own "Important nuances" note in
    // PHASE1_CORE_LOGGER_MODULE.md): it locks a mutex and allocates
    // (std::string/LogEntry copies), either of which can theoretically
    // throw.
    static void Log(LogLevel level, const std::string& category, const std::string& message);

    // Called ONCE per real engine frame, from Application::Run() (Phase 2)
    // - stamps every LogEntry recorded AFTER this call with `frameNumber`
    // until the next call. Safe to call from the main thread only in
    // production, but the method itself has no such restriction baked in.
    static void SetCurrentFrame(std::uint64_t frameNumber) noexcept;

    // Returns every currently-retained entry matching `filter`, in
    // ASCENDING id order (oldest matching first) - EXCEPT that when
    // filter.limit > 0 and more than `limit` entries match, only the
    // NEWEST `limit` of them are kept (still returned in ascending id
    // order among themselves). This "keep the newest N, but return them
    // oldest-first" shape is deliberate: it bounds response size while
    // still reading top-to-bottom in a Console-style UI/log the same way
    // a human expects.
    static std::vector<LogEntry> Query(const LogQueryFilter& filter);

    // Empties the ring buffer. Does NOT reset the monotonic id counter -
    // the next Log() call after Clear() continues from wherever the
    // counter already was, so a caller's stale `since_id` cursor from
    // before the clear is never misread as "still current". Deliberately
    // NOT noexcept, same reasoning as Log() above (locks a mutex).
    static void Clear();

    // Number of entries currently retained (0..kCapacity). O(1).
    static std::size_t EntryCount() noexcept;

    // The highest id ever assigned so far (0 if Log() has never been
    // called). Useful for a caller establishing an initial since_id
    // cursor ("start watching from now onward").
    static std::uint64_t LatestEntryId() noexcept;

    // True in this branch, always - lets Network/NetworkRoutes.h (Phase 3)
    // and Editor/LogPanelData.h (Phase 4) distinguish "this build can
    // record logs, there are just none yet" from "this build can never
    // record anything" (see ProfilerPanelData.h's own free constexpr
    // `kCpuScopeInstrumentationCompiledIn` for the conceptual precedent this
    // mirrors - that one is a free variable rather than a class method, but
    // serves the exact same "is the real thing compiled in" purpose).
    static constexpr bool IsEnabled() noexcept { return true; }
};

#else // !GTE_ENABLE_EDITOR

// Compiled-out form: every method is fully inline and does nothing,
// exactly mirroring Profiling/ScopeTimer.h's own `#else` branch. No
// Logger.cpp is compiled into the build at all in this configuration
// (see CMakeLists.txt) - nothing here needs linking.
class Logger {
public:
    static constexpr std::size_t kCapacity = 2000;

    // NOT noexcept, even though this trivial body obviously cannot throw -
    // see this file's own "Important nuances" note below on why this
    // branch's signature must stay byte-for-byte identical (including
    // noexcept-ness) to the ON branch's real, genuinely-throwing-capable
    // Log().
    static void Log(LogLevel, const std::string&, const std::string&) { }
    static void SetCurrentFrame(std::uint64_t) noexcept { }
    static std::vector<LogEntry> Query(const LogQueryFilter&) { return {}; }
    // NOT noexcept - same reasoning as Log() above.
    static void Clear() { }
    static std::size_t EntryCount() noexcept { return 0; }
    static std::uint64_t LatestEntryId() noexcept { return 0; }
    static constexpr bool IsEnabled() noexcept { return false; }
};

#endif // GTE_ENABLE_EDITOR

} // namespace gte
```

Important nuances to get right:

- The `#if`/`#else` branches must declare **byte-for-byte the same public
  API** (same method names, same parameter types, same `noexcept`-ness
  where practical) so every caller anywhere in the engine (including
  `Application.cpp` and `NetworkServer.cpp`, both of which are always
  compiled regardless of `GTE_ENABLE_EDITOR`) can call `Logger::` methods
  with **zero `#ifdef` at the call site** — this is the whole point of the
  dual-branch shape.
- **`Log()` and `Clear()` are deliberately declared WITHOUT `noexcept` in
  BOTH branches**, even though they look like natural `noexcept`
  candidates. The real (`#if` ON) implementations must lock a
  `std::mutex` and allocate (`std::string`/`std::vector<LogEntry>`
  copies), either of which can theoretically throw (`std::system_error`,
  `std::bad_alloc`), so they are correctly left non-`noexcept` there. The
  trivial `#else` bodies COULD safely be marked `noexcept` (they do
  nothing), but must deliberately be left un-annotated too, purely so the
  two branches keep byte-for-byte identical signatures per the rule
  above — do not "improve" only the `#else` branch by adding `noexcept`
  back onto `Log()`/`Clear()` there; that would silently reintroduce a
  signature mismatch between the two branches.
- `LogLevel`, `ToString()`, `TryParseLogLevel()`, `LogEntry`, and
  `LogQueryFilter` are **not** wrapped in `#if GTE_ENABLE_EDITOR` at all —
  they are plain, cheap, always-available data types/free functions (same
  category as `GpuResourceRecord`/`GpuResourceType` in
  `GpuMemoryTracker.h`, which also live outside that file's own
  `#if GTE_ENABLE_EDITOR` section). This keeps `NetworkRoutes.h` (Phase 3)
  able to reference `LogLevel`/`LogEntry`/`LogQueryFilter` in its own
  always-compiled parsing/JSON-building function signatures without any
  conditional compilation of its own.
- `Logger.h` itself declares no data members of any kind (all state lives
  in `Logger.cpp`, see 3.2 below), so it needs no `<mutex>`/`<atomic>`/
  `<deque>`/`<chrono>` includes at all — keep its include list limited to
  `<cstdint>`, `<string>`, `<vector>` (for `LogEntry`/`LogQueryFilter`'s own
  members and `Query()`'s return type). Pulling in `<mutex>` etc. here
  would be harmless but misleading, implying `Logger.h`'s class carries
  private state it does not.

### 3.2 `src/Editor/Logger.cpp` (the `#if GTE_ENABLE_EDITOR` implementation)

- Required includes: `"Logger.h"`, `<mutex>`, `<atomic>`, `<deque>`,
  `<chrono>`, `<algorithm>`, and `<cctype>` (the last two for the
  case-insensitive comparisons `Query()`'s keyword filter and
  `ToString()`/`TryParseLogLevel()` all need — see the `std::tolower`
  caution below).
- Internal state (anonymous namespace or static class members — follow
  `GpuMemoryTracker.cpp`'s own style of a few file-static/anonymous-namespace
  helpers plus straightforward member functions):
  - `std::mutex s_mutex;` guarding `std::deque<LogEntry> s_entries;` (a
    `std::deque` is the natural fit for "push to the back, pop from the
    front once over capacity, iterate front-to-back" — no need for a
    hand-rolled circular array).
  - `std::atomic<std::uint64_t> s_nextId{1};` — **must only ever be
    incremented while `s_mutex` is already held** (see `Log()` below).
    Kept as an atomic type purely so `LatestEntryId()` can read it
    lock-free without itself needing to take `s_mutex` — NOT because
    unguarded concurrent increments from multiple threads are expected or
    safe for this counter's actual use here.
  - `std::atomic<std::uint64_t> s_currentFrame{0};` — `SetCurrentFrame()`
    stores with `std::memory_order_relaxed`; `Log()` loads it the same way.
    Relaxed is correct here: there is no other memory this value needs to
    be ordered against — it is a plain "best current label", not a
    synchronization point.
  - A lazily-initialized process-start reference point for the wall-clock
    timestamp: a function-local `static const std::chrono::steady_clock::time_point
    s_startTime = std::chrono::steady_clock::now();` inside `Log()` (or a
    small private helper) — C++11 guarantees thread-safe, exactly-once
    initialization of a function-local `static`, so this needs no separate
    mutex/flag of its own. `timestampSeconds` for a given call is
    `std::chrono::duration<double>(std::chrono::steady_clock::now() -
    s_startTime).count()`. This is DELIBERATELY independent of
    `gte::Time`/`EngineContext` — see `PHASE0`'s Step 2 for why.
- `Log(level, category, message)`:
  1. Read `s_currentFrame` (relaxed) and compute `timestampSeconds` (both
     outside the mutex — neither needs it, and neither depends on ordering
     relative to any other thread).
  2. Lock `s_mutex`. **Only now** assign `id` (e.g. `const std::uint64_t
     id = s_nextId++;`). This ordering matters and is easy to get wrong:
     if `id` were instead assigned via a fetch-and-increment BEFORE
     acquiring `s_mutex` (a tempting "keep the hot path lock-free a
     little longer" micro-optimization), two racing threads could be
     assigned their `id`s in one order but then acquire `s_mutex` — and
     therefore `emplace_back` into `s_entries` — in the OPPOSITE order,
     silently breaking the "the ring buffer is always in ascending id
     order" invariant that `Query()`'s iteration and the eviction step
     below both depend on. Assigning `id` only once `s_mutex` is already
     held makes "id order" and "append order" the same total order by
     construction, with no extra synchronization required.
  3. Still holding `s_mutex`: `emplace_back` the fully-built `LogEntry`
     (this `id`; the `frameNumber`/`timestampSeconds` read in step 1;
     `level`; `category`; `message`); if `s_entries.size() > kCapacity`,
     `pop_front()` exactly one (never more than one per call, since
     capacity can only ever be exceeded by exactly one after a single
     push).
- `Query(filter)`:
  1. Lock `s_mutex`; iterate `s_entries` front-to-back (ascending id order
     already, since — per the corrected `Log()` above — they are always
     both assigned their id AND appended while holding the same lock);
     for each entry, apply every filter field in turn (all must pass):
     `id > filter.sinceId`;
     `!filter.hasMinLevel || static_cast<int>(entry.level) >=
     static_cast<int>(filter.minLevel)`; `filter.category.empty() ||
     entry.category == filter.category`; `filter.keyword.empty() ||`
     (case-insensitive substring search of `filter.keyword` inside
     `entry.message` — write a small private case-insensitive substring
     helper, e.g. lowercase-copy both sides and use `std::string::find`).
     **When lowercasing, cast each `char` to `unsigned char` before
     calling `std::tolower`** (`std::tolower(static_cast<unsigned char>(c))`)
     — passing a plain, possibly-negative `char` directly to `std::tolower`
     is undefined behavior per the standard for any byte outside the
     basic (7-bit) character set, a classic, easy-to-miss bug; the same
     caution applies to `TryParseLogLevel()`'s own case-insensitive
     compare below;
     `!filter.hasFrameMin || entry.frameNumber >= filter.frameMin`;
     `!filter.hasFrameMax || entry.frameNumber <= filter.frameMax`.
  2. Collect every match into a `std::vector<LogEntry>` (already ascending
     by id by construction).
  3. If `filter.limit > 0` and the result has more than `filter.limit`
     entries, erase from the FRONT until only the newest `filter.limit`
     remain (`result.erase(result.begin(), result.begin() + (result.size()
     - filter.limit))`) — this keeps the vector still ascending by id.
  4. Return by value (a full copy) — this is intentionally a snapshot; the
     caller never gets a reference into live, mutex-guarded state.
- `Clear()`: lock `s_mutex`; `s_entries.clear();`. Do **not** touch
  `s_nextId`.
- `EntryCount()`: lock `s_mutex`; return `s_entries.size()`.
- `LatestEntryId()`: `s_nextId.load(std::memory_order_relaxed) - 1` if
  `s_nextId > 1`, else `0` (careful with the unsigned-underflow edge case
  when no entry has ever been logged yet — write it as an explicit branch,
  not silent unsigned wraparound).
- `ToString(LogLevel)`/`TryParseLogLevel(...)`: trivial switch/if-chain,
  case-insensitive compare for parsing (lowercase the input first — apply
  the same `static_cast<unsigned char>` caution noted under `Query()`'s
  keyword matching above).

### 3.3 CMake wiring

- Root `CMakeLists.txt`: inside the existing `if(GTE_ENABLE_EDITOR)`
  `target_sources(gte_core PRIVATE ...)` block, add:
  ```
  src/Editor/Logger.h
  src/Editor/Logger.cpp
  ```
- `tests/CMakeLists.txt`: inside the existing
  `if(GTE_ENABLE_EDITOR) list(APPEND GTE_TEST_SOURCES ...)` block, add:
  ```
  Editor/LoggerTests.cpp
  ```

### 3.4 `tests/Editor/LoggerTests.cpp`

A plain GoogleTest file (mirror `Editor/ImGuiMemoryTrackerTests.cpp`'s/
`Editor/MemoryPanelDataTests.cpp`'s existing style/header include shape).
**Important**: `Logger` is process-global static state — tests running in
the same process must each call `Logger::Clear()` at the START of their own
body (not rely on test order) so one test's leftover entries never leak into
another's assertions; the monotonic id counter is expected to keep climbing
across tests within the same run, so assert relative id ordering
(`entryB.id > entryA.id`), never a specific absolute id value.

Cover at least:

- **Basic record + query**: log 3 entries with distinct
  category/level/message; `Query({})` (no filters) returns all 3, ascending
  by id, with the exact fields expected.
- **Ring buffer eviction**: log `kCapacity + 5` entries; `EntryCount() ==
  kCapacity`; the oldest surviving entry's `message` is the 6th one ever
  logged (the first 5 were evicted).
- **`since_id` cursor**: log entries, note the returned/inferred id of the
  2nd one, `Query({.sinceId = thatId})` returns only entries after it.
- **`min_level` ordinal filter**: log one entry per level; a query with
  `hasMinLevel=true, minLevel=Warning` returns exactly the Warning and
  Error entries, in that order.
- **`category` exact match**: two entries with different categories; a
  category filter returns only the matching one; confirm it is
  case-sensitive (a differently-cased category does NOT match).
- **`keyword` substring, case-insensitive**: an entry with message
  `"Something Failed To Load"`; a keyword filter of `"failed"` (lowercase)
  matches it.
- **`frame_min`/`frame_max` inclusive range**: `SetCurrentFrame(N)` before
  each `Log()` call for a few different N values; range filters include
  exactly the expected subset, boundary values included.
- **`limit` keeps the newest N**: log 10 entries, `Query({.limit = 3})`
  returns exactly the LAST 3, still in ascending id order among
  themselves.
- **`Clear()` semantics**: log a few entries, note `LatestEntryId()`,
  `Clear()`, assert `EntryCount() == 0`, log one more entry, assert its id
  is STRICTLY GREATER than the pre-clear `LatestEntryId()` (never reused/
  reset).
- **`TryParseLogLevel`/`ToString` round-trip**: every valid level name
  (any casing) round-trips; an invalid string returns `false` and leaves
  the output untouched.
- **`GTE_LOG_*` macros actually reach `Logger`**: call each of
  `GTE_LOG_DEBUG`/`GTE_LOG_INFO`/`GTE_LOG_WARNING`/`GTE_LOG_ERROR` once
  (with a distinct category/message per call), then `Query({})` and
  confirm all 4 entries exist with the exact level/category/message each
  macro was expected to produce. This is the only place in this phase that
  ever exercises the macros themselves (every other test here calls
  `gte::Logger::` methods directly, per `PHASE0`'s Locked Design Decision
  #13) — without it, a typo inside a macro body (e.g. a swapped `LogLevel`
  constant between two of the four macros) could slip through completely
  unnoticed until `PHASE2` adds real call sites.
- **Concurrency smoke test** (no crash/deadlock, not a correctness proof of
  exact interleaving): spawn e.g. 8 `std::thread`s, each logging ~200
  entries with a distinct category per thread, `join()` all of them, then
  assert `EntryCount() <= Logger::kCapacity` and that `Query({})` succeeds
  without throwing/crashing. **Also assert the returned entries' `id`
  fields are STRICTLY ascending with no duplicates** (i.e. for every
  adjacent pair, `result[i].id > result[i-1].id`) — this is a genuine
  correctness assertion, not just a crash check, made possible specifically
  because `Log()` assigns `id` only while already holding `s_mutex` (see
  `Logger.cpp`'s own Step 3.2 note on why); a future regression back to
  assigning `id` before acquiring the lock would fail this exact assertion
  under real concurrent load, which is precisely the bug this test exists
  to catch. This is a genuine, valuable regression check even without a
  thread sanitizer available on this toolchain.

## Definition of Done for this phase

- Fast, targeted incremental compile check (just rebuild `gte_core` and
  `GreatTamanaEngineTests`, default `GTE_ENABLE_EDITOR=ON` configuration) —
  **not** a full `ctest` run (that's Phase 5's job per `PHASE0`'s working
  agreement) — but DO run the new `LoggerTests.cpp` cases specifically
  (e.g. `GreatTamanaEngineTests.exe --gtest_filter=*Logger*`) to confirm
  they pass before moving on.
- Write `PHASE1_COMPLETION_REPORT.md` next to this file, noting anything
  that changed from this plan and why.
- `git add`/`git commit` (this phase's own files + the report) with a clear
  message.
