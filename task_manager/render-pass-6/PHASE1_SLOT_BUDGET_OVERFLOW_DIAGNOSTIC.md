# PHASE1 — Slot-Budget Overflow Diagnostic (item 2.4)

## Parent

`PHASE0_MASTER_STRATEGY.md` (read it first — this phase inherits every Locked
Design Decision and Non-Goal listed there, in particular Locked Design
Decision 3: **diagnostic-only, do not bump the budget constants**).

## Step 1: The Goal (Where are we going?)

Close a real, silent-failure gap: today, once a `RenderGraphNameSlotTable`'s
fixed slot budget is exhausted, every subsequent GPU-timing request for the
overflowing pass name is silently, permanently dropped — no log, no assert,
no Editor indicator, forever, for the rest of the process's life. After this
phase:

1. The FIRST time a name is denied a slot (in either the synchronous or the
   pipelined regime), it is reported exactly once via the engine's own
   internal logging facility (`GTE_LOG_WARNING`, see `AGENTS.md`'s "Logging"
   section — **never `std::cout`/raw `fprintf`**), so it is fetchable via
   `GET /get_logs` without recompiling anything.
2. `RenderGraphSnapshot` gains a real boolean field surfacing "this regime's
   timing slot budget is exhausted" so the Editor's "Render Graph" panel
   (and any future consumer) can show it structurally, not just infer it
   from a pass's GPU time mysteriously always reading as absent.
3. Zero change to any other behavior — the exact same
   `kSynchronousTimingSlotBudget = 16` / `kPipelinedTimingSlotBudget = 8`
   constants stay exactly as they are (Locked Design Decision 3).

## Step 2: The Situation (Where are we now?)

- `RenderGraphNameSlotTable::AssignOrGetSlot()`
  (`src/Renderer/RenderGraph/RenderGraphNameSlotTable.h`, lines 61-76) is a
  small, header-only, allocation-cheap, Tier-1-testable class (see
  `tests/Renderer/RenderGraph/RenderGraphNameSlotTableTests.cpp`). Its full
  current body:
  ```cpp
  std::int32_t AssignOrGetSlot(const char* name) noexcept
  {
      if (name == nullptr) {
          return kNoNameSlot;
      }
      for (std::size_t i = 0; i < m_names.size(); ++i) {
          if (m_names[i] == name || std::strcmp(m_names[i], name) == 0) {
              return static_cast<std::int32_t>(i);
          }
      }
      if (m_names.size() >= static_cast<std::size_t>(m_slotBudget)) {
          return kNoNameSlot;
      }
      m_names.push_back(name);
      return static_cast<std::int32_t>(m_names.size() - 1);
  }
  ```
  There is no way, from outside this class, to distinguish "this name was
  never assigned a slot because the budget is genuinely full" from "this
  name was never assigned a slot because `name == nullptr`" — both return
  `kNoNameSlot`.
- `RenderGraph.h` owns exactly two instances,
  `m_synchronousTimingSlots{ kSynchronousTimingSlotBudget }` and
  `m_pipelinedTimingSlots{ kPipelinedTimingSlotBudget }` (lines 480-481).
- `RenderGraph.cpp`'s `ExecuteCompiledGraph()` calls
  `timingSlots.AssignOrGetSlot(pass.name)` once per surviving pass, per
  `Execute()` call (line 313), and passes the result straight into
  `m_timestampPool.WriteBegin()`/`WriteEnd()`, both of which already
  silently no-op for `kNoNameSlot` (by design, for the `name == nullptr`
  case — but this also masks the overflow case identically).
- `RenderGraphNameSlotTable::AssignedCount()`/`SlotBudget()` (lines 94-95)
  already exist and are exactly what a diagnostic needs to detect "this call
  just failed AND the table is already at full capacity" versus "this call
  failed because `name` was null".
