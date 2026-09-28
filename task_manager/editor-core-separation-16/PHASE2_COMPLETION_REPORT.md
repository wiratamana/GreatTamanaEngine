# PHASE2 — Strict Name Validator & Existing-Route Hardening — COMPLETION REPORT

Status: **DONE**. All Definition-of-Done items satisfied.

## What was actually built

### 1. `IsValidProjectAssemblyIdentifierName()`
New, dependency-free `gte_core`-tier header/source pair,
`src/Core/Plugins/ProjectAssemblyNameValidation.h/.cpp`, implemented
byte-for-byte per the phase file's own code sample:
- Rejects an empty name, a name not starting with a letter/underscore, any
  name containing a character outside `[A-Za-z0-9_]`, and any of Windows'
  reserved device names (`CON`/`PRN`/`AUX`/`NUL`/`COM1`-`COM9`/`LPT1`-`LPT9`,
  case-insensitive).
- Hand-rolled ASCII character scan, no `<regex>`, matching this codebase's
  existing `src/Core/` convention.
- Registered in root `CMakeLists.txt`'s `gte_core` source list, immediately
  after `src/Core/Plugins/ProjectAssemblyBuildRunner.h/.cpp` (confirmed real
  lines 358-359 pre-edit), mirroring that entry's own comment style.

### 2. Tier-1 tests
New file `tests/Core/Plugins/ProjectAssemblyNameValidationTests.cpp`, 10
tests, all passing, implemented byte-for-byte per the phase file's own code
sample (empty/leading-digit/embedded-space/`..`/`/`/`\`, reserved names in
exact and mixed case, genuinely valid names including the one real,
existing project name `"ProjectAssemblyProbe"`, and the
error-message-only-set-on-failure contract). Registered in
`tests/CMakeLists.txt`'s hand-maintained list, immediately after
`Core/Plugins/ProjectAssemblyBuildRunnerSourceRootTests.cpp`.

### 3. LDD-CP3: hardened `ParseProjectNameQuery()`
`src/Network/NetworkRoutes.cpp` now `#include`s the new header and
`ParseProjectNameQuery()`'s body calls
`IsValidProjectAssemblyIdentifierName()` instead of only checking
`nameParam.empty()`, while still preserving the exact, pre-existing
`"'name' query parameter is required"` error string for the empty-string
case specifically (per the phase file's own explicit instruction not to
silently change that string). Every other invalid name now returns the new
validator's own specific reason string instead of falling through
unvalidated.

## Verification performed

- `cmake --build build --target GreatTamanaEngineTests` — clean incremental
  build (CMake glob re-check triggered automatically because
  `CMakeLists.txt`/`tests/CMakeLists.txt` changed), zero errors, zero new
  compiler warnings. Only 3 objects recompiled
  (`ProjectAssemblyNameValidation.cpp`, the new test file,
  `NetworkRoutes.cpp`), confirming no unrelated file was touched.
- `tests\GreatTamanaEngineTests.exe --gtest_filter=ProjectAssemblyNameValidationTest.*`
  — **10/10 new tests passed**.
- Per STEP 3's own required verification: `search_in_dir` for
  `ParseProjectNameQuery` across `tests/` found no test that calls the
  function directly, but 3 test files exercise routes that go through it
  (`ProjectAssemblyHotReloadEndpointsEndToEndTests.cpp` — the real,
  substantial one, using `name=SomeProject` and
  `name=NetworkTestFakeProject`, both of which trivially satisfy the
  stricter rule). Ran that targeted suite:
  `tests\GreatTamanaEngineTests.exe --gtest_filter=*ProjectAssemblyHotReloadEndpoints*`
  — **10/10 passed, unchanged pass/fail outcome** (including both the
  missing-name-param 400 tests and the valid-name-succeeds tests for
  `compile_only`/`hot_reload`/`ledger`).
- Also re-ran PHASE1's own test suite
  (`ProjectAssemblyBuildRunnerSourceRootTest.*:ProjectAssemblyBuildRunnerBackupRestoreTest.*`)
  as an extra regression check since this phase touches the same
  `gte_core` source-list region of `CMakeLists.txt` — **11/11 passed**,
  confirming the source-list edit itself introduced no ordering/duplication
  defect.
- No full `ctest`/full regression run performed, per this campaign's own
  Note 4/5 (only PHASE5 runs that).

## Deviations from the phase file (and why)

None. Every phase-file code sample (the header, the source file, the test
file, the hardened `ParseProjectNameQuery()` body, the include addition)
was implemented exactly as specified. One small mechanical note: the
`edit_line` tool's own auto-dedup safety net removed one leftover duplicate
comment-header line in `CMakeLists.txt` immediately after the first splice
(a byproduct of inserting a new commented block directly between two other
commented blocks) — caught immediately by re-reading the tool's own
returned context, and corrected with a follow-up `edit_line` call restoring
the removed line verbatim before the build was run. The final, built,
tested file is correct and matches the intended source-list shape; flagged
here for transparency, not because it affected the actual shipped result.

## New gaps found (honest)

- None beyond what PHASE0 already documented. `ParseProjectNameQuery()`'s
  own pre-existing test coverage is entirely indirect (via HTTP route
  end-to-end tests, never a direct unit test of the function itself) — this
  was already true before this phase and remains true after it; not a new
  gap this phase introduced.

## Definition of Done — checked

- [x] `IsValidProjectAssemblyIdentifierName()` exists, compiles, and every
      test in `ProjectAssemblyNameValidationTests.cpp` passes (10/10).
- [x] `ParseProjectNameQuery()`'s body now calls the new validator; every
      pre-existing test that exercises it (found via `search_in_dir`) still
      passes, confirmed by an actual targeted run (10/10), not assumed.
- [x] Both new files are registered in their respective `CMakeLists.txt`s.
- [x] Zero new compiler warnings.

Ready for PHASE3.
