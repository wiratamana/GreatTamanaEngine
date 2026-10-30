# AtmosphereCoreDecouplingCheck
> A small CMake script. The feature-agnostic core files must never contain the word `Atmosphere`.

## What it enforces

| Item | Value |
|---|---|
| ctest name | `AtmosphereCoreDecouplingCheck` |
| Script | `CheckAtmosphereCoreDecoupling.cmake`, run with `cmake -P` |
| Files checked | `src/Core/Core.h`, `src/Core/Core.cpp`, `src/Editor/EditorLayer.h`, `src/Editor/EditorHost.cpp` |
| Rule | the case-sensitive text `Atmosphere` must not appear |
| Method | plain line search. A comment hit counts like a code hit. |

These files are the rendering core, the editor boundary, and the composition root. None may name a built-in feature.

## Run

```cmd
ctest --test-dir build -R AtmosphereCoreDecouplingCheck --output-on-failure
```

Or directly:

```cmd
cmake -D GTE_REPO_ROOT=. -P tools/ci/atmosphere_core_decoupling_check/CheckAtmosphereCoreDecoupling.cmake
```

## Result

| Result | Meaning |
|---|---|
| PASS | no file has the word. This proves text decoupling only. It does not prove the feature still renders. |
| FAIL | the script prints every hit and exits non-zero |

## Limits

- It checks only the four files above.
- It does not parse C++.
