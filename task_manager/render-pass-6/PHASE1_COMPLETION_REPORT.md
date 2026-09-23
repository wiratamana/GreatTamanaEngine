# PHASE1 — Completion Report: Slot-Budget Overflow Diagnostic (item 2.4)

## Parent

`PHASE0_MASTER_STRATEGY.md` / `PHASE1_SLOT_BUDGET_OVERFLOW_DIAGNOSTIC.md`.

## What was done

Implemented exactly the plan in `PHASE1_SLOT_BUDGET_OVERFLOW_DIAGNOSTIC.md`,
Option A (the preferred, smallest-diff shape), with no deviation from the
locked design decisions.

### 1. `RenderGraphNameSlotTable` (`src/Renderer/RenderGraph/RenderGraphNameSlotTable.h`)

- Added a private `bool m_lastCallOverflowed = false;` member.
- `AssignOrGetSlot()` now explicitly sets this flag on every return path:
  `false` for a `nullptr` name, `false` for an already-assigned name, `true`
  only in the budget-exhausted branch (a genuinely new name with the budget
  already fully spent), `false` after a fresh successful assignment.
- Added the new public query method:
  ```cpp
  bool JustOverflowed() const noexcept { return m_lastCallOverflowed; }
  ```
- Zero change to `AssignOrGetSlot()`'s signature or return type — every
  existing call site (`RenderGraph.cpp`, the pre-existing test file) compiles
  and behaves unmodified.

### 2. `RenderGraph` (`src/Renderer/RenderGraph/RenderGraph.h` / `.cpp`)

- Added `#include "../../Editor/Logger.h"` to `RenderGraph.cpp`, right after
  the existing `#include "../Renderer.h"` line — mirroring the exact
  relative-path convention already established by `JobContinuation.cpp`/
  `NetworkServer.cpp`.
- Added two new private members to `RenderGraph.h`:
  ```cpp
  std::vector<const char*> m_reportedSynchronousOverflows;
  std::vector<const char*> m_reportedPipelinedOverflows;
  ```
  tracking which pass names have already had their overflow reported, per
  regime, for this process's entire lifetime.
- In `ExecuteCompiledGraph()`, immediately after the existing
  `const std::int32_t timingSlot = timingSlots.AssignOrGetSlot(pass.name);`
  line, added the one-time, name-keyed overflow report exactly as specified
  in the plan (3.2), using `GTE_LOG_WARNING` (never `std::cout`/raw
  `fprintf`).
- **Chosen `GTE_LOG_WARNING` category/message wording** (byte-for-byte, as
  actually shipped):
  - Category: `"RenderGraph"`.
  - Message (with `{name}`/`{regime}`/`{budget}` substituted):
    `Pass "{name}" could not be assigned a GPU-timing slot - the {regime}
    regime's fixed timing-slot budget ({budget}) is already fully assigned to
    other pass names. This pass's GPU timing will read as Absent until this
    budget is increased.`
  - `{name}` is `pass.name` or `"<unnamed>"` if null; `{regime}` is
    `"pipelined"`/`"synchronous"`; `{budget}` is
    `timingSlots.SlotBudget()`.
- At the existing `BuildRenderGraphSnapshot()` call site (end of
  `ExecuteCompiledGraph()`), computed
  `const bool timingSlotBudgetExhausted = timingSlots.AssignedCount() >=
  timingSlots.SlotBudget();` and passed it as the new 4th argument — exactly
  the "check current capacity at snapshot-build time" approach the plan
  recommended as simpler than threading `JustOverflowed()`'s one-shot state
  through.

### 3. `RenderGraphSnapshot` (`src/Renderer/RenderGraph/RenderGraphSnapshot.h` / `.cpp`)

- Added `bool timingSlotBudgetExhausted = false;` to the `RenderGraphSnapshot`
  struct itself (not `RenderGraphPassSnapshot` — a regime-wide fact, per the
  plan).
- `BuildRenderGraphSnapshot()` gained one new, trailing, **defaulted**
  parameter: `bool timingSlotBudgetExhausted = false`, copied straight into
  the returned snapshot's new field. The default was required and verified:
  all 15 pre-existing 3-argument call sites in
  `RenderGraphSnapshotTests.cpp` continue to compile and pass unmodified.

### 4. Tier-1 tests (per `AGENTS.md`'s "Testability & Regression Safety" rule)

- `tests/Renderer/RenderGraph/RenderGraphNameSlotTableTests.cpp`: added 6 new
  `JustOverflowed()` cases — immediately-after-construction, after a
  successful new-name assignment, for a `nullptr` name, for re-querying an
  already-assigned name after the budget is full, the exact call that first
  overflows, and a subsequent over-budget call for a different new name (also
  `true`, confirming the "only log once" de-duplication lives in
  `RenderGraph`, not this class).
- `tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp`: added 2 new
  cases — `timingSlotBudgetExhausted` defaults to `false` when omitted, and
  passes through explicit `true`/`false` correctly.

## Verification

- Fast, targeted incremental compile check (no full build, per Locked Design
  Decision 7):
  - `cmake --build build --target gte_core` — succeeds, 0 errors/warnings
    from the touched files.
  - `cmake --build build --target GreatTamanaEngineTests` — succeeds, 0
    errors/warnings from the touched files.
- Ran the new/touched test suites directly (not a full `ctest` pass):
  - `GreatTamanaEngineTests.exe --gtest_filter=RenderGraphNameSlotTableTests.*:RenderGraphSnapshotTest.TimingSlotBudgetExhausted*`
    → 20/20 passed.
  - `GreatTamanaEngineTests.exe --gtest_filter=RenderGraphSnapshotTest.*` →
    24/24 passed (full pre-existing suite in this file, zero regressions).

## Deviations from the plan

None. Option A was used as recommended; the `RenderGraph.h` member shape
(two separate plain `std::vector<const char*>`, one per regime) was chosen
over a single shared regime+name-keyed structure, as explicitly left open by
the plan as "a small implementation-detail choice, not a design one" —
mirrors the existing `m_reportedSynchronousOverflows`/`m_pipelinedTimingSlots`
sibling-pair-per-regime convention already used elsewhere in this same class.

## What was NOT touched (per plan's own "what NOT to do" list)

- `kSynchronousTimingSlotBudget`/`kPipelinedTimingSlotBudget` constants —
  unchanged (Locked Design Decision 3).
- `ExecuteCompiledGraph()`'s overall per-pass loop structure — untouched
  beyond the small, localized addition above (the larger extraction is
  PHASE2's job).
- `PassContext` — untouched (PHASE3's job).

## Live/manual verification

Not performed — per the plan's own 3.5, a genuine live overflow at today's
real pass counts (well under the 16/8 budgets) is not expected to be
reproducible, and is not required as proof; the Tier-1 tests above are the
actual proof, exactly as the campaign's own Definition of Done states.

## Next phase

`PHASE2_EXECUTE_COMPILED_GRAPH_EXTRACTION.md` (item 2.6) — extracting
`ExecuteCompiledGraph()`'s six concerns into named private methods. No blocker
or open question was found that would change PHASE2's plan.
