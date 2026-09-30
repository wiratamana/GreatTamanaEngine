# PHASE2 — Editor-Owned `FrameDebuggerPassMetadataRecorder`

Parent: `PHASE0_MASTER_STRATEGY.md` — **MUST READ FIRST**. Also read
`PHASE1_CORE_DEBUG_METADATA_INTERFACES.md` and its own completion report —
this phase implements both interfaces PHASE1 declared.

## Step 1: The Goal

Add ONE new, brand-new, Editor-owned, header-only class,
`src/Editor/FrameDebuggerPassMetadataRecorder.h`, implementing BOTH
`IPassDebugMetadataSink` and `IPassDebugMetadataProvider`
(`RenderGraphDebugMetadataSink.h`, PHASE1) on one concrete object — the
REAL table that used to live permanently on every `PassRecord`, now living
here instead, exactly once per session, cleared-but-capacity-kept once per
fresh graph declaration.

Plus: a brand-new, dedicated Tier-1 test file proving this class's own
contract in isolation, with no `RenderGraphBuilder`/`RenderGraph` involved
at all — pure, hand-fabricated calls to `OnPassDeclared()`/`BeginFrame()`/
`QueryPassDebugMetadata()`.

Nothing outside `src/Editor/` (and this phase's own new test file) is
touched. `RenderGraphBuilder`/`RenderGraph`/`PassRecord` are NOT wired to
this class yet — that is PHASE3 (builder wiring) and PHASE4 (RenderGraph +
Editor startup wiring). This phase only needs to compile and pass its own
tests in isolation.

## Step 2: The Situation

- The source document's own Section 4 gives a complete, concrete worked
  example of this exact class (`FrameDebuggerPassMetadataRecorder`,
  `At()`-based, `std::vector<Metadata>`, dense push_back, clear-but-keep-
  capacity `BeginFrame()`). This phase implements that SAME shape, adapted
  for PHASE0's own Locked Decision 1 (two base interfaces instead of one,
  `QueryPassDebugMetadata()` returning `bool` instead of a throwing `At()`).
