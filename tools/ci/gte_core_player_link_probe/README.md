# gte_core player link probe
> A manual check. A real executable must link against `gte_core` alone, with no `gte_editor`, SDL3 or ImGui.

This repo has no CI pipeline. Run this by hand.

## Why

The [standalone probe](../gte_core_standalone_probe/README.md) builds `gte_core` as an archive. An archive build never resolves symbols. So it cannot catch a `gte_core` call into a symbol only `gte_editor` defines. A real link can.

## Run

From the repo root:

```cmd
cmake -S tools/ci/gte_core_player_link_probe -B build-player-link-probe
cmake --build build-player-link-probe
```

## How it works

| Step | Detail |
|---|---|
| 1 | A nested configure sets `GTE_CORE_STANDALONE_PROBE_ONLY=ON` and `GTE_BUILD_TESTS=OFF` (same trick as the standalone probe) |
| 2 | In that mode the root `CMakeLists.txt` also defines the exe `gte_core_player_link_probe` from `tools/ci/gte_core_player_link_probe/main.cpp` |
| 3 | The exe links `gte_core` only |

`main.cpp` forces the linker to pull three object files out of `libgte_core.a`:

| Object file | How it is forced |
|---|---|
| `Core.cpp.obj` | takes the address of `gte::Core::BuildFrame` |
| `RenderSystem.cpp.obj` | takes the address of one `gte::RenderSystem::Draw` overload |
| `Network/NetworkServer.cpp.obj` | constructs a `gte::Network::NetworkServer`. It never calls `Start()`, so no socket opens. |

Nothing runs. Only the link step is the test. Running the `.exe` proves nothing.

## Result

- Success: the link works. No `gte_core` file needs an editor symbol.
- Failure: a linker `undefined reference` error. A `gte_core` file calls code that only `gte_editor` has.

## Notes

- `build-player-link-probe/` is git-ignored.
- Do not set `GTE_CORE_STANDALONE_PROBE_ONLY` in a normal `build/` configure.
