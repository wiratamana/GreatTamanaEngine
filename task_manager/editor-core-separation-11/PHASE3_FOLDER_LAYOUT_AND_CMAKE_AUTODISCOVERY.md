# PHASE3 of 8 — FOLDER LAYOUT + CMAKE AUTO-DISCOVERY + `gte_add_project()`

Read `PHASE0_MASTER_STRATEGY.md` first (Findings C and D especially — this
phase resolves both concretely).
**Depends on:** PHASE2's probe having passed (you are now applying its proven
mechanism to the REAL executable target).
**Blocks:** PHASE4 (shaders), PHASE5 (runtime loader) — both need the
`_Game`/`_Editor` CMake targets this phase creates to already exist.

**This is the heaviest phase in the whole campaign** — it touches the root
`CMakeLists.txt` (a large, shared, load-bearing file), adds a brand-new
`cmake/GteProject.cmake`, and adds a new function to the existing
`cmake/MingwRuntime.cmake`. Re-verify every anchor below via `search_in_dir`
before editing — do not trust any line number as still current.

End state of this phase: a brand-new, empty (no real capability code) test
project, `Projects/ProjectAssemblyProbe/`, that CMake auto-discovers, builds
into `ProjectAssemblyProbe_Game.dll` (and, since it has an `Editor/`
subfolder, `ProjectAssemblyProbe_Editor.dll` too), landing in a new,
dedicated output folder. Nothing loads these `.dll`s into the running engine
yet — that is PHASE5.

## STEP 1 — new CMake option, `GTE_ENABLE_PROJECT_ASSEMBLIES` (LDD9)

Add near the existing option group (root `CMakeLists.txt`, alongside
`GTE_ENABLE_PROJECT_PANEL` at line 31 / `GTE_ENABLE_PLUGINS` at line 116 —
re-locate both via `search_in_dir` for `option(GTE_ENABLE_` first):

```cmake
# editor-core-separation-11 campaign (Project Assembly system), PHASE3 -
# master switch for the WHOLE Project Assembly system: the CMake
# auto-discovery block below (Step 6) AND the runtime
# Core::LoadProjectAssemblies() call site (PHASE5's own EditorHost.cpp
# edit) are BOTH gated by this SAME flag - mirrors GTE_ENABLE_PLUGINS'
# own identical "one flag gates both the CMake side and the runtime call
# site" shape exactly. Deliberately its OWN, independent flag - never
# reuses GTE_ENABLE_PLUGINS, which controls the completely unrelated
# gte_plugin_abi system (see cmake/MingwRuntime.cmake's own
# gte_apply_project_assembly_shared_crt_linkage(), Step 3 below, for why
# conflating the two flags is a real, confirmed hazard, not a style
# preference).
option(GTE_ENABLE_PROJECT_ASSEMBLIES "Auto-discover and load per-developer Projects/<Name>/ Project Assembly .dll's (editor-core-separation-11)" ON)
```

## STEP 2 — modify `GreatTamanaEditor`'s own target (the Locked Architecture
Decision, LDD2, now applied for real, not just probed)

Re-locate the real `add_executable(GreatTamanaEditor src/main.cpp)` call
(confirmed 2026-09-28: root `CMakeLists.txt` line 1298, inside an
`if(NOT GTE_CORE_STANDALONE_PROBE_ONLY)` guard opened at line 1296),
immediately followed by `target_link_libraries(GreatTamanaEditor PRIVATE
gte_editor)` (confirmed line 1313). Immediately after that
`target_link_libraries` call, add:

```cmake
# editor-core-separation-11 campaign (Project Assembly system), PHASE3 - the
# Locked Architecture Decision (LDD2, PHASE0_MASTER_STRATEGY.md): makes this
# executable export symbols so a Project Assembly .dll (which links THIS
# target, never gte_core/gte_editor directly) resolves its unresolved
# gte_core/gte_editor references straight back into this ALREADY-RUNNING
# image - exactly one physical copy of every gte_core/gte_editor global in
# the process. WINDOWS_EXPORT_ALL_SYMBOLS is REQUIRED, not optional - see
# PHASE2_ENABLE_EXPORTS_FEASIBILITY_PROBE.md, Step 3/4, for the mechanical
# proof this property is load-bearing on this repo's real MinGW/GNU-ld
# toolchain (ENABLE_EXPORTS alone does not export non-dllexport-marked
# symbols on Windows/PE).
set_target_properties(GreatTamanaEditor PROPERTIES
    ENABLE_EXPORTS ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)
```

**Verify immediately, mechanically, that this alone did not break the
existing build**: `cmake --build build --target GreatTamanaEditor`, confirm
it still runs (`run_app_background` + `GET /get_logs` + `GET /get_swapchain`
+ `stop_app_background`), confirm the existing plugin `.dll`s still load
(`ENABLE_EXPORTS` changes how the linker emits the executable's own import
library — it should not change the executable's own runtime behavior, but
this repo's own house discipline says verify mechanically, never assume).

## STEP 3 — new function in the EXISTING `cmake/MingwRuntime.cmake`:
`gte_apply_project_assembly_shared_crt_linkage()` (Finding D — resolves a
real, confirmed hazard the original design docs never found)

**Do not reuse `gte_apply_plugin_dll_shared_crt_linkage()` for a Project
Assembly target — it is gated by `GTE_ENABLE_PLUGINS` (confirmed,
`cmake/MingwRuntime.cmake` lines 176-178:
`if(NOT GTE_ENABLE_PLUGINS) return() endif()`), a DIFFERENT, unrelated
system's flag.** Reusing it as-is means a developer who sets
`GTE_ENABLE_PLUGINS=OFF` while wanting Project Assemblies gets a silently
CRT-mismatched `.dll`, with a log message that never even mentions "Project
Assembly." Add this NEW function to the end of the existing
`cmake/MingwRuntime.cmake` (never a new file — this file already owns every
CRT-linkage helper; adding a sibling function here, not a parallel file, is
the correct home):

```cmake
# editor-core-separation-11 campaign (Project Assembly system), PHASE3
# (PHASE0_MASTER_STRATEGY.md, Finding D) - the variant of
# gte_apply_plugin_dll_shared_crt_linkage() every Project Assembly _Game.dll/
# _Editor.dll TARGET must call instead. Applies the exact SAME link-time CRT
# flip (-shared-libgcc) but is gated ONLY by GTE_ENABLE_PROJECT_ASSEMBLIES +
# GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED - NEVER by GTE_ENABLE_PLUGINS,
# which controls the completely separate, unrelated gte_plugin_abi system.
# Reusing gte_apply_plugin_dll_shared_crt_linkage() as-is here would silently
# leave a Project Assembly .dll CRT-mismatched (and therefore genuinely
# unsafe for the real std::string/std::vector cross-boundary traffic this
# whole system exists for) the moment a developer sets GTE_ENABLE_PLUGINS=OFF
# for the unrelated OTHER system, with a log message that never even
# mentions "Project Assembly" - a real, confirmed hazard this function
# exists specifically to close. Deliberately does NOT call
# mingw_copy_runtime_dll() here (mirrors gte_apply_plugin_dll_shared_crt_linkage()'s
# own identical reasoning): a Project Assembly .dll's own
# RUNTIME_OUTPUT_DIRECTORY (GTE_PROJECT_ASSEMBLY_OUTPUT_DIR, Step 5 below) is
# NEVER scanned by ProjectAssemblyHost for anything other than
# "*_Game.dll"/"*_Editor.dll" (PHASE5) - unlike PluginHost, which scans
# EVERY *.dll in its own folder - so copying the 3 runtime DLLs there would
# not itself break anything, but is still unnecessary: GreatTamanaEditor.exe
# already stages its own copy of these same 3 DLLs (via the ORIGINAL,
# unchanged gte_apply_plugin_shared_crt_linkage() call already applied to it
# elsewhere, or, if that call was never applied because GTE_ENABLE_PLUGINS
# is OFF, via a DIRECT mingw_copy_runtime_dll(GreatTamanaEditor) call this
# phase adds unconditionally - see Step 3.1 immediately below), and Windows'
# own DLL search order already includes "the directory the loading
# APPLICATION's own .exe is in" for any DLL a loaded .dll's own transitive
# dependency resolves by bare name.
function(gte_apply_project_assembly_shared_crt_linkage target_name)
    if(NOT GTE_ENABLE_PROJECT_ASSEMBLIES)
        return()
    endif()
    if(NOT GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED)
        message(WARNING "gte_apply_project_assembly_shared_crt_linkage(${target_name}): active toolchain has no shared libstdc++ variant - honest no-op, ${target_name} stays statically linked (see this file's own top-of-file comment).")
        return()
    endif()
    target_link_options(${target_name} PRIVATE -shared-libgcc)
endfunction()
```

**Step 3.1 — a real, confirmed edge case this new function's own comment
above depends on: does `GreatTamanaEditor` itself unconditionally get the 3
runtime DLLs staged next to it, REGARDLESS of `GTE_ENABLE_PLUGINS`?** Check
this concretely: `search_in_dir` for
`gte_apply_plugin_shared_crt_linkage(GreatTamanaEditor` in root
`CMakeLists.txt`. If this call exists and is itself gated by
`GTE_ENABLE_PLUGINS` being `ON` (it will be, since the function itself
early-returns when that flag is `OFF`), then a `GTE_ENABLE_PLUGINS=OFF` +
`GTE_ENABLE_PROJECT_ASSEMBLIES=ON` configuration would leave
`GreatTamanaEditor.exe` itself WITHOUT the 3 runtime DLLs staged next to it —
a real problem for Project Assemblies too, since they depend on the SAME
process-wide shared CRT the `.exe` itself needs to have loaded correctly.
**Resolve this by having PHASE3 call `mingw_copy_runtime_dll(GreatTamanaEditor)`
directly, once, unconditionally (gated only by
`GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED`, never by either
`GTE_ENABLE_PLUGINS` or `GTE_ENABLE_PROJECT_ASSEMBLIES`), immediately after
Step 2's `set_target_properties` block above**, e.g.:

```cmake
if(GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED)
    mingw_copy_runtime_dll(GreatTamanaEditor)
endif()
```

Confirm this does not DOUBLE-stage the 3 DLLs (harmless if it does — CMake's
`copy_if_different` is idempotent — but confirm no build warning/error
results from calling it twice if `GTE_ENABLE_PLUGINS` is also `ON`).

## STEP 4 — NEW FILE: `cmake/GteProject.cmake`

```cmake
# cmake/GteProject.cmake
#
# editor-core-separation-11 campaign (Project Assembly system), PHASE3.
# Defines gte_add_project(<Name>) - called exactly once by each Project
# Assembly's own generated Projects/<Name>/Libraries/CMakeLists.txt. Mirrors
# this repo's OWN cmake/CompileShaders.cmake / cmake/MingwRuntime.cmake "one
# generic function, called by every real consumer" shape. include()-ed via
# the bare module name (include(GteProject), CMAKE_MODULE_PATH already
# includes cmake/ - root CMakeLists.txt line 118) - NEVER
# include(cmake/GteProject.cmake), which is not this repo's own convention
# (see include(CompileShaders)/include(MingwRuntime) for the precedent this
# matches).

function(gte_add_project NAME)
    # CMAKE_CURRENT_SOURCE_DIR here is Projects/<Name>/Libraries/ (the
    # folder this function is called FROM, via add_subdirectory() - Step 6
    # below) - ".." is therefore Projects/<Name>/ itself.
    set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
    set(ASSETS "${PROJECT_ROOT}/Assets")

    if(NOT IS_DIRECTORY "${ASSETS}")
        message(WARNING "gte_add_project(${NAME}): no Assets/ folder found under ${PROJECT_ROOT} - nothing to build.")
        return()
    endif()

    file(GLOB_RECURSE ALL_CPP CONFIGURE_DEPENDS "${ASSETS}/*.cpp")

    set(GAME_SOURCES "")
    set(EDITOR_SOURCES "")
    foreach(SRC ${ALL_CPP})
        # Any path containing a path-segment literally named "Editor", at any
        # depth, goes to the Editor assembly instead of the Game one.
        # Case-sensitive, matching this engine's own existing case-sensitive
        # folder-name conventions elsewhere.
        if(SRC MATCHES "/Editor/")
            list(APPEND EDITOR_SOURCES ${SRC})
        else()
            list(APPEND GAME_SOURCES ${SRC})
        endif()
    endforeach()

    if(GAME_SOURCES)
        add_library(${NAME}_Game SHARED ${GAME_SOURCES})
        target_link_libraries(${NAME}_Game PRIVATE GreatTamanaEditor)
        gte_project_assembly_apply_header_paths(${NAME}_Game)
        gte_apply_project_assembly_shared_crt_linkage(${NAME}_Game)
        set_target_properties(${NAME}_Game PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${GTE_PROJECT_ASSEMBLY_OUTPUT_DIR}"
            PREFIX "")
        gte_add_project_shaders(${NAME}_Game "${ASSETS}")
    else()
        message(STATUS "gte_add_project(${NAME}): no non-Editor .cpp files found under ${ASSETS} - ${NAME}_Game.dll not created.")
    endif()

    if(EDITOR_SOURCES)
        add_library(${NAME}_Editor SHARED ${EDITOR_SOURCES})
        target_link_libraries(${NAME}_Editor PRIVATE GreatTamanaEditor)
        gte_project_assembly_apply_header_paths(${NAME}_Editor)
        gte_project_assembly_apply_editor_header_paths(${NAME}_Editor) # PHASE7 - imgui/imguizmo headers only, _Editor targets only
        gte_apply_project_assembly_shared_crt_linkage(${NAME}_Editor)
        set_target_properties(${NAME}_Editor PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${GTE_PROJECT_ASSEMBLY_OUTPUT_DIR}"
            PREFIX "")
        gte_add_project_shaders(${NAME}_Editor "${ASSETS}")
    endif()
endfunction()
```

`gte_add_project_shaders()` is called here but defined in PHASE4 — implement
PHASE4 immediately after this phase (they are tightly coupled and small); do
not leave a permanent stub. `gte_project_assembly_apply_editor_header_paths()`
is called here but defined in PHASE7 — until PHASE7 exists, either stub it as
a no-op or implement PHASE7's Step A1 alongside this phase (both are tiny);
delete any stub the moment the real phase lands.

**Verify before trusting:** `target_link_libraries(${NAME}_Game PRIVATE
GreatTamanaEditor)` requires `GreatTamanaEditor` to already be a real,
defined CMake target at the point Step 6's `add_subdirectory()` runs (unlike
a generator-expression reference, `target_link_libraries` needs the target to
already exist) — confirm Step 6's discovery block is placed textually AFTER
Step 2's edit, inside the SAME `if(NOT GTE_CORE_STANDALONE_PROBE_ONLY)` guard
(re-locate that guard's matching `endif()` — do not place the discovery block
outside it, or `GreatTamanaEditor` will not exist there when
`GTE_CORE_STANDALONE_PROBE_ONLY=ON`).

