# PHASE2 — COMPLETION REPORT: Editor-Owned `FrameDebuggerPassMetadataRecorder`

Campaign: `editor-core-separation-25` ("Core/Editor Separation: PassRecord Debug
Metadata Sink"), Branch: `feature/editor-core-separation`.

## What was read first

- `readme.md`, `AGENTS.md` (full, re-confirmed against current repo state).
- `task_manager/editor-core-separation-25/PHASE0_MASTER_STRATEGY.md` (full).
- `task_manager/editor-core-separation-25/PHASE1_COMPLETION_REPORT.md` (full)
  — confirmed PHASE1 shipped exactly `src/Renderer/RenderGraph/
  RenderGraphDebugMetadataSink.h` (`PassDebugMetadata`,
  `IPassDebugMetadataSink` = 2 pure-virtual methods,
  `IPassDebugMetadataProvider` = 1 pure-virtual method), one new
  `CMakeLists.txt` source-list line, no test file, `PassRecord`/
  `AddRenderPass()` untouched — no clue for continuation beyond "PHASE2
  implements both interfaces" was found.
- `task_manager/editor-core-separation-25/PHASE2_EDITOR_FRAME_DEBUGGER_PASS_METADATA_RECORDER.md`
  (full — this phase's own plan, which specifies the new header's exact
  final content verbatim).
- `src/Renderer/RenderGraph/RenderGraphDebugMetadataSink.h` (the actual
  PHASE1 file, read directly rather than trusting only the completion
  report's own quoted excerpt) — confirmed the class/method signatures the
  new recorder must implement match the phase plan's own worked example
  exactly.
- `src/Editor/EditorGpuMemoryNameOverlay.h` (the closest sibling precedent
  the phase plan names) — confirmed tone/detail level for the new header's
  doc comment.
- `tests/Editor/EditorGpuMemoryNameOverlayTests.cpp` (the closest sibling
  test-file precedent) — confirmed `TEST_F`/`TEST` style, `gtest.h` include
  path, `namespace gte { namespace { ... } }` wrapping convention.
- `src/Renderer/RenderGraph/RenderGraphTypes.h` (`RenderPassCategory`,
  `RenderPassDrawKind`, `RenderPassTagMask`) — confirmed exact enumerator
  names/values so the test file could use genuinely distinct
  category/drawKind/tags combinations per case.
- Root `CMakeLists.txt` and `tests/CMakeLists.txt` around the
  `EditorGpuMemoryNameOverlay` entries — confirmed the exact line numbers
  the phase plan cites (~1225-1226 / ~2410) before editing, per Locked
  Decision 11.

No ambiguity was found beyond what PHASE0/PHASE2 already resolved — the
phase file specifies the new header's content verbatim, so `ask_questions`
was not needed for this phase. No sub-task was delegated (this phase's own
work is small, fully specified by the phase file, and mechanically
verified by 6 passing Tier-1 tests plus direct code inspection — the
delegation rule reserves `delegate_task` for the campaign's own heaviest
phase, PHASE3, which this is not).

## What was done

### 1. New file: `src/Editor/FrameDebuggerPassMetadataRecorder.h`

Written byte-for-byte per the phase plan's Step 3.1 worked example (no
deviation): a single `final` class,
`gte::FrameDebuggerPassMetadataRecorder`, publicly inheriting BOTH
`rg::IPassDebugMetadataSink` and `rg::IPassDebugMetadataProvider`
(`RenderGraphDebugMetadataSink.h`, PHASE1):

- `OnPassDeclared(declarationIndexThisFrame, category, drawKind, tags)` —
  `assert(declarationIndexThisFrame == m_table.size())` then exactly one
  `m_table.push_back(rg::PassDebugMetadata{ category, drawKind, tags })` —
  no find/insert-by-key, no scan, no map lookup of any kind.
- `BeginFrame()` — exactly `m_table.clear()` (clears entries, keeps
  whatever capacity `std::vector` had already reserved from a previous
  high-water mark).
- `QueryPassDebugMetadata(declarationIndex, outMetadata) const` — bounds
  check (`>= m_table.size()` → return `false`, `outMetadata` left
  completely untouched), else copies `m_table[declarationIndex]` into
  `outMetadata` and returns `true`. Never throws.
- `EntryCountForTesting() const noexcept` — plain `m_table.size()`
  accessor, test-only, never called by production code.
- Private state: exactly one member, `std::vector<rg::PassDebugMetadata> m_table;`.

Header-only (no matching `.cpp`), `#include`s only
`../Renderer/RenderGraph/RenderGraphDebugMetadataSink.h` plus
`<cassert>`/`<cstddef>`/`<vector>`. Lives in `namespace gte` (not
`gte::rg`), matching `EditorGpuMemoryNameOverlay`'s own namespace
placement.

### 2. New test file: `tests/Editor/FrameDebuggerPassMetadataRecorderTests.cpp`

Pure Tier-1 — no `RenderGraphBuilder`/`RenderGraph`/live device anywhere,
just hand-fabricated calls against one `FrameDebuggerPassMetadataRecorder`
instance, using plain (non-fixture) `TEST(...)` cases inside
`namespace gte { namespace { ... } }` (mirroring
`RenderPassHonestyCheckerTests.cpp`-style pure-logic tests already in this
same folder, since this class needs no `SetUp()`/`TearDown()` — each test
constructs its own fresh, independent `FrameDebuggerPassMetadataRecorder`).
Exactly the 6 required cases from the phase plan's Step 3.2, each proving
one specific requirement:

| Test | Proves |
|---|---|
| `FreshInstanceStartsEmpty` | A default-constructed recorder has `EntryCountForTesting() == 0` and `QueryPassDebugMetadata(0, out)` returns `false`. |
| `OnPassDeclaredAppendsInDeclarationOrder` | Three sequential `OnPassDeclared(0/1/2, ...)` calls with distinct category/drawKind/tags values each round-trip correctly through `QueryPassDebugMetadata()` at their own index, and `EntryCountForTesting() == 3`. |
| `QueryOutOfRangeIndexReturnsFalseAndLeavesOutputUntouched` | After declaring 2 entries, querying index `2` (exactly at size) and `999` (far out of range) both return `false`, and a sentinel value seeded into `out` beforehand is completely unchanged afterward (confirms `outMetadata` is never written on a failed query). |
| `BeginFrameClearsPreviousEntries` | After declaring 2 entries, `BeginFrame()` drops `EntryCountForTesting()` back to `0` and `QueryPassDebugMetadata(0, out)` now returns `false` — the stale entry never leaks through. |
| `BeginFrameThenRedeclareStartsIndicesFreshAtZero` | Declare 2, `BeginFrame()`, declare exactly 1 new entry at index `0` again — confirms `EntryCountForTesting() == 1` and the NEW value is returned at index 0, never the stale pre-`BeginFrame()` one. |
| `MultipleBeginFrameCyclesNeverLeakBetweenCycles` | Three full declare/`BeginFrame()` cycles in a row (2, then 1, then 3 entries, each cycle using distinct `tags` values 10-32) — each cycle's own query results only ever reflect that cycle's own values, and cycle 2's stale leftover index from cycle 1 (index `1`) correctly returns `false`. This is the closest a Tier-1 test at this phase can get to proving the "two sequential `RenderGraph::Execute()` calls never see each other's stale data" acceptance criterion — the full end-to-end proof through a real `RenderGraphBuilder` is PHASE3's job. |

A `SentinelMetadata()` helper builds a `PassDebugMetadata` value using
enumerators/`tags` deliberately never used by any of the "real" values a
test declares (`RenderPassCategory::FrameDebuggerInternal`,
`RenderPassDrawKind::Blit`, `tags = 0xDEADBEEF`), so "was `out` left
untouched" checks are unambiguous.

### 3. CMake registration

- Root `CMakeLists.txt`, `gte_editor`-tier explicit source list — added
  immediately after the existing `src/Editor/EditorGpuMemoryNameOverlay.h`/
  `.cpp` pair (now lines 1228-1233):
  ```
      src/Editor/EditorGpuMemoryNameOverlay.h
      src/Editor/EditorGpuMemoryNameOverlay.cpp
      # editor-core-separation-25 campaign, PHASE2
      # (PHASE2_EDITOR_FRAME_DEBUGGER_PASS_METADATA_RECORDER.md) - the
      # Editor-owned, header-only implementation of BOTH
      # rg::IPassDebugMetadataSink and rg::IPassDebugMetadataProvider (see
      # src/Renderer/RenderGraph/RenderGraphDebugMetadataSink.h).
      src/Editor/FrameDebuggerPassMetadataRecorder.h
  ```
  Header-only, no `.cpp` line — matches the phase plan's Step 3.1 rationale
  (nothing in it needs out-of-line definitions, mirroring
  `RenderGraphDebugMetadataSink.h` itself from PHASE1).
- `tests/CMakeLists.txt`, explicit test-source list — added immediately
  after the existing `Editor/EditorGpuMemoryNameOverlayTests.cpp` entry
  (now lines 2410-2416):
  ```
          Editor/EditorGpuMemoryNameOverlayTests.cpp
          # editor-core-separation-25 campaign, PHASE2
          # (PHASE2_EDITOR_FRAME_DEBUGGER_PASS_METADATA_RECORDER.md) - pure,
          # Tier-1 test for FrameDebuggerPassMetadataRecorder (zero
          # RenderGraphBuilder/RenderGraph/live-device dependency by design -
          # see src/Editor/FrameDebuggerPassMetadataRecorder.h).
          Editor/FrameDebuggerPassMetadataRecorderTests.cpp
  ```

## Verification performed

1. **Incremental build**: `cmake --build build` (working directory:
   project root). CMake re-configured cleanly (picked up both
   `CMakeLists.txt` changes), then Ninja compiled exactly
   `Editor/FrameDebuggerPassMetadataRecorderTests.cpp.obj` and re-linked
   `GreatTamanaEngineTests.exe` — **zero errors, zero warnings**. No other
   target needed rebuilding, since the new header is not yet `#include`d by
   any production `.cpp` (PHASE3/PHASE4's job) — exactly the expected,
   minimal footprint for a purely-additive, not-yet-wired-in class.
2. **Targeted `ctest`**:
   `cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug -R FrameDebuggerPassMetadataRecorder --output-on-failure`
   — matched and ran exactly the 6 new tests, **100% passing**:
   ```
       Start 320: FrameDebuggerPassMetadataRecorderTest.FreshInstanceStartsEmpty ................. Passed 0.09 sec
       Start 321: FrameDebuggerPassMetadataRecorderTest.OnPassDeclaredAppendsInDeclarationOrder .... Passed 0.08 sec
       Start 322: FrameDebuggerPassMetadataRecorderTest.QueryOutOfRangeIndexReturnsFalseAndLeavesOutputUntouched . Passed 0.08 sec
       Start 323: FrameDebuggerPassMetadataRecorderTest.BeginFrameClearsPreviousEntries ............ Passed 0.08 sec
       Start 324: FrameDebuggerPassMetadataRecorderTest.BeginFrameThenRedeclareStartsIndicesFreshAtZero . Passed 0.08 sec
       Start 325: FrameDebuggerPassMetadataRecorderTest.MultipleBeginFrameCyclesNeverLeakBetweenCycles .. Passed 0.08 sec
       100% tests passed out of 6
   ```
3. **Direct code inspection** confirming `m_table` is a plain
   `std::vector<rg::PassDebugMetadata>` appended to via exactly one
   `push_back()` call inside `OnPassDeclared()` — no `find`, no
   `std::map`/`std::unordered_map`, no key comparison of any kind anywhere
   in the file. This is a literal, later PHASE5/Section-8 acceptance
   checkbox, confirmed correct here so it does not need revisiting.
4. **`git_status`** after all changes:
   ```
   Changes not staged for commit:
       modified:   CMakeLists.txt
       modified:   tests/CMakeLists.txt
   Untracked files:
       src/Editor/FrameDebuggerPassMetadataRecorder.h
       tests/Editor/FrameDebuggerPassMetadataRecorderTests.cpp
   ```
   Exactly the four files this phase was supposed to touch — nothing else
   in the tree differs.

## Scope discipline confirmed

- `PassRecord`, `AddRenderPass()`, `AddPass()`, `AddComputePass()`,
  `BuildRenderGraphSnapshot()`, `ViewScope`, `RenderGraph` — none of these
  were touched. They remain PHASE3/PHASE4's job.
- `src/Renderer/RenderGraph/RenderGraphDebugMetadataSink.h` (PHASE1) was
  not modified — only referenced by `#include`.
- Nothing outside `src/Editor/` (production) and `tests/Editor/` (test)
  was touched, other than the two `CMakeLists.txt` source-list
  registrations.
- No `GTE_ENABLE_EDITOR`/other preprocessor gate was introduced or assumed
  — the Core/Editor boundary here is enforced purely by which static
  library (`gte_core` vs. `gte_editor`) the file compiles into, matching
  this repo's current, documented convention.
- The class is `final`, matching the phase plan exactly (never intended to
  be further subclassed).

## Next phase

PHASE3 (`PHASE3_PASSRECORD_FIELD_MIGRATION_AND_SNAPSHOT_REWIRING.md`) is
the campaign's heaviest phase: removes `category`/`drawKind`/`tags` from
`PassRecord`, widens `ViewScope` to `std::uint8_t` (TR6), wires
`RenderGraphBuilder::AddRenderPass()` to call `OnPassDeclared()` on this
phase's new recorder (via the `IPassDebugMetadataSink*` interface),
rewires `BuildRenderGraphSnapshot()`/`BuildPassSnapshot()` to read through
a caller-supplied metadata lookup instead of `PassRecord` fields directly,
and rewrites the 4 existing `RenderGraphSnapshotTests.cpp` tests that
currently assert on `PassRecord`-backed snapshot data.
