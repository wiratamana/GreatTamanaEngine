# PHASE2 — `GTE_ENABLE_PLUGINS=OFF` Build Fix

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it first. Also read
`PHASE1_COMPLETION_REPORT.md` if it exists, for continuity clues.

**Severity:** CRITICAL (Issue #2 of the re-analysis document).

**Source of truth for this bug:**
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\editor-core-separation\
editor-core-separation-3_REANALYSIS_bugs_and_gaps_2026-09-24.md`, section "2.
CRITICAL — flipping `GTE_ENABLE_PLUGINS` to `OFF` breaks the build".

---

## Step 1: The Goal

Make `-DGTE_ENABLE_PLUGINS=OFF` actually produce a working build. Right now it
does not — it is a real, guaranteed, never-before-tested build break, not a
theoretical "tension." `GTE_ENABLE_PLUGINS` is a user-facing CMake `option()`
(root `CMakeLists.txt` line 116) that defaults `ON` — anyone deliberately
turning it off, expecting a plugin-free build (a completely reasonable thing
to want), gets a broken configure/link instead.

Design decision already made (confirmed via `ask_questions`, do not
re-litigate): **`gte_plugin_abi` becomes an unconditional `add_subdirectory()`
call** (it is header-only and cheap to compile — nothing is lost by always
building it). Only the three demo plugin subdirectories, and the real
`PluginHost::LoadPlugins()` runtime CALL SITE (already correctly gated —
`EditorHost.cpp` line 190's `#if GTE_ENABLE_PLUGINS`), stay conditional. This
matches `GTE_ENABLE_PLUGINS`'s own doc comment in `CMakeLists.txt` (lines
104-116), which already promises "always compiles, only the runtime call site
is gated" — the same precedent `GTE_ENABLE_PROFILER`/`GTE_ENABLE_JOB_SYSTEM`/
`GTE_ENABLE_NETWORK` already establish. The current code violates its own
documented precedent; this phase makes the code match it.

## Step 2: The Situation (exact current code)

Root `CMakeLists.txt`:

- Line 116: `option(GTE_ENABLE_PLUGINS ... ON)`.
- Lines 716-736 (`GTE_PLUGIN_RUNTIME_OUTPUT_DIR` setup, then):
  ```cmake
  if(GTE_ENABLE_PLUGINS)
      add_subdirectory(plugins/gte_plugin_abi)
      add_subdirectory(plugins/demo_hello_world)
      add_subdirectory(plugins/demo_render_feature)
      add_subdirectory(plugins/demo_editor_panel)
  endif()
  ```
  — this is what DEFINES the `gte_plugin_abi` CMake target (an `INTERFACE`
  library, `plugins/gte_plugin_abi/CMakeLists.txt`) and generates
  `GtePluginAbiFingerprintGenerated.h` via `configure_file()`.
- Line 1000: `target_compile_definitions(gte_core PUBLIC
  GTE_ENABLE_PLUGINS=$<BOOL:${GTE_ENABLE_PLUGINS}>)` — fine, unconditional,
  correct.
- Line 1033: `target_link_libraries(gte_core PUBLIC gte_plugin_abi)` — this
  reference is **UNCONDITIONAL**, but the target it references only exists
  when the block above ran. With `GTE_ENABLE_PLUGINS=OFF`, this line fails
  configure/link because `gte_plugin_abi` was never created.

