# PHASE4 — Completion Report: The Auto-Wire Helper (`TryAutoWireRegisterCall()`)

Parent: `PHASE0_MASTER_STRATEGY.md`. Phase file:
`PHASE4_AUTO_WIRE_HELPER_TRY_AUTO_WIRE_REGISTER_CALL.md`.

## What changed

Two brand-new files, plus two hand-maintained CMake list edits, exactly as the
phase file's own Step 3.1 mandates:

- **`src/Editor/ScreenPassAutoWire.h`** — declares
  `bool TryAutoWireRegisterCall(const std::filesystem::path& projectGameCppPath, const std::string& registerFunctionName);`
  inside `namespace gte`, with EXTERNAL linkage (a real header, not inside
  `EditorProjectLifecycleCapability.cpp`'s anonymous namespace).
- **`src/Editor/ScreenPassAutoWire.cpp`** — the real implementation (see
  "Discovered deviations" below for the two load-bearing corrections made to
  the phase file's own assumed algorithm, both confirmed via `ask_questions`
  before writing any code).
- **`CMakeLists.txt`** — both new files added to the explicit, non-glob
  `gte_editor` source list, immediately after
  `src/Editor/EditorProjectLifecycleCapability.cpp`'s own existing entry
  (confirmed exact insertion point by reading the file fresh, around line
  1096, before editing).
- **`tests/CMakeLists.txt`** — the new test file added to the explicit,
  non-glob test-source list, immediately after
  `Editor/AssetScaffoldTemplateTests.cpp`'s own existing entry (confirmed
  exact insertion point by reading the file fresh, around line 2476, before
  editing).
- **`tests/Editor/ScreenPassAutoWireTests.cpp`** — a new, dedicated Tier-1
  test file, `#include`-ing `Editor/ScreenPassAutoWire.h` directly and calling
  `TryAutoWireRegisterCall()` by name against real temp files, covering all 9
  required scenarios from the phase file's own Step 4 (see "Test coverage"
  below).

`EditorProjectLifecycleCapability.cpp` itself was **not** touched this phase —
per the phase file's own Step 3.1, wiring the new `#include` into that file is
explicitly PHASE6's job (this function is not yet called by any real dispatch
path).

## Discovered deviations from this phase file's own written Step 3.1/3.2/3.3
text — both confirmed via `ask_questions` before implementing

Before writing any implementation code, I re-read PHASE2's own
ALREADY-COMMITTED, real template output (`EditorProjectLifecycleCapability.cpp`'s
`BuildGameStubCppContent()`, lines ~59-94 today) to confirm the exact anchor
text this phase's Step 2 search logic needs to match against. It does **not**
match what this phase file's own Step 3.1/3.2 quote:

1. **The phase file assumes each anchor is a single physical output line
   ending in a period** (e.g.
   `"// GTE_AUTO_REGISTER_FORWARD_DECLARATIONS - do not remove or edit this line."`).
   The real, already-shipped template wraps each anchor's own explanatory
   sentence across a SEVEN-physical-line comment block instead — the
   forward-declarations anchor's own first line reads
   `// GTE_AUTO_REGISTER_FORWARD_DECLARATIONS - do not remove or edit this`
   (no trailing "line." — that word starts the NEXT physical line,
   `// line. The Editor's "Create" scaffolding tools ...`), and the body
   anchor's own first line reads
   `    // GTE_AUTO_REGISTER_ANCHOR - do not remove or edit this line. The`
   (extra trailing prose AFTER the period, continuing onto the next line too).
   A literal whole-line-equality (or even whole-sentence substring) search for
   either phase-file-quoted sentence would therefore **never** match any real
   generated file — permanently, silently breaking this entire feature for
   every future project, the opposite of the intended "safely refuses on an
   old project" behavior.

   **User-confirmed fix**: search for the bare, unique keyword TOKEN instead
   of the full quoted sentence — `"GTE_AUTO_REGISTER_FORWARD_DECLARATIONS"` /
   `"GTE_AUTO_REGISTER_ANCHOR"` — which each appear exactly once, on their own
   comment line, nowhere else in a real generated file. Still a plain
   `std::string::find()`-based search, never `<regex>`, per Step 2's own
   constraint.