- `RenderGraphCompiler.cpp`'s `DetectRenderPassEventContradictions()` (lines
  49-134) already establishes this codebase's own precedent for "a pure,
  Tier-1-testable pre-pass that reports a real problem": it returns a
  `std::vector<...>` of pure data (never printing/asserting itself), and its
  ONE caller (`Compile()`, lines 211-232) is the thing that turns a non-empty
  result into `std::fprintf(stderr, ...)` + a debug-build `assert()`. This
  phase should use the SAME two-layer shape: a pure "was this the first
  overflow" detection, reported by whichever call site actually observes it.
- `RenderGraphSnapshot.h`'s `RenderGraphSnapshot` struct (lines 165-178) is
  the exact "whole displayable snapshot of one Execute() call" type the
  Editor's "Render Graph" panel already reads — the natural home for a new
  "timing budget exhausted" flag. It is built once per `ExecuteCompiledGraph()`
  call by `BuildRenderGraphSnapshot()` (`RenderGraphSnapshot.cpp`), called
  from `RenderGraph.cpp` line 663.
- `AGENTS.md`'s "Logging" section: `src/Editor/Logger.h/.cpp` is the engine's
  Editor-only, thread-safe, in-memory log store, callable via
  `GTE_LOG_DEBUG/INFO/WARNING/ERROR` macros from any thread, compiling to a
  true empty no-op when `GTE_ENABLE_EDITOR` is OFF. `GET /get_logs`/
  `POST /clear_logs` are the HTTP endpoints that read it back.

## Step 3: The Plan (How do we get there?)

### 3.1 — `RenderGraphNameSlotTable`: make overflow observable

In `src/Renderer/RenderGraph/RenderGraphNameSlotTable.h`, change
`AssignOrGetSlot()`'s return so a caller can distinguish the three outcomes
it can produce, without changing its existing return type or existing
callers' behavior. Two acceptable shapes — pick whichever compiles cleanest
against existing callers, confirm via `ask_questions` if genuinely unsure:

- **Option A (preferred, smallest diff)**: add a second, separate query
  method, `bool JustOverflowed() const noexcept`, that returns true if and
  only if the MOST RECENT `AssignOrGetSlot()` call returned `kNoNameSlot`
  because the budget was already full for a non-null `name` that had never
  been seen before (i.e. genuinely NEW name, budget genuinely exhausted —
  not a null name, and not a name that already has a slot). Store this as a
  single `bool m_lastCallOverflowed = false;` member, set at the exact
  `return kNoNameSlot;` line inside the budget-exhausted branch, cleared at
  every other return path. This keeps `AssignOrGetSlot()`'s signature and
  every existing call site (`RenderGraph.cpp` line 313,
  `RenderGraphNameSlotTableTests.cpp`) completely unchanged.
- **Option B**: add a `bool* outOverflowed = nullptr` optional out-parameter
  to `AssignOrGetSlot()` itself. Rejected unless Option A proves awkward —
  it changes the method's signature for every existing call site for no
  real benefit over Option A's simpler "ask separately, right after" shape.

Recommended: **Option A.** Add, alongside `AssignedCount()`/`SlotBudget()`:
```cpp
// PHASE1 (render-pass-6 campaign, item 2.4) - true if and only if the MOST
// RECENT AssignOrGetSlot() call returned kNoNameSlot specifically because
// this table's fixed slotBudget was already fully assigned to OTHER names
// (a genuinely new name, budget genuinely exhausted) - false for every other
// outcome (name == nullptr, or name already had a slot). Lets a caller
// distinguish "this exact call just discovered a real overflow" from
// "kNoNameSlot for an unrelated, harmless reason", without AssignOrGetSlot()
// itself needing to log/assert/mutate any wider state - see RenderGraph.cpp's
// own call site for how this drives a real, one-time diagnostic.
bool JustOverflowed() const noexcept { return m_lastCallOverflowed; }
```
Add a matching `bool m_lastCallOverflowed = false;` private member, and set
it explicitly (`true` in the budget-exhausted branch, `false` in every other
return path — including the `name == nullptr` early return) so it is always
freshly accurate for the call that just happened, never stale from an
earlier call.

### 3.2 — `RenderGraph`: one-time, name-keyed overflow report

