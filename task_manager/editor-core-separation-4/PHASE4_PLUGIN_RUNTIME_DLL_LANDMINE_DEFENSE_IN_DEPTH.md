# PHASE4 — Plugin Runtime DLL Landmine — Defense in Depth

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it first. Also read
`PHASE1`-`PHASE3`'s own `COMPLETION_REPORT.md` files if they exist.

**Severity:** HIGH (Issue #4 of the re-analysis document).

**Source of truth for this bug:**
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\editor-core-separation\
editor-core-separation-3_REANALYSIS_bugs_and_gaps_2026-09-24.md`, section "4.
HIGH — a real future toolchain switch (already planned) will make `PluginHost`
try to load its own runtime DLLs as plugins".

---

## Step 1: The Goal

Fix a landmine planted by the prior campaign's own CMake code, for a future
toolchain switch that campaign's own report says is coming. Today, every demo
plugin's `CMakeLists.txt` calls `gte_apply_plugin_shared_crt_linkage(<target>)`,
which — once a shared-CRT-capable toolchain is switched on
(`GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED` becomes `TRUE`, already planned,
already installed via `scoop install mingw`) — copies
`libstdc++-6.dll`/`libgcc_s_seh-1.dll`/`libwinpthread-1.dll` directly into
`<build-dir>/plugins/`, the EXACT SAME folder `PluginHost::LoadPlugins()`
scans at startup. `PluginHost` has zero filename filtering beyond "ends in
`.dll`" — it will `LoadLibraryW()` all 3 runtime DLLs, `GetProcAddress()` will
correctly fail to find `GTE_GetPluginAbiFingerprint`, and the log will fill
with 3 spurious warnings, every single run, forever.

Design decision already made (confirmed via `ask_questions`, do not
re-litigate): implement **BOTH** independent mitigations, for defense in
depth:

- **(a)** Stop staging those 3 runtime DLLs into the shared `plugins/` scan
  folder for PLUGIN `.dll` targets specifically — the HOST executable (and any
  standalone probe `.exe`) still stages them next to itself exactly as before
  (unchanged); a plugin `.dll` loaded into the same process relies on the
  host's own copy, found via the loading application's own directory (part of
  the standard Windows DLL search order), so a plugin's own separate copy was
  always redundant, never actually required.
- **(b)** Make `PluginHost::LoadPlugins()` itself skip a small, explicit,
  known-non-plugin filename ignore-list (the 3 runtime DLL names) — a second,
  independent safety net that also protects against a human manually copying
  one of these DLLs into `plugins/` for some other unrelated reason.

## Step 2: The Situation (exact current code)

`cmake/MingwRuntime.cmake`'s `gte_apply_plugin_shared_crt_linkage(target_name)`
(the ONE reusable helper called by the host executable, every plugin `.dll`,
and every standalone probe `.exe`):

```cmake
function(gte_apply_plugin_shared_crt_linkage target_name)
    if(NOT GTE_ENABLE_PLUGINS)
        return()
    endif()
    if(NOT GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED)
        message(WARNING "gte_apply_plugin_shared_crt_linkage(${target_name}): ...")
        return()
    endif()
    target_link_options(${target_name} PRIVATE -shared-libgcc)
    mingw_copy_runtime_dll(${target_name})
endfunction()
```

`mingw_copy_runtime_dll(target_name)` copies the 3 runtime DLLs into
`$<TARGET_FILE_DIR:${target_name}>` — for a plugin `.dll` target, that
resolves to `${GTE_PLUGIN_RUNTIME_OUTPUT_DIR}` (i.e. `<build-dir>/plugins/`,
per each demo plugin's own `set_target_properties(... RUNTIME_OUTPUT_DIRECTORY
"${GTE_PLUGIN_RUNTIME_OUTPUT_DIR}" ...)`) — the SAME folder `PluginHost`
scans. Today this is inert only because `GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED`
is `FALSE` on this machine.

`src/Core/Plugins/PluginHost.cpp`'s `LoadPlugins()`:

```cpp
void PluginHost::LoadPlugins(const std::filesystem::path& pluginsDirectory)
{
    ...
    for (const auto& entry : std::filesystem::directory_iterator(pluginsDirectory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() != ".dll") {
            continue;
        }
        TryLoadOnePlugin(entry.path());
    }
}
```
— zero filename filtering beyond the extension check.

Every demo plugin's `CMakeLists.txt` calls both:
```cmake
set_target_properties(demo_xxx PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${GTE_PLUGIN_RUNTIME_OUTPUT_DIR}" PREFIX "")
gte_apply_plugin_shared_crt_linkage(demo_xxx)
```

## Step 3: The Plan (exact changes)

### 3.1 Split `gte_apply_plugin_shared_crt_linkage()` into a host variant
(unchanged) and a new plugin-`.dll` variant (no DLL staging)

File: `cmake/MingwRuntime.cmake`.

Keep the EXISTING `gte_apply_plugin_shared_crt_linkage(target_name)` function
completely unchanged — it is still exactly correct for the HOST executable
(`GreatTamanaEditor`) and every standalone probe `.exe` (they each need the 3
runtime DLLs staged next to their own binary, since nothing else stages them
there).

Add a NEW function, right after it in the same file:

```cmake
# editor-core-separation-4 campaign, PHASE4
# (PHASE4_PLUGIN_RUNTIME_DLL_LANDMINE_DEFENSE_IN_DEPTH.md) - the variant of
# gte_apply_plugin_shared_crt_linkage() every PLUGIN .dll TARGET must call
# instead of the original function above. Applies the SAME link-time CRT
# flip (-shared-libgcc, needed so THIS .dll's own fingerprint correctly
# reports sharedRuntimeLinkage=1 once a shared-CRT-capable toolchain is
# active) but DELIBERATELY DOES NOT call mingw_copy_runtime_dll() - a plugin
# .dll's RUNTIME_OUTPUT_DIRECTORY is ALWAYS the shared plugins/ folder
# PluginHost::LoadPlugins() scans at startup (GTE_PLUGIN_RUNTIME_OUTPUT_DIR),
# so copying libstdc++-6.dll/libgcc_s_seh-1.dll/libwinpthread-1.dll there
# would make PluginHost try to LoadLibraryW() them as if they were plugins
# (see this phase's own file for the full, confirmed failure mode this
# fixes). This is safe: the HOST executable (or standalone probe .exe) that
# actually loads this plugin .dll already stages its OWN copy of these same
# 3 runtime DLLs next to ITSELF (via the ORIGINAL, unchanged
# gte_apply_plugin_shared_crt_linkage() above, which every host/probe target
# must still call) - the Windows DLL search order includes "the directory
# the loading APPLICATION's own .exe is in" for any DLL resolved by bare
# name (no path) during another DLL's own import resolution, which is
# exactly how a plugin .dll's transitive dependency on libstdc++-6.dll etc.
# gets satisfied here, with zero redundant copy needed inside plugins/
# itself.
function(gte_apply_plugin_dll_shared_crt_linkage target_name)
    if(NOT GTE_ENABLE_PLUGINS)
        return()
    endif()
    if(NOT GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED)
        message(WARNING "gte_apply_plugin_dll_shared_crt_linkage(${target_name}): active toolchain has no shared libstdc++ variant - honest no-op, ${target_name} stays statically linked (see this file's own top-of-file comment).")
        return()
    endif()
    target_link_options(${target_name} PRIVATE -shared-libgcc)
    # Deliberately NOT calling mingw_copy_runtime_dll(${target_name}) here -
    # see this function's own doc comment above for exactly why.
endfunction()
```

### 3.2 Update every demo plugin's `CMakeLists.txt` to call the new function

Files: `plugins/demo_hello_world/CMakeLists.txt`,
`plugins/demo_render_feature/CMakeLists.txt`,
`plugins/demo_editor_panel/CMakeLists.txt` (and Phase 5's future
`demo_render_feature_second`, if Phase 5 has not already landed when you do
this — check first; if it has, update that one too).

Change the last line of each file from:
```cmake
gte_apply_plugin_shared_crt_linkage(demo_xxx)
```
to:
```cmake
gte_apply_plugin_dll_shared_crt_linkage(demo_xxx)
```

Update each file's own neighboring comment (if any references the old
function name) to reference the new one, briefly, with a pointer to this
phase's file for the full reasoning — do not duplicate the full reasoning in
each of the 3-4 small plugin CMake files, one short pointer comment per file
is enough.

### 3.3 Do NOT touch the host executable's or probes' own calls

`GreatTamanaEditor`'s own `gte_apply_plugin_shared_crt_linkage(GreatTamanaEditor)`
call (find it via `search_in_dir` on root `CMakeLists.txt` for
`gte_apply_plugin_shared_crt_linkage`), and
`tools/ci/gte_plugin_abi_handshake_probe`/`tools/ci/gte_plugin_isolation_probe`/
`tools/ci/gte_core_player_link_probe`'s own calls, all stay calling the
ORIGINAL, unchanged function — they are hosts, not plugin `.dll`s, and they
each need their own real copy of the 3 runtime DLLs staged next to
themselves.

### 3.4 `PluginHost` defensive ignore-list (mitigation (b))

File: `src/Core/Plugins/PluginHost.cpp`.

Add a small, explicit, named ignore-list check inside `LoadPlugins()`'s
per-file loop, right after the extension check (this phase adds it BEFORE
Phase 7's case-insensitive extension-matching change lands — if Phase 7 has
already landed when you implement this, adapt the merge so both checks
coexist cleanly, they are independent of each other):

```cpp
namespace {

// editor-core-separation-4 campaign, PHASE4
// (PHASE4_PLUGIN_RUNTIME_DLL_LANDMINE_DEFENSE_IN_DEPTH.md) - a second,
// independent safety net beyond 3.1/3.2's own CMake-level fix (which stops
// these files from ever being COPIED into plugins/ in the first place for a
// plugin .dll target). This list additionally protects against these exact
// filenames ending up in plugins/ for any OTHER reason (e.g. a developer
// manually copying one there, or a future CMake change reintroducing the
// same mistake this phase fixes) - PluginHost is a load-bearing safety
// boundary, it should not silently regress if the CMake-level fix is ever
// undone by accident.
constexpr const char* kKnownNonPluginFilenames[] = {
    "libstdc++-6.dll",
    "libgcc_s_seh-1.dll",
    "libwinpthread-1.dll",
};

bool IsKnownNonPluginFilename(const std::filesystem::path& fileName)
{
    const std::string name = fileName.string();
    for (const char* known : kKnownNonPluginFilenames) {
        if (_stricmp(name.c_str(), known) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace
```

And in `LoadPlugins()`'s loop body:

```cpp
for (const auto& entry : std::filesystem::directory_iterator(pluginsDirectory)) {
    if (!entry.is_regular_file()) {
        continue;
    }
    if (entry.path().extension() != ".dll") { // (Phase 7 may change this to a case-insensitive helper - keep whichever form is present, just add the new check below it)
        continue;
    }
    if (IsKnownNonPluginFilename(entry.path().filename())) {
        GTE_LOG_INFO("PluginHost", "Skipping known non-plugin runtime file: " + entry.path().string());
        continue;
    }
    TryLoadOnePlugin(entry.path());
}
```

`_stricmp` needs `<cstring>` (already implicitly available via `<windows.h>`
on this toolchain, but add an explicit `#include <cstring>` if it does not
already compile cleanly — never rely on an implicit transitive include).

## Verification (fast, incremental — no full build)

1. `cmake --build build --target gte_core` (rebuilds `PluginHost.cpp`) and
   `cmake --build build` for the demo plugin targets + `GreatTamanaEditor`
   (needed since their `CMakeLists.txt` changed) — this IS effectively a
   normal incremental build (Ninja only rebuilds what changed), not a full
   clean rebuild; do not `rd /s /q build` first.
2. Confirm zero compile/configure errors.
3. Confirm (via `browse_dir` on `build/plugins/`) that
   `libstdc++-6.dll`/`libgcc_s_seh-1.dll`/`libwinpthread-1.dll` are NOT
   present there today either way (this machine's toolchain still can't
   produce shared linkage, so this was already true before this phase — the
   real proof this phase adds is code-level/structural, not observable via a
   file listing on THIS machine; that is expected and fine, note it honestly
   in your completion report rather than claiming a false "observed" proof).
4. Live check: `run_app_background` the rebuilt `GreatTamanaEditor.exe`,
   confirm (`GET /get_logs?limit=50`) the 3 demo plugins still load
   successfully (unchanged behavior) and there is no new, unexpected warning.
   `stop_app_background` when done.
5. If time/scope allows, a stronger, OPTIONAL manual proof: temporarily copy
   one real `libstdc++-6.dll` (from wherever the active MinGW toolchain's own
   `bin/` directory has one, if any exists on this machine at all — check
   first, do not fail this phase if none exists) into `build/plugins/` by
   hand, restart the Editor, confirm `PluginHost`'s new ignore-list produces
   the expected `GTE_LOG_INFO` "Skipping known non-plugin runtime file" line
   via `GET /get_logs`, then delete that manually-copied file again before
   finishing. This is optional because mitigation (b) is fully exercisable
   this way even though mitigation (a) cannot be observed end-to-end on this
   toolchain.

## Completion

Write `PHASE4_COMPLETION_REPORT.md`: what changed in both `cmake/
MingwRuntime.cmake` and `PluginHost.cpp`, an honest note that mitigation (a)'s
real effect cannot be observed end-to-end on this machine's current toolchain
(no shared-CRT DLLs are ever produced here), and the result of the optional
manual ignore-list proof if you ran it. Then `git_add` + `git_commit` (message
referencing PHASE4).