2. **Given each anchor is a genuine multi-line comment block, WHERE exactly
   to insert the new code line was also ambiguous.** The phase file's own
   Step 3.2 items 4/5 say "insert on the line immediately following the
   anchor line" — literally following just the marker's own first physical
   line would land the new code line in the MIDDLE of the wrapped comment
   paragraph (mechanically consistent with the phase file's wording if "the
   anchor line" = the marker's own line, but visually ugly and confusing to a
   human reading the generated file).

   **User-confirmed fix (Option B)**: insert immediately after the WHOLE
   anchor comment block ends — scan forward from the marker's own line,
   skip every subsequent line that is ALSO a comment line (trimmed content
   starts with `"//"`), and insert right before the first non-comment line
   found (the blank line before `namespace {` for the forward-declaration
   anchor; the closing `}` for the body anchor). This matches the anchor
   comment's own human-readable text read as referring to the whole note
   ("insert...directly BELOW this line" / "call directly ABOVE this
   comment"), not one literal physical line, and produces clean, readable
   generated output.

Both fixes are implemented in `FindLineContaining()` (bare-token search) and
`FindInsertionIndexAfterAnchorBlock()` (comment-block-boundary scan) in
`ScreenPassAutoWire.cpp` — see that file's own header comment for the same
writeup, restated at the point it actually matters for a future reader who
only opens that one file.

**Everything else in the phase file's own Step 3 was followed exactly as
written**: whole-file slurp via `std::ifstream` (no `<regex>` anywhere), the
idempotency guard skipping any line whose trimmed content starts with `"//"`
before checking for `registerFunctionName + "(core);"`, the
`"void " + registerFunctionName + "(gte::Core& core);"` /
`"    " + registerFunctionName + "(core);"` exact line text, resolving BOTH
insertion indices from the ORIGINAL (pre-insert) lines vector and inserting at
the LARGER index first (generalized as "whichever computed insertion index is
larger," since in the real template the forward-declarations anchor always
sits above the body anchor, but the code compares the two resolved indices
rather than assuming that ordering), a complete `std::ofstream` overwrite, and
`GTE_LOG_ERROR("ScreenPassAutoWire", ...)` on a write failure while still
returning `false` to the caller.

## Test coverage (Step 4's own 9 required scenarios)

All 9 scenarios from the phase file's own Step 4 checklist are covered in
`tests/Editor/ScreenPassAutoWireTests.cpp`, each constructing a real temporary
scratch file via `std::filesystem::temp_directory_path()`
(`TempScratchFile`, mirroring `AssetScaffoldTemplateTests.cpp`'s own
`TempScratchProjectDirectory` shape for a single file instead of a whole
`Assets/` folder):

1. `BothAnchorsPresentInsertsBothLinesCorrectlyPositioned` — both new lines
   present exactly once each, correctly positioned immediately after each
   anchor's own whole comment block (verified via exact adjacent-substring
   checks), every original anchor/line surviving, and the file's own total
   line count growing by exactly 2.
2. `ForwardDeclAnchorMissingReturnsFalseFileUnchanged`.
3. `BodyAnchorMissingReturnsFalseFileUnchanged`.
4. `BothAnchorsMissingReturnsFalseFileUnchanged`.
5. `IdempotentSecondCallForSameNameWritesNothingNew` — second call's own
   post-write content compared byte-for-byte against the first call's own
   post-write content.
6. `CommentedOutCallTreatedAsNotWiredInsertsFreshActiveCall` — a
   manually-commented `// RegisterFooScreenPass(core);` line survives
   untouched AND a fresh, active, uncommented call line is inserted alongside
   it (exactly 2 total occurrences of the bare call substring).