**New `#include` required**: `RenderGraph.cpp` does not currently include
`Editor/Logger.h` anywhere (directly or transitively through `RenderGraph.h`'s
own include list) — confirmed by grep, zero existing hit. Add
`#include "../../Editor/Logger.h"` near `RenderGraph.cpp`'s existing
`#include "../Renderer.h"` line, mirroring the exact relative-path convention
`Jobs/JobContinuation.cpp`'s/`Network/NetworkServer.cpp`'s own real
`#include "../Editor/Logger.h"` lines already use (one `../` per directory
level up from the including file to `src/`, then down into `Editor/`) — those
two files are the established precedent that `GTE_LOG_*` is a sanctioned,
cross-cutting dependency any subsystem may take, Editor-gated or not (see
`AGENTS.md`'s "Logging" section), so this is not a layering violation.

`RenderGraph` already tracks pass names 1:1 with a
`RenderGraphNameSlotTable` per regime — add a small, per-regime "already
reported" set so the SAME overflowing name is only ever logged once per
regime per process lifetime (mirrors `AGENTS.md`'s general "never spam the
log every frame for a persistent condition" expectation, and mirrors
`DetectRenderPassEventContradictions()`'s own "one report per (reader,
resource) pair" rule in spirit).

In `RenderGraph.h`, add a private member per regime (or one shared one keyed
by regime + name, whichever reads more cleanly — confirm the final shape
compiles cleanly, this is a small implementation-detail choice, not a design
one):
```cpp
// PHASE1 (render-pass-6 campaign, item 2.4) - names whose timing-slot
// overflow has already been reported this process lifetime, per regime -
// so a name that keeps overflowing every single frame is only ever logged
// ONCE, not once per frame forever. A plain vector (never a hash set),
// matching this engine's "no hashing on the hot path" convention (see
// AGENTS.md) - overflow is expected to be a rare, one-time-per-name event,
// never a steady-state hot path.
std::vector<const char*> m_reportedSynchronousOverflows;
std::vector<const char*> m_reportedPipelinedOverflows;
```
In `RenderGraph.cpp`'s `ExecuteCompiledGraph()`, immediately after the
existing `const std::int32_t timingSlot = timingSlots.AssignOrGetSlot(pass.name);`
line (line 313), add:
```cpp
if (timingSlot == kNoNameSlot && timingSlots.JustOverflowed()) {
    std::vector<const char*>& reported =
        isPipelined ? m_reportedPipelinedOverflows : m_reportedSynchronousOverflows;
    bool alreadyReported = false;
    for (const char* n : reported) {
        if (n == pass.name || (pass.name != nullptr && n != nullptr && std::strcmp(n, pass.name) == 0)) {
            alreadyReported = true;
            break;
        }
    }
    if (!alreadyReported) {
        reported.push_back(pass.name);
        GTE_LOG_WARNING("RenderGraph",
            "Pass \"" + std::string(pass.name != nullptr ? pass.name : "<unnamed>")
            + "\" could not be assigned a GPU-timing slot - the "
            + std::string(isPipelined ? "pipelined" : "synchronous")
            + " regime's fixed timing-slot budget (" + std::to_string(timingSlots.SlotBudget())
            + ") is already fully assigned to other pass names. This pass's GPU timing will read as "
              "Absent until this budget is increased.");
    }
}
```
**This must match `GTE_LOG_WARNING`'s real, current shape** (`src/Editor/Logger.h`):
it is a plain, exactly-2-argument macro, `GTE_LOG_WARNING(category, message)`,
forwarding verbatim into `Logger::Log(LogLevel, const std::string& category,
const std::string& message)` — there is **no printf-style/variadic form at
all**, so a call site can never pass a format string plus trailing
substitution arguments the way `std::fprintf(stderr, "...%s...", x)` does.
`category` must be a quoted string literal — every real call site in this
engine today (`Application.cpp`'s `"Application"`, `NetworkServer.cpp`'s
`"Network"`, `JobContinuation.cpp`'s `"Jobs"`) passes one; a bare, unquoted
identifier like `RenderGraph` does not name anything in scope here and fails
to compile. `message` must already be ONE fully-built `std::string` (or a
`const char*` literal) by the time it reaches the macro — dynamic values are
spliced in via ordinary `operator+`/`std::to_string()` concatenation, exactly
like `NetworkServer.cpp`'s own real
`GTE_LOG_ERROR("Network", "failed to bind " + std::string(kBindHost) + ":" +
std::to_string(port) + " - network endpoint disabled this run.")` call site
— never a `%s`/`%u`-style format string.

**Note for whoever implements this phase**: as of this writing,
`GTE_LOG_WARNING` specifically has **zero real call sites anywhere in this
engine yet** — only `GTE_LOG_INFO`/`GTE_LOG_ERROR` have shipped precedent (in
the two files named above), so grepping specifically for `GTE_LOG_WARNING(`
elsewhere in `src/` finds nothing; this phase's own call site is the first
real one. Follow `GTE_LOG_ERROR`'s/`GTE_LOG_INFO`'s identical-shape precedent
instead — all four `GTE_LOG_*` macros share the exact same 2-argument,
non-variadic, `Logger::Log()`-forwarding shape, so their real call sites are
equally valid precedent for this one.

**This is the one and only place `std::cout`/raw un-gated `fprintf` must
NOT be used** — per PHASE0's Locked Design Decision 8, this must go through
`GTE_LOG_WARNING` so it is fetchable via `GET /get_logs`, exactly like every
other engine diagnostic.

### 3.3 — `RenderGraphSnapshot`: a real, structural flag

In `src/Renderer/RenderGraph/RenderGraphSnapshot.h`, add a field to
`RenderGraphSnapshot` itself (not `RenderGraphPassSnapshot` — this is a
REGIME-wide fact, not a per-pass one):
```cpp
// PHASE1 (render-pass-6 campaign, item 2.4) - true if THIS regime's fixed
// GPU-timing slot budget (RenderGraph::kSynchronousTimingSlotBudget /
// kPipelinedTimingSlotBudget) was already fully assigned to other pass
// names at least once during the Execute() call that produced this
// snapshot - i.e. at least one surviving pass this call could not be
// assigned a timing slot at all, and its own PassGpuStats::timing will
// permanently read Status::Absent for as long as this remains true. The
// Editor's "Render Graph" panel should surface this structurally (e.g. a
// warning banner) rather than leaving a reader to infer it from an
// individual pass's timing quietly never updating.
bool timingSlotBudgetExhausted = false;
```
`BuildRenderGraphSnapshot()` (`RenderGraphSnapshot.cpp`) needs a way to know
this fact — its REAL, current signature (confirmed directly against
`RenderGraphSnapshot.h`) is:
```cpp
RenderGraphSnapshot BuildRenderGraphSnapshot(const CompiledGraph& compiled, const CompiledGraphInput& input,
    const std::function<PassGpuStats(const char*)>& statsLookup);
```
Add one new, **trailing, DEFAULTED** `bool` parameter,
`bool timingSlotBudgetExhausted = false`, threaded through from
`RenderGraph.cpp`'s call site (line 663), which already has direct access to
`timingSlots.AssignedCount() >= timingSlots.SlotBudget()` (a simple boolean
check against the SAME `RenderGraphNameSlotTable` instance already in
scope) — this does NOT require plumbing `JustOverflowed()`'s one-shot state
through the snapshot; it's simpler and equally correct to just check "is
this table currently at 100% capacity" at snapshot-build time, since that
is exactly the persistent condition the flag is meant to describe.

**The default value is required, not a stylistic nicety.** Confirmed by
direct grep: `tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp` calls
`BuildRenderGraphSnapshot(compiled, input, ...)` with exactly 3 arguments at
**15 separate existing call sites** — an un-defaulted 4th parameter would
fail every one of them at compile time, directly contradicting this phase's
own "zero change to any other behavior" goal (Step 1). Mirror this
codebase's own established "trailing defaulted parameter never breaks an
existing call site" convention (e.g. `RenderGraphBuilder::AddRenderPass()`'s
own trailing, defaulted `drawKind`/`renderPassEvent` parameters) — defaulting
to `false` means every one of those 15 existing calls (which never observed
this flag before, and should keep behaving exactly as before) keeps
compiling and passing unmodified, and only the ONE new test case this phase
adds (3.4 below) needs to pass `true` explicitly.

### 3.4 — Tier-1 tests (required, per `AGENTS.md`'s "Testability &
Regression Safety" rule: every Tier-1 code change needs a matching test
change)

