# PHASE1 — COMPLETION REPORT: SDL Dependency Regression Baseline

## Parent
`PHASE0_MASTER_STRATEGY.md`, plus the original design doc
(`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\Editor_Core_Library_Separation_Design_2026-09-23.md`),
both read in full before starting.

## Status: DONE

No prior `PHASEn_COMPLETION_REPORT.md` files existed in this campaign folder
(confirmed via `browse_dir` before starting) — this genuinely is the first
executed phase, per the task.

## What I did

1. Confirmed the exact current test-binary output path and the existing
   `sdl3_copy_runtime_dll()` mechanism by reading `tests/CMakeLists.txt` in
   full and `cmake/FetchSDL3.cmake` in full, and confirming the real build
   output via `browse_dir` (`build/tests/GreatTamanaEngineTests.exe`, with
   `SDL3.dll` copied next to it as a POST_BUILD step).

2. **Mechanism selection — deviated from my own first attempt after live
   investigation (see "Deviation" below).** Landed on a pure, self-contained
   PE (Portable Executable)/COFF import-table parser, added as a brand-new
   test file: `tests/Build/SdlLinkageRegressionTests.cpp`.

   - `ListImportedDllNames(const std::vector<std::uint8_t>&)` is a pure
     function that hand-parses a raw PE32+ image's DOS header, NT headers,
     section table, and Import Directory Table (walking `IMAGE_IMPORT_DESCRIPTOR`-
     shaped entries until the all-zero terminator), returning every DLL name
     the image imports. It deliberately does NOT use `<winnt.h>`'s `IMAGE_*`
     structs — it defines its own small, explicitly `#pragma pack(1)`-ed POD
     structs, so the function is a genuinely pure, Tier-1-testable piece of
     logic over a plain byte buffer, mirroring this test suite's own
     established "hand-parse a binary format by hand" convention
     (`Assets/GtaFileTests.cpp`, `Assets/PmxLoaderTests.cpp`).
   - Three tests prove the parser itself is correct BEFORE trusting it
     against the real binary: a hand-built minimal PE32+ image whose import
     table names exactly one DLL (`"TEST.dll"`), plus two malformed/empty-
     buffer degrade-gracefully cases.
   - The actual regression test,
     `SdlLinkageRegressionTest.SdlLinkageRegression_TestBinaryCurrentlyRequiresSdl3Dll`,
     resolves the CURRENTLY RUNNING test binary's own `.exe` path via
     `GetModuleFileNameW(nullptr, ...)` (a single, safe Win32 API call — no
     process creation involved), reads its own bytes off disk, and asserts
     `"SDL3.dll"` (case-insensitive) is present in its own import table. This
     currently PASSES, proving today's known-bad state
     (`tests/CMakeLists.txt`'s own header comment) is real and mechanically
     detectable, not just a verbal claim.

3. Registered the new file in `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES`
   list, and extended the existing prose comment about the SDL3.dll test-
   binary dependency to point at this new file and this campaign's Phase 14
   (which is expected to flip this test's expectation once `gte_core` drops
   its SDL3 link dependency for good).

4. Incremental compile check: `cmake --build build --target
   GreatTamanaEngineTests` — succeeded cleanly (108 build steps, only the
   changed/dependent objects rebuilt; `gte_core` relinked once). The only
   non-zero-looking output was a pre-existing, unrelated `stderr` warning
   from `third_party/ktx`'s own CMake (`git describe` failing to find a tag —
   present before this phase's changes too, confirmed by its wording
   referencing `third_party/ktx/cmake/version.cmake` only).

5. Ran ONLY the new tests via `ctest -R "SdlLinkageRegression|ListImportedDllNames"`
   (a single-test-family filter, per the phase's own explicit allowance for a
   brand-new test with no prior baseline — NOT a full regression pass). All 4
   new tests passed:
   - `ListImportedDllNamesTest.FindsANamedDllInAHandBuiltMinimalPe32PlusImage` — Passed
   - `ListImportedDllNamesTest.ReturnsEmptyForATooShortBuffer` — Passed
   - `ListImportedDllNamesTest.ReturnsEmptyForAnEmptyBuffer` — Passed
   - `SdlLinkageRegressionTest.SdlLinkageRegression_TestBinaryCurrentlyRequiresSdl3Dll` — Passed