Also relevant: `src/Core/Plugins/PluginHost.h/.cpp` are in `gte_core`'s
**unconditional** source file list (root `CMakeLists.txt`, lines 302-303) —
they always compile, regardless of `GTE_ENABLE_PLUGINS`. `PluginHost.cpp`
`#include`s `../../../plugins/gte_plugin_abi/PluginExports.h` (a plain,
always-present header under `plugins/gte_plugin_abi/`, NOT gated by CMake at
all — only the CMake TARGET that exposes its include directories is gated)
and `gte_plugin_abi/GtePluginAbiFingerprintGenerated.h` (a
**CMake-generated** header, which only exists once `plugins/gte_plugin_abi/
CMakeLists.txt`'s `configure_file()` call has actually run). This confirms the
real failure mode precisely: with the fix in place (`gte_plugin_abi` always
`add_subdirectory()`'d), `PluginHost.cpp` will always find both the plain
header and the generated one, in every configuration.

`plugins/gte_plugin_abi/CMakeLists.txt` line 47:
```cmake
if(GTE_ENABLE_PLUGINS AND GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED)
    set(GTE_PLUGIN_SHARED_RUNTIME_LINKAGE_INT 1)
else()
    set(GTE_PLUGIN_SHARED_RUNTIME_LINKAGE_INT 0)
endif()
```
This already correctly reads the real `GTE_ENABLE_PLUGINS` value (never
hardcodes it) — it will correctly keep producing `sharedRuntimeLinkage = 0`
in a `GTE_ENABLE_PLUGINS=OFF` configure once this file is always
`add_subdirectory()`'d. No change needed here.

## Step 3: The Plan (exact changes)

### 3.1 Root `CMakeLists.txt` — split the `if(GTE_ENABLE_PLUGINS)` block

Change the block currently at (approximately) lines 722-736 from:

```cmake
if(GTE_ENABLE_PLUGINS)
    add_subdirectory(plugins/gte_plugin_abi)
    add_subdirectory(plugins/demo_hello_world)
    # editor-core-separation-3 campaign, PHASE3
    # (PHASE3_RUNTIME_RENDER_FEATURE_CAPABILITY.md, Step 3.5) - the
    # IRenderFeatureModule_v1 throwaway demo (Milestone 1).
    add_subdirectory(plugins/demo_render_feature)
    # editor-core-separation-3 campaign, PHASE4
    # (PHASE4_EDITOR_PANEL_CAPABILITY_AND_REGISTRY.md, Step 3.9) - the
    # IEditorPanelModule_v1 throwaway demo (Milestone 2). Only depends on
    # gte_plugin_abi (Locked Design Decision #2), so it builds correctly even
    # in a GTE_CORE_STANDALONE_PROBE_ONLY=ON configuration where gte_editor
    # itself is skipped entirely.
    add_subdirectory(plugins/demo_editor_panel)
endif()
```

to:

```cmake
# editor-core-separation-4 campaign, PHASE2
# (PHASE2_GTE_ENABLE_PLUGINS_OFF_BUILD_FIX.md) - gte_plugin_abi is an
# unconditional add_subdirectory() call, ALWAYS run regardless of
# GTE_ENABLE_PLUGINS. It is a plain, header-only INTERFACE library (cheap to
# configure - one configure_file() call, zero real compilation), and gte_core
# unconditionally links against it (see target_link_libraries(gte_core
# PUBLIC gte_plugin_abi) below) because src/Core/Plugins/PluginHost.h/.cpp
# are themselves in gte_core's own UNCONDITIONAL source file list - they
# always compile and always need this target's include directories, in
# EVERY configuration, including GTE_ENABLE_PLUGINS=OFF (where PluginHost's
# class still compiles and is still testable, it is simply never CALLED at
# runtime - EditorHost.cpp's own m_core.LoadPlugins() call site is what is
# actually gated, via #if GTE_ENABLE_PLUGINS). Confirmed, real, previously-
# broken bug this phase fixes: before this change, this whole
# add_subdirectory() call (including gte_plugin_abi's own) was gated behind
# `if(GTE_ENABLE_PLUGINS)`, so a GTE_ENABLE_PLUGINS=OFF configure meant
# gte_plugin_abi never existed as a CMake target at all, while
# target_link_libraries(gte_core PUBLIC gte_plugin_abi) further down this
# file referenced it completely unconditionally - a guaranteed configure/link
# failure, never exercised before this phase (see
# task_manager/editor-core-separation-4/PHASE2_GTE_ENABLE_PLUGINS_OFF_BUILD_FIX.md).
add_subdirectory(plugins/gte_plugin_abi)

if(GTE_ENABLE_PLUGINS)
    add_subdirectory(plugins/demo_hello_world)
    # editor-core-separation-3 campaign, PHASE3
    # (PHASE3_RUNTIME_RENDER_FEATURE_CAPABILITY.md, Step 3.5) - the
    # IRenderFeatureModule_v1 throwaway demo (Milestone 1).
    add_subdirectory(plugins/demo_render_feature)
    # editor-core-separation-3 campaign, PHASE4
    # (PHASE4_EDITOR_PANEL_CAPABILITY_AND_REGISTRY.md, Step 3.9) - the
    # IEditorPanelModule_v1 throwaway demo (Milestone 2). Only depends on
    # gte_plugin_abi (Locked Design Decision #2), so it builds correctly even
    # in a GTE_CORE_STANDALONE_PROBE_ONLY=ON configuration where gte_editor
    # itself is skipped entirely.
    add_subdirectory(plugins/demo_editor_panel)
endif()
```

(Phase 5 of this same campaign will add a fourth demo plugin subdirectory
inside this same `if(GTE_ENABLE_PLUGINS)` block — do not worry about that
here, just get this phase's own split correct.)

### 3.2 Leave `plugins/gte_plugin_abi/CMakeLists.txt` untouched

Its own `if(GTE_ENABLE_PLUGINS AND GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED)`
check (line 47) already does the right thing once it is always
`add_subdirectory()`'d: with `GTE_ENABLE_PLUGINS=OFF`, this correctly
evaluates false, so the generated fingerprint's `sharedRuntimeLinkage` field
reads `0` — an honest reflection of "plugins are disabled in this build," not
a fabricated `1`. No edit needed to this file for this phase.

### 3.3 Leave line ~1033 (`target_link_libraries(gte_core PUBLIC
gte_plugin_abi)`) untouched

It was already correct in isolation (an unconditional reference to a target
that, after 3.1's fix, now unconditionally exists). Do not add a redundant
`if(GTE_ENABLE_PLUGINS)` guard around it — that would be pointless now that
the target always exists, and would be inconsistent with `PluginHost.h/.cpp`
being in `gte_core`'s own unconditional source list.

### 3.4 Double-check `docs/conventions/plugin-architecture.md` / `AGENTS.md`

Neither currently claims `gte_plugin_abi` itself is conditional (they
correctly describe `PluginHost` as "always compiled" already) — re-read both
quickly after your change to confirm nothing there needs updating to match
the new, corrected CMake shape. If you do find a stale claim, fix it in the
same commit.

## Verification (fast, incremental — THIS is the one narrow case where a
SEPARATE small configure is required, not a full rebuild of the main `build`
tree)

This phase's whole point is proving a configuration that was NEVER tested
before. Verify it for real, in a throwaway side build directory, without
touching the main `build/` tree at all:

1. `cmake -S . -B build-plugins-off -G Ninja -DGTE_ENABLE_PLUGINS=OFF
   -DGTE_BUILD_TESTS=OFF` (working directory: project root). Confirm this
   configure step succeeds with **zero** errors about `gte_plugin_abi` — this
   is the exact failure this phase fixes; if it still fails here, the fix is
   incomplete.
2. `cmake --build build-plugins-off --target gte_core` (just the one static
   library target — do not build the whole Editor executable, keep this
   fast). Confirm it links with zero errors.
3. Optionally, for extra confidence, also build the full executable target
   once (`cmake --build build-plugins-off`) if the incremental `gte_core`
   step above succeeds cleanly and time allows — this is still a SEPARATE,
   throwaway directory, not the main `build/` tree, so it does not violate
   this campaign's "no full build of the main tree except Phase 8" rule.
4. Confirm the MAIN `build/` tree (the one used by every other phase) is
   completely unaffected: `cmake --build build --target gte_core` (a normal,
   `GTE_ENABLE_PLUGINS=ON` incremental build) still succeeds, proving this
   phase's CMake restructuring didn't break the default, already-working
   configuration.
5. Clean up: delete the throwaway `build-plugins-off` directory once verified
   (do not leave it in the repo — it is not meant to be a permanent build
   tree, just this phase's own proof).

## Completion

Write `PHASE2_COMPLETION_REPORT.md`: the exact `cmake -S . -B
build-plugins-off ...` command run, its real output (success), and
confirmation the main `build/` tree still builds too. Then `git_add` +
`git_commit` (message referencing PHASE2). If anything about "should the
throwaway directory be kept for future regression checking" feels ambiguous,
use `ask_questions` rather than deciding silently either way.
