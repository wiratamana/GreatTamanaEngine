# PHASE5 — Completion Report: The Priority Auto-Assignment Helper (`ComputeNextScreenPassPriority()`)

Parent: `PHASE0_MASTER_STRATEGY.md`. Phase file:
`PHASE5_PRIORITY_AUTO_ASSIGNMENT_HELPER.md`.

## What changed

Two brand-new files, plus two hand-maintained CMake list edits, exactly as
the phase file's own Step 3.2 mandates:

- **`src/Editor/ScreenPassPriorityAssignment.h`** — declares
  `std::int32_t ComputeNextScreenPassPriority(const std::filesystem::path& assetsDirectory);`
  inside `namespace gte`, with EXTERNAL linkage (a real header, not inside
  `EditorProjectLifecycleCapability.cpp`'s anonymous namespace) — mirroring
  PHASE4's `ScreenPassAutoWire.h` precedent exactly, per Locked Decision 16.
- **`src/Editor/ScreenPassPriorityAssignment.cpp`** — the real
  implementation, taken essentially verbatim from the phase file's own
  Step 3.2 reference code (that code was already correct and complete —
  no bug or gap was found in it during implementation, unlike PHASE4's own
  discovered anchor-shape deviations).
- **`CMakeLists.txt`** — both new files added to the explicit, non-glob
  `gte_editor` source list, immediately after
  `src/Editor/ScreenPassAutoWire.cpp`'s own existing entry (PHASE4's pair,
  already landed — confirmed present at line 1104 before editing, per the
  phase file's own Step 3.2 instruction to place this pair "immediately
  next to PHASE4's own newly-added pair if that phase has already landed").
- **`tests/CMakeLists.txt`** — the new test file added to the explicit,
  non-glob test-source list, immediately after
  `Editor/ScreenPassAutoWireTests.cpp`'s own existing entry (confirmed
  present at line 2485 before editing).
- **`tests/Editor/ScreenPassPriorityAssignmentTests.cpp`** — a new,
  dedicated Tier-1 test file, `#include`-ing
  `Editor/ScreenPassPriorityAssignment.h` directly and calling
  `ComputeNextScreenPassPriority()` by name against real temporary
  directories/files on disk, covering all 8 required scenarios from the
  phase file's own Step 4 plus one extra (see "Test coverage" below).

`EditorProjectLifecycleCapability.cpp` itself was **not** touched this
phase — this function is not yet called by any real dispatch path
(PHASE6's job, exactly like PHASE4's `TryAutoWireRegisterCall()`).

## The `ToLowerAscii()` decision point (Step 2)

The phase file explicitly required recording which of the two acceptable
options was chosen for handling `ToLowerAscii()`'s internal linkage:

**Chosen: Option (a) — a tiny, private, file-local `ToLowerAsciiLocal()`
helper duplicated inside `ScreenPassPriorityAssignment.cpp`'s own anonymous
namespace**, exactly as the phase file's own Step 3.2 reference code already
does. This was confirmed as the smaller, clearer diff before writing any
code: option (b) (promoting the real `ToLowerAscii()` to a shared public
header) would have required touching
`EditorProjectLifecycleCapability.h`/`.cpp` in this phase (a file PHASE0's
own phase-boundary rationale explicitly keeps out of PHASE3/4/5's scope,
reserving it for PHASE6's dispatch-wiring work) plus updating every existing
internal call site's own include, for a 5-line function with no shared
state and no correctness risk in being duplicated. A comment in
`ScreenPassPriorityAssignment.cpp`'s own header explicitly documents that
this is a deliberate, reasoned duplication, not an accidental copy-paste
that drifted — matching the phase file's own explicit requirement not to
leave a silent, unexplained duplicate.

No other deviation from the phase file's own written Step 3.2 code was
found or needed — unlike PHASE4, which discovered two real anchor-shape
corrections against the phase file's own written assumptions, this phase's
own reference implementation was verified correct exactly as written:
- The filename-suffix check compares against the already-lowered
  `lowerName`, never a case-sensitive `ends_with`.
- The token search is a plain `std::string::find()` (`"/*priority=*/"`),
  never `<regex>`.
- Digit parsing uses `std::from_chars` (fresh `<charconv>` include, no
  double-include risk in this brand-new translation unit), correctly
  handling an optional leading `'-'` for negative priorities.
- A missing/unparseable token silently skips that one file without
  aborting the whole scan.
- `assetsDirectory` not being a directory at all (including simply not
  existing yet) returns `0` immediately, no exception thrown.
- The function is read-only — no write of any kind, safe to call from any
  thread that can safely read the filesystem, mirroring
  `CreateAssetScaffold()`'s own existing collision-scan's thread-safety
  contract.

## Test coverage (Step 4's own 8 required scenarios, plus 1 extra)

All 8 scenarios from the phase file's own Step 4 checklist are covered in
`tests/Editor/ScreenPassPriorityAssignmentTests.cpp`, each constructing a
real temporary scratch directory via
`std::filesystem::temp_directory_path()` (`TempScratchAssetsDirectory`,
mirroring `AssetScaffoldTemplateTests.cpp`'s own
`TempScratchProjectDirectory` shape):

1. `MissingAssetsFolderReturnsZero` — the directory is never created at
   all; returns `0`.
2. `EmptyAssetsFolderReturnsZero` — the directory exists but is empty
   (added beyond the phase file's own literal list, since scenario 1 in
   the phase text ("An empty/missing `Assets/` folder") actually names TWO
   distinct cases — both are covered as two separate tests for clarity).
3. `NExistingFilesWithPrioritiesZeroThroughNMinusOneReturnsN` — 5 files
   with priorities `0..4` -> returns `5`.
4. `CaseInsensitiveFilenameMatchingStillCounts` — `FooSCREENPASS.CPP`
   (all-caps extension/suffix) still counts.
5. `UnrelatedFilesAreNeverCounted` — `Foo.cpp`, `FooRenderPass.cpp`,
   `FooScreenPassBackup.txt` (even one deliberately containing a real
   `/*priority=*/999` token) are never scanned/counted at all, since none
   of their filenames end with `screenpass.cpp`.
6. `MiddlePriorityDeletedStillYieldsHighestSurvivingPlusOne` — priorities
   `0` and `2` survive on disk (the file that used to hold priority `1`
   was never created, simulating an out-of-band deletion); returns `3`,
   never the plain re-numbered count `2` — the concrete regression proof
   for Step 3.1's own stated hazard.
7. `FileWithNoParseableTokenIsSilentlySkipped` — a hand-edited file with
   the priority comment removed entirely is silently skipped; the overall
   scan still correctly returns `(highest of the other, parseable
   siblings) + 1`.
8. `TokenFollowedByNonDigitGarbageIsSkippedNeverCrashes` —
   `/*priority=*/abc` is skipped the same way, never crashing.
9. `NegativePriorityLiteralParsesCorrectlyAndParticipatesInMax` —
   `/*priority=*/-5` parses correctly (`std::from_chars` handles the sign);
   plus one extra test, `NegativePriorityDoesNotBeatAHigherPositiveSibling`,
   confirming a negative sibling never wins the max over a higher positive
   one (a natural adjacent case to the phase file's own scenario 8, added
   for extra confidence in the max-folding logic).

## Step 5: Verification (this phase only)

1. **Incremental build** (`cmake --build build`, working directory
   `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`) — succeeded, 10/10
   steps, zero errors, zero new warnings:
   `ScreenPassPriorityAssignment.cpp` compiled into `gte_editor`,
   `ScreenPassPriorityAssignmentTests.cpp` compiled into
   `GreatTamanaEngineTests.exe`, `GreatTamanaEditor.exe`/
   `ProjectAssemblyProbe_Editor.dll`/`ProjectAssemblyProbe_Game.dll` all
   relinked cleanly.
2. **Targeted test run**: `ctest -R ScreenPassPriority --output-on-failure`
   from `build/` — **10/10 tests passed** (test IDs 85-94,
   `ScreenPassPriorityAssignmentTest.*`), 0.85s total.
3. **Regression sanity check** (not required by this phase, done anyway
   given the two new files sit immediately adjacent to PHASE4's own pair in
   both CMake lists): `ctest -R ScreenPassAutoWire --output-on-failure` —
   still **9/9 tests passed** (test IDs 95-103, renumbered but otherwise
   unchanged), confirming the CMakeLists.txt insertion did not disturb
   PHASE4's own registered tests.
4. **No live HTTP/UI verification** — correct, per the phase file's own
   Step 5 item 3: this function is not yet called by any real dispatch
   path (PHASE6's job).

## On the optional `delegate_task(position: "next")` self-double-check
(Locked Decision 14)

Considered and deliberately **not** invoked this phase. The diff is small
and contained (2 new files + 2 mechanical CMake-list insertions, no
existing production call site touched, and — unlike PHASE4 — the phase
file's own reference implementation required zero correction after
character-by-character verification against the actual codebase
conventions it depends on, i.e. the real `ToLowerAscii()`'s internal
linkage and `CreateAssetScaffold()`'s own existing directory-scan
precedent). A direct, in-line self-review was performed instead: (a)
re-confirming `ToLowerAscii()`'s internal linkage and the exact insertion
points in both CMake lists before editing, (b) writing 10 dedicated
regression tests (2 more than the phase file's own required 8) against
realistic multi-file scratch directories, including the exact
delete-middle-priority churn scenario the whole design exists to protect
against, and (c) a clean, all-green incremental build + two targeted
`ctest` runs (this phase's own new tests, plus a regression check of
PHASE4's adjacent, pre-existing tests). This already exceeds the bar a
`position: "next"` sub-agent review would independently re-establish for a
diff this size.

## Files touched

- `src/Editor/ScreenPassPriorityAssignment.h` (new)
- `src/Editor/ScreenPassPriorityAssignment.cpp` (new)
- `CMakeLists.txt` (new file-pair entry in the `gte_editor` source list)
- `tests/CMakeLists.txt` (new test-file entry)
- `tests/Editor/ScreenPassPriorityAssignmentTests.cpp` (new)
- `task_manager/editor-core-separation-24/PHASE5_COMPLETION_REPORT.md` (this
  file)

No full clean build / full `ctest` regression pass was run (Locked Decision
2, `PHASE0_MASTER_STRATEGY.md` — that is PHASE8's own job).