- `tests/Renderer/RenderGraph/RenderGraphNameSlotTableTests.cpp`: add a case
  asserting `JustOverflowed()` is `false` immediately after construction,
  `false` after every successful `AssignOrGetSlot()` call (new name, still
  under budget), `false` for a `nullptr` name, `false` for a name that
  already has a slot (re-querying an existing name never "overflows"), and
  `true` exactly on the call that first returns `kNoNameSlot` for a
  genuinely new name once the budget is already full — plus a case
  confirming a SUBSEQUENT call (even for a different new name, still over
  budget) is ALSO `true` (every over-budget call reports true — the "only
  log once" de-duplication lives in `RenderGraph`, not in this class).
- `tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp`: add a case
  asserting `BuildRenderGraphSnapshot()`'s new `timingSlotBudgetExhausted`
  parameter passes straight through to `RenderGraphSnapshot::
  timingSlotBudgetExhausted` unchanged, for both `true` and `false` inputs.
- No new GPU-dependent (Tier 2) test is required or expected — this stays
  entirely within already-Tier-1-testable files.

### 3.5 — Manual/live verification (encouraged, not gating)

Once compiling, this phase MAY be sanity-checked live (never required to
pass before moving to PHASE2, per PHASE0's "only PHASE7 runs full build/
ctest" rule): run the engine via `run_app_background`, drive a scene through
`gte_send_request` (e.g. spawn enough distinct-named passes to force an
artificial overflow is impractical without changing the constants — since
this campaign deliberately does NOT bump them, real-world overflow at 16/8
named passes is unlikely to be reproducible live today; a targeted, throwaway
manual test — e.g. temporarily constructing a `RenderGraphNameSlotTable`
with a tiny budget in a scratch context — is an acceptable substitute for
live verification here, or simply rely on the Tier-1 tests above as the
actual proof). Fetch `GET /get_logs` to confirm log formatting looks
reasonable if a live overflow IS reproduced.

### 3.6 — What NOT to do in this phase

- Do not touch `kSynchronousTimingSlotBudget`/`kPipelinedTimingSlotBudget`
  (Locked Design Decision 3).
- Do not touch `ExecuteCompiledGraph()`'s per-pass loop structure beyond the
  small addition in 3.2 — the larger extraction is PHASE2's job, not this
  phase's.
- Do not touch `PassContext` — that is PHASE3's job.

## Definition of Done for this phase

- `RenderGraphNameSlotTable::JustOverflowed()` exists, is Tier-1-tested, and
  correctly distinguishes a genuine overflow from every other
  `kNoNameSlot`-returning case.
- `RenderGraph.cpp` logs a real, one-time-per-name-per-regime
  `GTE_LOG_WARNING` the first time a pass name overflows its regime's
  timing-slot budget, fetchable via `GET /get_logs`.
- `RenderGraphSnapshot::timingSlotBudgetExhausted` exists, is wired through
  `BuildRenderGraphSnapshot()`, and is Tier-1-tested.
- A fast, targeted incremental compile check passes (no full build/ctest
  required this phase).
- `PHASE1_COMPLETION_REPORT.md` is written, next to this file, summarizing
  what was actually done, the exact `GTE_LOG_WARNING` category/message
  wording chosen, and any deviation from this plan (with reasoning).
- Changes are committed via `git_add`/`git_commit`.

**Remember**: use `ask_questions` for any genuine ambiguity this document
doesn't already resolve (e.g. the exact `GTE_LOG_WARNING` macro signature,
or `BuildRenderGraphSnapshot()`'s exact current parameter list) — do not
guess silently on anything that changes an actual public signature.