7. `TwoDifferentNamesCalledInSequenceBothPresentAndCorrectlyPositioned` — two
   sequential calls with two different `registerFunctionName` values against
   the same file; both forward declarations and both call lines present
   exactly once each, neither duplicated — the concrete regression proof for
   the "second insertion point resolution thrown off by the first" hazard
   (each call re-reads the file fresh from disk, so this is exercised for
   real, not merely reasoned about).
8. `CorruptedBinaryGarbageInputFailsSafelyNeverCrashes` — raw non-UTF8 bytes
   (`\x00\x01\xFF\xFE\x80\x81 ...`), no anchor text at all: fails safely,
   unchanged, no crash/throw.
9. `NonExistentFileReturnsFalse` — a made-up path under the temp directory
   that was never created.

## Step 5: Verification (this phase only)

1. **Incremental build** (`cmake --build build`, working directory
   `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`) — succeeded, 10/10
   steps, zero errors, zero new warnings:
   `ScreenPassAutoWire.cpp` compiled into `gte_editor`,
   `ScreenPassAutoWireTests.cpp` compiled into
   `GreatTamanaEngineTests.exe`, `GreatTamanaEditor.exe`/
   `ProjectAssemblyProbe_Editor.dll`/`ProjectAssemblyProbe_Game.dll` all
   relinked cleanly.
2. **Targeted test run**: `ctest -R ScreenPassAutoWire --output-on-failure`
   from `build/` — **9/9 tests passed** (test IDs 85-93,
   `ScreenPassAutoWireTest.*`), 0.78s total:
   - `BothAnchorsPresentInsertsBothLinesCorrectlyPositioned`
   - `ForwardDeclAnchorMissingReturnsFalseFileUnchanged`
   - `BodyAnchorMissingReturnsFalseFileUnchanged`
   - `BothAnchorsMissingReturnsFalseFileUnchanged`
   - `IdempotentSecondCallForSameNameWritesNothingNew`
   - `CommentedOutCallTreatedAsNotWiredInsertsFreshActiveCall`
   - `TwoDifferentNamesCalledInSequenceBothPresentAndCorrectlyPositioned`
   - `CorruptedBinaryGarbageInputFailsSafelyNeverCrashes`
   - `NonExistentFileReturnsFalse`
3. **No live HTTP/UI verification** — correct, per the phase file's own Step
   5 item 3: this function is not yet called by any real dispatch path
   (PHASE6's job).

## On the optional `delegate_task(position: "next")` self-double-check
(Locked Decision 14)

Considered and deliberately **not** invoked this phase. The actual diff is
small and contained (2 new files + 2 mechanical CMake-list insertions, no
existing production call site touched), and — given `delegate_task` cannot
return synchronous results back into this same turn to inform the very
completion report it would need to precede — a self-review was instead
performed directly, in-line, by: (a) re-deriving the real anchor text
character-by-character from the actual committed template before writing any
code (catching both discovered deviations above BEFORE they could become
silent bugs), (b) confirming both fixes with the user via `ask_questions`
rather than guessing, (c) writing 9 dedicated regression tests against
REALISTIC (not simplified) multi-line anchor content mirroring the real
template exactly, and (d) a clean, all-green incremental build + targeted
`ctest` run. This combination already exceeds the bar a `position: "next"`
sub-agent review would independently re-establish for a diff this size.

## Files touched

- `src/Editor/ScreenPassAutoWire.h` (new)
- `src/Editor/ScreenPassAutoWire.cpp` (new)
- `CMakeLists.txt` (new file-pair entry in the `gte_editor` source list)
- `tests/CMakeLists.txt` (new test-file entry)
- `tests/Editor/ScreenPassAutoWireTests.cpp` (new)
- `task_manager/editor-core-separation-24/PHASE4_COMPLETION_REPORT.md` (this
  file)

No full clean build / full `ctest` regression pass was run (Locked Decision
2, `PHASE0_MASTER_STRATEGY.md` — that is PHASE8's own job).
