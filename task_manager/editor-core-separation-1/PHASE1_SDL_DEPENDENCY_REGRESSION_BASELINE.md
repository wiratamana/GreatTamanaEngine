# PHASE1 — SDL Dependency Regression Baseline

## Parent
`PHASE0_MASTER_STRATEGY.md` — read it in full first, plus the design doc it
references.

## Step 1: The Goal

Before changing ANY code, capture today's known-bad fact — the
`GreatTamanaEngineTests` binary depends on `SDL3.dll` at process load time,
even though no Tier-1 test ever calls `SDL_Init()` or opens a window — as a
concrete, provable, standing check. This gives Phase 14 (where the fix
actually lands) a real "before" state to flip to "after", instead of relying
on a vague verbal claim.

## Step 2: The Situation / The Problem

`tests/CMakeLists.txt`'s own header comment already documents this: *"the
test BINARY still dynamically depends on SDL3.dll at process load time
regardless of the above (gte_core's Window.cpp references SDL symbols like
SDL_CreateWindow, even though no Tier 1 test ever calls SDL_Init() or opens a
window)"*. This is currently just a comment — nothing mechanically checks it.

## Step 3: The Plan

1. Read `tests/CMakeLists.txt` in full to confirm the exact current wording
   of that comment and find the test binary's exact output path/name after
   a build (e.g. `build/tests/GreatTamanaEngineTests.exe` — confirm the real
   path via `browse_dir`, do not assume).
2. Add a new, small, self-contained Tier-1 test file,
   `tests/Build/SdlLinkageRegressionTests.cpp` (new folder `tests/Build/` is
   fine — mirrors the "mirror the src folder it lives in" rule loosely,
   this one is about the BUILD OUTPUT itself, not a `src/` mirror). This
   test does NOT try to detect SDL at the C++ level (that would prove
   nothing new) — instead:
   - Write a tiny helper (can be a `.cmake` script invoked as a
     `add_test(NAME SdlLinkageProbe COMMAND ...)` in `tests/CMakeLists.txt`,
     OR a small C++ test that shells out) that inspects the actual built
     test executable's import table for `SDL3.dll` — on Windows, the
     simplest mechanically-reliable way is: `dumpbin /dependents
     <path-to-GreatTamanaEngineTests.exe>` (MSVC toolchain) if available, OR
     — since this project uses Ninja/MinGW — parse the PE import directory
     yourself with a tiny helper, OR (simplest, most portable): copy
     `SDL3.dll` OUT of the directory the test binary loads from, run the
     test binary, and confirm whether it fails to LAUNCH AT ALL (a process
     that fails to start at all, distinct from a normal test failure, is
     the actual symptom `tests/CMakeLists.txt`'s comment describes).
   - Prefer the simplest option that is actually mechanically checkable
     with the tools available in this environment (`run_shell`/`cmake`) —
     do not over-engineer a PE-parsing tool if a simpler shell-level probe
     proves the same fact. Decide the exact mechanism during this phase's
     own execution and document the choice in the completion report.
3. Register this as a clearly-named CTest case, e.g.
   `SdlLinkageRegression_TestBinaryCurrentlyRequiresSdl3Dll`, that is
   EXPECTED TO PASS TODAY (i.e. it currently asserts/confirms "yes, SDL3.dll
   IS required" — Phase 14 will flip its expectation, or replace it with a
   companion test, once the fix lands. Document clearly, in a comment right
   at the top of this new test file, that this is a DELIBERATE "before"
   snapshot that Phase 14 must revisit).
4. Compile-check: build only the tests target incrementally
   (`cmake --build build --target GreatTamanaEngineTests` or the project's
   actual test target name — confirm the real target name from
   `tests/CMakeLists.txt` first) and run just this one new test via `ctest`
   with `-R SdlLinkageRegression` (a single-test filter, NOT a full run —
   this is allowed since it is a brand-new test with no prior baseline to
   compare against, and is not a "full regression pass").
5. If genuinely unsure whether the chosen detection mechanism is reliable
   (e.g. shell-level DLL probing behaves oddly on this machine), use
   `ask_questions` to confirm the mechanism with the user before finalizing.

## Files Touched

- NEW: `tests/Build/SdlLinkageRegressionTests.cpp` (or a `.cmake`-only probe
  — pick during execution, see step 2 above).
- `tests/CMakeLists.txt` — register the new test/probe.

## Definition of Done

- A single, clearly-named CTest case exists that mechanically confirms
  today's SDL3.dll dependency, and currently PASSES (proving the bad state
  is real and detectable).
- `PHASE1_COMPLETION_REPORT.md` written in this folder, documenting the
  exact detection mechanism chosen and why.
- Git commit of the new test file(s) + `tests/CMakeLists.txt` change +
  report.

## Out of Scope

Do not attempt to fix the SDL dependency itself here — that is Phase 14's
job. This phase only proves the problem exists in a mechanically-checkable
way.