- `RenderPassBlackboard::BeginFrame()` (`src/Renderer/RenderGraph/` or
  wherever it currently lives — re-check its exact path before citing it in
  code comments) is the CLEARING-DISCIPLINE precedent this class's own
  `BeginFrame()` mirrors ("clears entries but keeps whatever backing
  storage was already reserved from the previous frame's high-water mark").
  Its LOOKUP shape (scan-by-key) is explicitly NOT what this class mirrors
  — this class's own table is dense, index-addressed, `push_back()`-only,
  with no key comparison of any kind, ever (source doc, Section 4 — read it
  again before implementing, this is the single most load-bearing
  correctness requirement in this whole campaign).
- `src/Editor/EditorGpuMemoryNameOverlay.h` is this file's own closest
  sibling precedent for "a small, focused, Editor-owned class installed
  once at startup as an observer/sink for a Core-owned hook" — read its
  own doc comment once more before writing this phase's new header
  (tone/detail level to match).
- The root `CMakeLists.txt`'s `gte_editor`-tier explicit source list
  already has an entry for `src/Editor/EditorGpuMemoryNameOverlay.h`/`.cpp`
  (confirmed, ~line 1225-1226). This new header's own `.h` line goes
  immediately next to it (header-only — see Step 3.1 below for why no
  `.cpp` is needed).
- `tests/CMakeLists.txt`'s explicit test-source list already has an entry
  for `Editor/EditorGpuMemoryNameOverlayTests.cpp` (confirmed, ~line 2410)
  — this phase's new test file's own line goes immediately next to it, in
  the same `Editor/` grouping.

## Step 3: The Plan

### 3.1 — New file: `src/Editor/FrameDebuggerPassMetadataRecorder.h`

```cpp
#pragma once

#include "../Renderer/RenderGraph/RenderGraphDebugMetadataSink.h"

#include <cassert>
#include <cstddef>
#include <vector>

namespace gte {

// editor-core-separation-25 campaign - "Core/Editor Separation: PassRecord
// Debug Metadata Sink". The REAL implementation of BOTH
// rg::IPassDebugMetadataSink (write) and rg::IPassDebugMetadataProvider
// (read) - see RenderGraphDebugMetadataSink.h's own header comment for the
// full contract each base class promises. Lives entirely under
// src/Editor/ - i.e. compiled exclusively into the separate `gte_editor`
// static library, never `gte_core` (this codebase has no
// `GTE_ENABLE_EDITOR` preprocessor macro anymore - see AGENTS.md's
// "`gte_core` / `gte_editor` Library Separation" section) - only ever
// constructed/installed by Editor-tier startup code (see PHASE4),
// mirroring EditorGpuMemoryNameOverlay's own placement exactly.
//
// PER-FRAME TABLE, NEVER PERMANENT (source doc, Section 4): a PassRecord
// has no stable identity across frames ("a PassRecord is never 'upserted
// by name'; every AddPass() call mints a brand new PassRecord every
// frame" - RenderGraphTypes.h's own PassRecord doc comment), so a
// permanent, ever-growing map would leak forever across a session. This
// class instead CLEARS-BUT-KEEPS-CAPACITY once per fresh graph
// declaration (BeginFrame()), mirroring RenderPassBlackboard::BeginFrame()'s
// own established clearing discipline - but NEVER its scan-by-key LOOKUP
// shape. m_table is a plain, dense, contiguously-indexed std::vector,
// appended to via exactly one push_back() per OnPassDeclared() call, with
// NO key comparison of any kind, ever - a future maintainer who instead
// reaches for RenderPassBlackboard's own scan-by-key container shape would
// still be functionally correct, just needlessly O(n) per declared pass
// and O(n^2) across one graph's whole declaration, for a feature whose
// entire point is to be free when the Editor is compiled out and cheap
// when it isn't.
//
// declarationIndexThisFrame is always exactly == m_table.size() at the
// moment OnPassDeclared() fires - a dense, monotonically increasing
// 0, 1, 2, ... sequence with no gaps, produced in the exact same order
// PassRecord entries themselves are appended to RenderGraphBuilder's own
// m_passes (since OnPassDeclared() is only ever called from inside
// AddRenderPass(), the ONE call site that fires it - AddPass()/
// AddComputePass() never call it at all, see RenderGraphTypes.h's own
// PassRecord doc comment / source doc Section 2 for why those two
// primitives are permanently out of this campaign's scope).
class FrameDebuggerPassMetadataRecorder final : public rg::IPassDebugMetadataSink,
                                                 public rg::IPassDebugMetadataProvider {
public:
    void OnPassDeclared(std::size_t declarationIndexThisFrame, rg::RenderPassCategory category,
        rg::RenderPassDrawKind drawKind, rg::RenderPassTagMask tags) override
    {
        // See this class's own doc comment above - a plain push_back,
        // never a find/insert-by-key. Fires exactly once per pass, from
        // AddRenderPass() alone.
        assert(declarationIndexThisFrame == m_table.size());
        m_table.push_back(rg::PassDebugMetadata{ category, drawKind, tags });
    }

    // Called exactly once per fresh graph declaration, directly by
    // RenderGraph::Execute() itself (never by RenderGraphBuilder, never by
    // a pass author) - see PHASE4 for the exact call site. Clears entries
    // but keeps whatever capacity was already reserved from a previous
    // high-water mark - mirrors RenderPassBlackboard::BeginFrame()'s own
    // discipline, never its lookup shape (see this class's own doc comment
    // above).
    void BeginFrame() override { m_table.clear(); }

    // Defensive, never-throwing read accessor - see
    // IPassDebugMetadataProvider::QueryPassDebugMetadata()'s own doc
    // comment (RenderGraphDebugMetadataSink.h) for the full contract.
    bool QueryPassDebugMetadata(std::size_t declarationIndex, rg::PassDebugMetadata& outMetadata) const override
    {
        if (declarationIndex >= m_table.size()) {
            return false;
        }
        outMetadata = m_table[declarationIndex];
        return true;
    }

    // TEST-ONLY: how many entries this table currently holds - lets a test
    // assert the table's own size directly, without needing to know any
    // particular declarationIndex's value in advance. Never called by
    // production code.
    std::size_t EntryCountForTesting() const noexcept { return m_table.size(); }

private:
    std::vector<rg::PassDebugMetadata> m_table;
};

} // namespace gte
```

Notes on the exact shape above (do not deviate without using
`ask_questions` first):

- `final` — this class is never subclassed, and `RenderGraph`'s own two
  installed pointers (PHASE4) never need virtual dispatch through a
  further-derived type.
- `EntryCountForTesting()` is a genuinely new addition beyond the source
  document's own worked example — added here specifically so this phase's
  own test file (3.2 below) can assert "the table has exactly N entries"
  without reaching into private state or relying only on
  `QueryPassDebugMetadata()`'s own bounds check as an indirect probe. This
  mirrors this codebase's existing `...ForTesting()` naming convention
  (`RenderPassGroupRegistry::ResetPassGroupRegistryForTesting()`,
  `FrameProfiler::ResetForTesting()`).

### 3.2 — New test file: `tests/Editor/FrameDebuggerPassMetadataRecorderTests.cpp`

Pure, Tier-1, no `RenderGraphBuilder`/`RenderGraph`/live device of any
kind — hand-fabricated calls directly against one
`FrameDebuggerPassMetadataRecorder` instance. Required cases (write more
if you find a genuine gap, but these are the minimum, each mapped directly
to a specific correctness requirement from the source document):

1. `FreshInstanceStartsEmpty` — a default-constructed recorder has
   `EntryCountForTesting() == 0`, and `QueryPassDebugMetadata(0, out)`
   returns `false`.
2. `OnPassDeclaredAppendsInDeclarationOrder` — three sequential
   `OnPassDeclared(0, ...)`, `OnPassDeclared(1, ...)`,
   `OnPassDeclared(2, ...)` calls, each with distinct
   category/drawKind/tags values; confirm `EntryCountForTesting() == 3`
   and `QueryPassDebugMetadata(i, out)` returns `true` with the exact
   values passed for each `i` in `{0, 1, 2}`.
3. `QueryOutOfRangeIndexReturnsFalseAndLeavesOutputUntouched` — after
   declaring exactly 2 entries, `QueryPassDebugMetadata(2, out)` and
   `QueryPassDebugMetadata(999, out)` both return `false`; confirm `out`
   was never written (e.g. seed `out` with a known sentinel value before
   the call and confirm it is unchanged after).
4. `BeginFrameClearsPreviousEntries` — declare 2 entries, call
   `BeginFrame()`, confirm `EntryCountForTesting() == 0` and
   `QueryPassDebugMetadata(0, out)` now returns `false` (the STALE entry
   from before `BeginFrame()` must never leak through).
5. `BeginFrameThenRedeclareStartsIndicesFreshAtZero` — declare 2 entries,
   `BeginFrame()`, then declare exactly 1 NEW entry via
   `OnPassDeclared(0, ...)` (index 0 again — this is the correct,
   required call pattern after a fresh `BeginFrame()`, exactly mirroring
   how `RenderGraph::Execute()` will drive this in PHASE4); confirm
   `EntryCountForTesting() == 1` and `QueryPassDebugMetadata(0, out)`
   returns the NEW value, never the stale pre-`BeginFrame()` one.
6. `MultipleBeginFrameCyclesNeverLeakBetweenCycles` — repeat cycle 4/5
   THREE times in a row with different values each cycle, confirming each
   cycle only ever sees its own values — this is the closest a Tier-1
   test in THIS phase can get to proving the "two sequential
   `RenderGraph::Execute()` calls never see each other's stale data"
   acceptance criterion from the source document's own Section 8 (the
   FULL end-to-end proof of that, wired through a real
   `RenderGraphBuilder`, is PHASE3's own job — this phase proves the
   underlying table's own contract in isolation first).

### 3.3 — CMake registration

- Root `CMakeLists.txt`, `gte_editor`-tier explicit source list: add
  `src/Editor/FrameDebuggerPassMetadataRecorder.h` immediately next to the
  existing `src/Editor/EditorGpuMemoryNameOverlay.h`/`.cpp` pair (~line
  1225 — re-check the exact line before editing). Header-only, no `.cpp`
  line.
- `tests/CMakeLists.txt`, explicit test-source list: add
  `Editor/FrameDebuggerPassMetadataRecorderTests.cpp` immediately next to
  the existing `Editor/EditorGpuMemoryNameOverlayTests.cpp` entry
  (~line 2410 — re-check the exact line before editing).

### 3.4 — Verification (this phase)

1. **Incremental compile check**: `cmake --build build` succeeds.
2. **Targeted `ctest`**: `ctest -C Debug -R FrameDebuggerPassMetadataRecorder --output-on-failure`
   (adjust the filter to match whatever Google Test suite name the new
   `TEST(...)` cases actually register under — confirm the exact filter
   string works before trusting it silently matched zero tests).
3. Confirm, by direct code inspection, that `m_table` is a plain
   `std::vector` appended to via `push_back()` only — never a container
   searched/found by key (this is a literal, later Section 8 acceptance
   checkbox).
4. `git_status` to confirm only the new header, the new test file, and
   the two CMakeLists.txt lines changed.

### 3.5 — This phase's own completion report

Write `PHASE2_COMPLETION_REPORT.md` into this same folder covering: the
new header's full final content (or a diff), the new test file's full
list of `TEST(...)` names and what each proves, both CMakeLists.txt lines
added, the exact `ctest` filter used and its output. Commit both the code
change and the report together (`git_add` + `git_commit`).

If you discover ANY ambiguity not already resolved by this file or
`PHASE0_MASTER_STRATEGY.md` — use `ask_questions` before proceeding. If you
delegate any part of this phase to a sub-task, that sub-task MUST also be
instructed to use `ask_questions` for its own ambiguities.