## STEP 5 — the header-propagation fix (Finding C) — MUST be done here, or
nothing a Project Assembly includes will compile past its first third-party
angle-bracket include

`target_link_libraries(${NAME}_Game PRIVATE GreatTamanaEditor)` alone gives
SYMBOL resolution (LDD2's hazard fix) but ZERO propagated header search
paths, because `GreatTamanaEditor`'s own link to `gte_editor` is `PRIVATE`
(confirmed, line 1313). Fix via `$<TARGET_PROPERTY:...,INTERFACE_INCLUDE_DIRECTORIES>`
generator expressions — reads a target's own recorded include-directory
USAGE REQUIREMENT without calling `target_link_libraries()` against it, so it
adds search paths only, never a second compiled copy of that target's code.
Add this to `cmake/GteProject.cmake`, alongside `gte_add_project()`:

```cmake
# editor-core-separation-11 campaign (Project Assembly system), PHASE3
# (PHASE0_MASTER_STRATEGY.md, Finding C) - GreatTamanaEditor's own link to
# gte_editor is PRIVATE, so linking GreatTamanaEditor alone propagates ZERO
# header search paths to a Project Assembly target. This function grabs
# ONLY the include-directory usage requirement of every dependency a
# Project Assembly's own #include chain can reach through gte_core/
# gte_editor's real headers - NEVER re-linking any of these targets' own
# compiled code (that would reintroduce the duplicate-globals hazard for
# that one dependency). FULL, verified dependency list as of PHASE3
# (re-verify against the real, current root CMakeLists.txt before trusting
# unchanged - dependencies can be added later): gte_core links
# volk/vma/stb_image/stb_image_write/KTX::ktx/httplib/nlohmann_json PUBLIC
# (line ~1176), gte_plugin_abi PUBLIC (line ~1186, its own header comment
# there states the exact same "any consumer of gte_core needs this
# propagated too" reasoning this function exists for - PluginHost.h, which
# Core.h transitively #includes, needs it), and Threads::Threads PUBLIC
# (line ~1199, included below purely for completeness/symmetry with that
# same reasoning - it is a link-only imported target with no real headers
# of its own, so this entry is a harmless no-op in practice, not a
# functional requirement). gte_core's ONE PRIVATE dependency, saba_pmx (plus
# its own glm dependency), is correctly NEVER listed here - PRIVATE never
# propagates, and PmxLoader.h's/VmdLoader.h's public APIs never leak a
# saba::/glm:: type, so no consumer of gte_core (including a Project
# Assembly) needs its headers. gte_editor links imgui/imguizmo PRIVATE
# (handled separately, PHASE7 only, _Editor targets only - see
# gte_project_assembly_apply_editor_header_paths()) and SDL3::SDL3 PUBLIC
# (line ~1114).
function(gte_project_assembly_apply_header_paths TARGET_NAME)
    target_include_directories(${TARGET_NAME} PRIVATE
        $<TARGET_PROPERTY:gte_core,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:gte_editor,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:volk,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:vma,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:stb_image,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:stb_image_write,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:KTX::ktx,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:httplib,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:nlohmann_json,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:gte_plugin_abi,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:Threads::Threads,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>
    )
endfunction()
```

**Verify this concretely, do not trust it from reading alone:** build the
`ProjectAssemblyProbe_Game` test target (Step 9 below) with `HelloGame.cpp`
temporarily changed to add `#include <volk.h>` at the very top, BEFORE this
step existed vs. AFTER — confirm it fails without this step's fix and
succeeds with it. Remove the temporary `#include` afterward (the real
`HelloGame.cpp` from Step 9 already includes `Core.h`, which itself pulls in
`<volk.h>` transitively — the same fact this manual test proves directly).

## STEP 6 — new output-folder variable + auto-discovery block in root
`CMakeLists.txt`

Add near the existing `GTE_PLUGIN_RUNTIME_OUTPUT_DIR` definition (confirmed
lines 802-805):

```cmake
# editor-core-separation-11 campaign (Project Assembly system), PHASE3 -
# deliberately a SEPARATE folder from GTE_PLUGIN_RUNTIME_OUTPUT_DIR (<exe
# dir>/plugins/): the two coexisting systems (gte_plugin_abi .dll plugins vs.
# Project Assembly .dll's) must never share one scan folder, so neither
# system's loader (PluginHost vs. PHASE5's ProjectAssemblyHost) can ever
# confuse the other's .dll for its own. Deliberately NOT wrapped in the same
# if(GTE_CORE_STANDALONE_PROBE_ONLY)/else()/endif() split
# GTE_PLUGIN_RUNTIME_OUTPUT_DIR itself uses (confirmed lines 801-805) - that
# split exists ONLY because GTE_PLUGIN_RUNTIME_OUTPUT_DIR is genuinely
# CONSUMED even in a GTE_CORE_STANDALONE_PROBE_ONLY=ON configure (demo
# plugin subdirectories are add_subdirectory()-ed unconditionally elsewhere,
# see the comment immediately above add_subdirectory(plugins/gte_plugin_abi)
# in root CMakeLists.txt), where GreatTamanaEditor never exists as a target
# and a literal $<TARGET_FILE_DIR:GreatTamanaEditor> generator expression
# WOULD be a real generate-time error if actually evaluated against a real
# target property. GTE_PROJECT_ASSEMBLY_OUTPUT_DIR has no such consumer in
# that configuration - its ONLY consumer is Step 6's own auto-discovery
# block below, which is placed INSIDE the SAME if(NOT
# GTE_CORE_STANDALONE_PROBE_ONLY) guard as GreatTamanaEditor's own
# definition - so this plain, unconditional set() is safe: an unused CMake
# variable holding an un-evaluated generator-expression string is inert,
# never checked against real targets unless actually attached to a real
# target property at generate time.
set(GTE_PROJECT_ASSEMBLY_OUTPUT_DIR "$<TARGET_FILE_DIR:GreatTamanaEditor>/project_assemblies")
```

Add ONE `include()` line near the existing `include(CompileShaders)` /
`include(MingwRuntime)` group (confirmed lines 127/135 — **bare module name,
`include(GteProject)`, per LDD10, never `include(cmake/GteProject.cmake)`**):

```cmake
include(GteProject)
```

Then add the auto-discovery loop itself, placed AFTER Step 2's edit, still
inside the SAME `if(NOT GTE_CORE_STANDALONE_PROBE_ONLY)` guard (re-locate its
`endif()` before adding this — do not place it after that `endif()`):

```cmake
# editor-core-separation-11 campaign (Project Assembly system), PHASE3.
# CONFIGURE_DEPENDS makes a brand-new/removed Projects/<Name>/ folder
# automatically re-run CMake's configure step before the next build - no
# manual "reconfigure" step needed to pick up a newly-created project. Gated
# by GTE_ENABLE_PROJECT_ASSEMBLIES (LDD9) - mirrors GTE_ENABLE_PLUGINS' own
# identical "gate both the CMake side and the runtime call site with one
# flag" shape. Must run AFTER GreatTamanaEditor/gte_core/gte_editor are
# fully defined (their targets must already exist for
# target_link_libraries(... PRIVATE GreatTamanaEditor) inside a project's
# own Libraries/CMakeLists.txt to resolve).
if(GTE_ENABLE_PROJECT_ASSEMBLIES AND IS_DIRECTORY "${CMAKE_SOURCE_DIR}/Projects")
    file(GLOB GTE_PROJECT_DIRS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Projects/*")
    foreach(GTE_PROJECT_DIR ${GTE_PROJECT_DIRS})
        if(IS_DIRECTORY "${GTE_PROJECT_DIR}" AND EXISTS "${GTE_PROJECT_DIR}/Libraries/CMakeLists.txt")
            add_subdirectory("${GTE_PROJECT_DIR}/Libraries")
        endif()
    endforeach()
endif()
```

## STEP 7 — `.gitignore` entry

```
# Project Assembly system (editor-core-separation-11 campaign) - per-
# developer, in-progress user C++/shader source living OUTSIDE the engine's
# own committed src/ tree. Never committed - same-toolchain, same-build-run
# scoped by design (see task_manager/editor-core-separation-11/PHASE0_MASTER_STRATEGY.md,
# LDD3).
/Projects/
```

## STEP 8 — new authoring-sugar header:
`cmake/templates/ProjectAssemblyExports.h`

This header is NOT itself compiled by CMake — it is copied, verbatim, into
every real project's own `Libraries/` folder (a future scaffolding tool is
explicitly out of scope, LDD/Non-Goals — copy it by hand for the one test
project below). Keep one canonical copy at `cmake/templates/ProjectAssemblyExports.h`:

```cpp
// ProjectAssemblyExports.h
//
// Copied verbatim into every Project Assembly's own Libraries/ folder -
// canonical source: cmake/templates/ProjectAssemblyExports.h.
// editor-core-separation-11 campaign (Project Assembly system), PHASE3.
// Every _Game.dll/_Editor.dll exports exactly ONE fixed, fingerprint-free
// entry point - no ABI-versioning ceremony needed (these two binaries are
// guaranteed same-toolchain/same-build by construction, unlike a
// gte_plugin_abi .dll).
#pragma once

namespace gte { class Core; class EditorHost; }

// Use this macro in exactly ONE .cpp file per _Game.dll/_Editor.dll target.
// TWO variants exist (never one, conditionally-parameterized) because C++
// preprocessor macros cannot conditionally add/remove a function parameter
// based on which target is building - see
// PHASE5_PROJECTASSEMBLYHOST_RUNTIME_LOADER.md for exactly how
// ProjectAssemblyHost resolves and calls this export, and why the _Game
// vs. _Editor parameter-count difference is real and load-bearing, not
// cosmetic.
#define GTE_DEFINE_PROJECT_EXPORTS_GAME(RegisterFn) \
    extern "C" __declspec(dllexport) void GTE_RegisterProject(gte::Core& core) { RegisterFn(core); }

#define GTE_DEFINE_PROJECT_EXPORTS_EDITOR(RegisterFn) \
    extern "C" __declspec(dllexport) void GTE_RegisterProject(gte::Core& core, gte::EditorHost& editorHost) { RegisterFn(core, editorHost); }
```

