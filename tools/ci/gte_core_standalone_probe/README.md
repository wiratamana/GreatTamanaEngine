# gte_core standalone probe
> A manual check. `gte_core` must build as a static library with no editor, no SDL and no ImGui.

This repo has no CI pipeline. Run this by hand.

## Why

A normal build always links `gte_editor` next to `gte_core`. So a wrong `gte_core` to `gte_editor` include would never show up. This probe builds `gte_core` alone.

## Run

From the repo root:

```cmd
cmake -S tools/ci/gte_core_standalone_probe -B build-core-probe
cmake --build build-core-probe
```

## How it works

| Step | Detail |
|---|---|
| 1 | The probe is a tiny meta-project. It runs a nested `cmake -S <repo root> -B <inner dir> -G Ninja` |
| 2 | The nested configure sets `GTE_CORE_STANDALONE_PROBE_ONLY=ON` and `GTE_BUILD_TESTS=OFF` |
| 3 | The root `CMakeLists.txt` skips `gte_editor`, the exe, SDL3, ImGui and ImGuizmo |
| 4 | The probe builds the `gte_core` target |

Nested configure is used because the `cmake/Fetch*.cmake` modules use `CMAKE_SOURCE_DIR`. `add_subdirectory` cannot work.

## Result

- Success: `gte_core` compiles and archives with no editor symbols.
- Failure: a `gte_core` file includes an editor, SDL or ImGui header.

An archive build does not resolve symbols. For an undefined-reference check, use the [player link probe](../gte_core_player_link_probe/README.md).

## Notes

- `build-core-probe/` is git-ignored.
- Do not set `GTE_CORE_STANDALONE_PROBE_ONLY` in a normal `build/` configure.
