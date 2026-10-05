# Atmosphere/Core Decoupling Check

## What this is

A tiny, standalone CMake script (`CheckAtmosphereCoreDecoupling.cmake`, run via
`cmake -P`, never configured as its own project) that enforces one permanent
rule: `src/Core/Core.h`, `src/Core/Core.cpp`, and `src/Editor/EditorLayer.h`
must never contain the literal, case-sensitive substring `Atmosphere`. These
three files are the engine's feature-agnostic rendering core and its one
Editor abstraction boundary - neither should ever name a concrete built-in
render feature directly.

Unlike the other folders under `tools/ci/` (manually-invocable local CMake
probes), this one IS wired into the real, automated test run: it is
registered as a `ctest` test named `AtmosphereCoreDecouplingCheck` (see
`tests/CMakeLists.txt`), so a plain `ctest` run from the build directory
exercises it automatically, every time, alongside the GoogleTest suite.

## Running it directly

```
cmake -D GTE_REPO_ROOT=<repo root> -P tools/ci/atmosphere_core_decoupling_check/CheckAtmosphereCoreDecoupling.cmake
```

Or, once registered as a test:

```
ctest -R AtmosphereCoreDecouplingCheck --output-on-failure
```

## What a pass/fail result means

- **PASS**: none of the three files mention the substring. This proves
  textual decoupling only - it is NOT proof the feature still renders
  correctly after being moved out of Core (that needs a live, manual
  smoke test).
- **FAIL**: the script prints every offending line and exits non-zero. A
  wrapping `ctest` run reports this the same way it reports any other failed
  test.

## What this deliberately does NOT do

- It does not check any file other than the three listed above.
- It does not understand C++ at all - it is a plain, line-based textual
  substring search (`file(STRINGS ... REGEX "Atmosphere")`), so a match
  inside a comment counts exactly the same as a match inside real code.
