# `gte_plugin_abi` Handshake Probe

## What this is

A tiny, **manually-invocable** local CMake project, mirroring
`tools/ci/gte_core_player_link_probe/`'s own exact shape (see that project's
own `README.md` for the full "why nested cmake, not `add_subdirectory()`"
reasoning, which applies here identically). It configures the real
repository root `CMakeLists.txt` with `GTE_CORE_STANDALONE_PROBE_ONLY=ON` and
`GTE_ENABLE_PLUGINS=ON`, then builds ONE target that only exists in that
configuration: `gte_plugin_abi_handshake_probe` - a tiny executable that
`LoadLibraryW()`s a real, already-built `demo_hello_world.dll` at runtime and
mechanically re-proves the WHOLE plugin ABI handshake works end-to-end
OUTSIDE `GreatTamanaEditor.exe` entirely.

Needs **zero `gte_core`/`gte_editor`** at all - only `gte_plugin_abi` (the
frozen ABI contract) plus a real, built `demo_hello_world.dll` - proving
Milestone 0's own "the ABI boundary itself works before any real capability
exists" claim in complete isolation.

## How it works

`main.cpp` `#include`s only `plugins/gte_plugin_abi/`'s own headers, resolves
`demo_hello_world.dll`'s own path relative to its own `.exe` directory
(`GetModuleFileNameW`), then:

1. `LoadLibraryW()`s it.
2. Resolves all 3 fixed exports (`GTE_GetPluginAbiFingerprint`/
   `GTE_CreatePluginModule`/`GTE_DestroyPluginModule`) by exact name.
3. Calls `GTE_GetPluginAbiFingerprint()` and compares it, byte-for-byte,
   against this probe's own `MakeThisBuildsFingerprint()`.
4. Calls `GTE_CreatePluginModule()`, then `GetModuleInfo()`, and prints the
   result via plain `std::printf` (`PHASE0_MASTER_STRATEGY.md`'s Universal
   Rule 5's own explicit exception for a standalone CI-probe `main.cpp`).
5. Calls `GTE_DestroyPluginModule()`, then `FreeLibrary()`s.

Any failure at any step prints a clear `FAIL: ...` message to `stderr` and
returns a non-zero exit code - success prints `OK: ...` and returns `0`.

## Exact command to run this by hand

From the repository root:

```
cmake -S tools/ci/gte_plugin_abi_handshake_probe -B build-plugin-abi-handshake-probe -G Ninja
cmake --build build-plugin-abi-handshake-probe
build-plugin-abi-handshake-probe\gte_plugin_abi_inner_build\gte_plugin_abi_handshake_probe.exe
```

(`-G Ninja` is required on a machine whose default CMake generator isn't
already Ninja - this repository's own main build tree already uses Ninja, but
this OUTER wrapper project is configured independently and does not inherit
that choice automatically.)

## What a successful run proves

- The plugin ABI handshake (fingerprint check -> resolve exports ->
  `GTE_CreatePluginModule()` -> `GetModuleInfo()` -> destroy -> unload) works
  correctly end-to-end, with zero `gte_core`/`gte_editor` involvement at all.
- `demo_hello_world.dll` is a genuinely valid, correctly-exporting plugin
  `.dll` - not just "the build produced a file".

## What this probe deliberately does NOT do

- It does not exercise any real capability (`IRenderFeatureModule_v1`/
  `IEditorPanelModule_v1`) - `demo_hello_world` implements zero capabilities
  on purpose (Milestone 0 only).
- It is not wired into any GitHub Actions workflow or other real CI system -
  none exists in this repository. Run it by hand whenever you want to
  re-confirm the plugin ABI handshake still works.
- It never appears as a user-facing option inside the main build.