## STEP 9 — create the test project and prove the whole phase end-to-end

```
Projects/
  ProjectAssemblyProbe/
    Assets/
      HelloGame.cpp
      Editor/
        HelloEditorPanel.cpp
    Libraries/
      CMakeLists.txt
      ProjectAssemblyExports.h   <- copied from Step 8's canonical file
```

`Libraries/CMakeLists.txt` (the ENTIRE file):

```cmake
gte_add_project(ProjectAssemblyProbe)
```

`Assets/HelloGame.cpp` (minimal — proves `_Game.dll` compiles and links,
nothing more; PHASE5 proves this is a real, live `Core&`; PHASE8 replaces
this file's body entirely):

```cpp
// editor-core-separation-11 campaign (Project Assembly system), PHASE3 -
// minimal proof this project's own _Game.dll compiles/links against
// GreatTamanaEditor. Replaced with real capability code in PHASE8.
#include "../Libraries/ProjectAssemblyExports.h"
#include "../../../src/Core/Core.h"

namespace {
void RegisterProbeGame(gte::Core& core) {
    (void)core;
}
}

GTE_DEFINE_PROJECT_EXPORTS_GAME(RegisterProbeGame)
```

`Assets/Editor/HelloEditorPanel.cpp` (same idea, `_Editor.dll` side; PHASE7
replaces this file's body entirely). **Confirm the exact real header path for
`EditorHost` before writing this `#include`** — confirmed 2026-09-28:
`src/Editor/EditorHost.h` line 67, `class EditorHost {` — re-verify this is
still correct via `search_in_dir` before trusting it unchanged:

```cpp
#include "../../Libraries/ProjectAssemblyExports.h"
#include "../../../../src/Core/Core.h"
#include "../../../../src/Editor/EditorHost.h"

namespace {
void RegisterProbeEditor(gte::Core& core, gte::EditorHost& editorHost) {
    (void)core;
    (void)editorHost;
}
}

GTE_DEFINE_PROJECT_EXPORTS_EDITOR(RegisterProbeEditor)
```

Reconfigure the whole repo (the one time a full reconfigure is expected —
`CONFIGURE_DEPENDS` in Step 6 is what makes this exact new-folder-appearing
case work automatically going forward), then build:

```
cmake -S . -B build
cmake --build build --target ProjectAssemblyProbe_Game --target ProjectAssemblyProbe_Editor
```

Confirm both `.dll`s land in `build/project_assemblies/` (NOT
`build/plugins/`), named exactly `ProjectAssemblyProbe_Game.dll` /
`ProjectAssemblyProbe_Editor.dll` (no `lib` prefix — the `PREFIX ""` property
prevents MinGW's default prefix, matching this repo's own existing plugin
`.dll`s).

## Definition of Done

- [ ] `GTE_ENABLE_PROJECT_ASSEMBLIES` option exists, default `ON`, matching
      house style.
- [ ] `GreatTamanaEditor` has `ENABLE_EXPORTS ON` + `WINDOWS_EXPORT_ALL_SYMBOLS ON`,
      and PHASE1's own build/probe/launch checklist still passes after this
      change.
- [ ] `cmake/MingwRuntime.cmake` has the new
      `gte_apply_project_assembly_shared_crt_linkage()` function, gated
      ONLY by `GTE_ENABLE_PROJECT_ASSEMBLIES` + toolchain support — never by
      `GTE_ENABLE_PLUGINS`. `GreatTamanaEditor` itself unconditionally stages
      its 3 runtime DLLs regardless of `GTE_ENABLE_PLUGINS` (Step 3.1).
- [ ] `cmake/GteProject.cmake` exists, `include(GteProject)`-ed once (bare
      module name), defines `gte_add_project()` +
      `gte_project_assembly_apply_header_paths()` exactly as above.
- [ ] `GTE_PROJECT_ASSEMBLY_OUTPUT_DIR` exists, distinct from
      `GTE_PLUGIN_RUNTIME_OUTPUT_DIR`, confirmed by listing both folders
      after a build and seeing different `.dll`s in each.
- [ ] The auto-discovery block exists inside the correct `if(NOT
      GTE_CORE_STANDALONE_PROBE_ONLY)` guard, after `ENABLE_EXPORTS` is set,
      gated by `GTE_ENABLE_PROJECT_ASSEMBLIES`; a fresh
      `Projects/ProjectAssemblyProbe/` folder is picked up by the next
      `cmake --build` with no manual reconfigure (test explicitly: delete
      the folder, build once, recreate it, build again with no explicit
      `cmake -S . -B build` in between — confirm `CONFIGURE_DEPENDS`
      triggers the reconfigure automatically).
- [ ] `cmake/templates/ProjectAssemblyExports.h` has both
      `GTE_DEFINE_PROJECT_EXPORTS_GAME`/`_EDITOR` macros.
- [ ] The manual `#include <volk.h>` test (Step 5) concretely proves the
      header-propagation fix is load-bearing, not decorative.
- [ ] `Projects/ProjectAssemblyProbe/` builds both `.dll`s successfully into
      `project_assemblies/`, no `lib` prefix.

## What this phase does NOT do

- Does NOT actually load `ProjectAssemblyProbe_*.dll` into the running
  engine — nothing calls `GTE_RegisterProject` yet. PHASE5.
- Does NOT implement any real capability — `RegisterProbeGame`/
  `RegisterProbeEditor` are intentionally empty. PHASE7/PHASE8.
- Does NOT build a scaffolding tool that creates this folder structure
  automatically — a human creates `Projects/<Name>/{Assets,Libraries}` by
  hand, following this phase's exact recipe.