**Incremental compile check only, per campaign policy** — no full clean
build, no full `ctest` regression pass was run (not required until Phase 9/
Phase 14/Phase 19).

## Deviation from the strategy doc's suggested mechanism (documented per Universal Rule 9)

The phase's own `Step 3` suggested, as one option: "copy `SDL3.dll` OUT of
the directory the test binary loads from, run the test binary, and confirm
whether it fails to LAUNCH AT ALL". I attempted exactly this first, via a
`CreateProcessW()`-based probe (copy `GreatTamanaEngineTests.exe` into a
fresh temp directory with no `SDL3.dll` present, launch it, confirm the
launch fails).

This was abandoned after live, empirical investigation during this phase's
own execution, for two concrete, observed reasons:

1. **A real, blocking, modal OS dialog appears.** Manually reproducing the
   scenario via `cmd.exe` (`GreatTamanaEngineTests.exe --gtest_list_tests`
   run from a directory with the `.exe` but no `SDL3.dll`) popped a genuine
   Windows "SDL3.dll が見つからないため、コードの実行を続行できません。"
   ("cannot continue because SDL3.dll was not found") system dialog that
   blocks the process until a human clicks OK — screenshot-confirmed during
   this session. Even with `SetErrorMode(SEM_FAILCRITICALERRORS |
   SEM_NOGPFAULTERRORBOX)` set on the parent process before `CreateProcessW()`
   (the standard, documented technique for suppressing this), a follow-up
   verification probe (a tiny throwaway C program built specifically to test
   this) hung indefinitely rather than returning a clean exit code within a
   generous timeout.
2. **A freshly-compiled throwaway `.exe` used for that verification probe
   itself appears to have tripped this environment's antivirus real-time
   scanning**, compounding the hang and making the mechanism's behavior
   unpredictable and hard to debug on this specific machine — exactly the
   kind of fragile, non-deterministic dependency this project's own testing
   philosphy (`AGENTS.md`, "Testability & Regression Safety") avoids
   elsewhere.

Per the phase's own guidance ("If genuinely unsure whether the chosen
detection mechanism is reliable... use `ask_questions`"), the user (who was
observing live) intervened directly and steered the implementation toward
option (3) from the strategy doc's own menu instead — a plain file-I/O-based
PE import-table parser, no process spawning, no OS dialog risk, no
antivirus interaction, and (as a bonus) a genuinely Tier-1-testable pure
function with its own dedicated correctness tests, which the process-launch
approach could never have had. This is a strictly stronger proof of the same
underlying fact (SDL3.dll is a REAL entry in the binary's own import table —
which is *why* the launch would fail without it, not just an assumed
correlation) and was fully verified working end-to-end (see test results
above) before being finalized.

## Files touched

- NEW: `tests/Build/SdlLinkageRegressionTests.cpp`
- MODIFIED: `tests/CMakeLists.txt` (registered the new source file in
  `GTE_TEST_SOURCES`, extended the existing SDL3.dll prose comment)
- NEW: `task_manager/editor-core-separation-1/PHASE1_COMPLETION_REPORT.md` (this file)

## Note for Phase 14

`SdlLinkageRegressionTest.SdlLinkageRegression_TestBinaryCurrentlyRequiresSdl3Dll`
currently asserts `SDL3.dll` IS present in the import table (today's known-bad
"before" state). Once Phase 14 moves `Window.cpp`/`SdlContext` out of
`gte_core` and `gte_core` drops `SDL3::SDL3` at link time, this exact
assertion is expected to start FAILING — that failure is the intended,
correct signal that the fix landed. Phase 14 must flip this test's own
expectation (or add an explicit sibling test asserting the opposite) rather
than silently leaving it red. The comment block at the top of
`tests/Build/SdlLinkageRegressionTests.cpp` documents this explicitly.
