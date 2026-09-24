# PHASE1 — `gte_plugin_abi` Foundation: the Fingerprint, `IPluginModule`, and the CRT-Linkage Flip

**Parent**: `PHASE0_MASTER_STRATEGY.md` — read it in full first, especially
Step 3's Locked Design Decisions #2-#7, before touching any file below.

Maps to the source design doc's Milestone 0 (design half — the "prove the
handshake" probe itself is PHASE2's job).

---

## Step 1: The Goal (Where are we going, this phase specifically?)

Stand up the ONE thing every later phase depends on: `gte_plugin_abi`, the
frozen, versioned, minimal contract a host process and a plugin `.dll` both
compile against for anything that crosses the process/binary boundary. By
the end of this phase:

- `plugins/gte_plugin_abi/` exists, containing self-contained headers with
  zero dependency on any real `gte_core`/`gte_editor` header (Locked Design
  Decision #3).
- `GtePluginAbiFingerprint` is a real, POD struct, generated from this exact
  build's own real compiler id/version/config at CMake configure time — not
  hand-typed, not guessed.
- `IPluginModule` (the one interface every plugin implements directly) and
  `GtePluginModuleInfo` exist, matching the source design doc's Section 3.2
  exactly.
- The three fixed `extern "C"` exports (Section 3.1) are documented as a
  concrete C++ function-pointer-typedef contract every later phase's plugin
  project implements against.
- The engine's own build flips from static-linked libgcc/libstdc++ to
  shared, with a new runtime-DLL-staging CMake step so the built `.exe`
  keeps running on a machine without MinGW installed.
- `docs/conventions/plugin-architecture.md` exists (new file, mirroring this
  codebase's own established `docs/conventions/*.md` + `AGENTS.md`
  short-summary pattern), documenting this whole system for future readers —
  this is real, load-bearing documentation this campaign's own code changes
  require, not optional polish.

**Nothing loads a `.dll` yet.** That is PHASE2's job. This phase is pure
foundation: types, the fingerprint, and the CRT-linkage change every later
phase's actual DLL-loading code depends on being already true.

---

## Step 2: The Situation (what exists right now, confirmed by direct reading)

- Root `CMakeLists.txt` sets `CMAKE_CXX_STANDARD 20`, targets Windows only
  (`if(NOT WIN32) message(FATAL_ERROR ...)`), and never sets
  `CMAKE_CXX_FLAGS`/`-static`/`-static-libgcc`/`-static-libstdc++` explicitly
  anywhere in the file today (confirmed via `search_in_dir`) — meaning the
  MinGW/GCC toolchain's own DEFAULT static-runtime-linking behavior is what
  currently produces a build with zero runtime DLL dependency (confirmed
  mechanically via `objdump -p` on the real built `GreatTamanaEditor.exe`:
  imports only `SDL3.dll`/`KERNEL32.dll`/`msvcrt.dll`/`SHELL32.dll`/
  `USER32.dll`/`WS2_32.dll` — zero `libstdc++-6.dll`/`libgcc_s_seh-1.dll`/
  `libwinpthread-1.dll`).
- `cmake/FetchSDL3.cmake` defines `sdl3_copy_runtime_dll(<target>)` — a
  `POST_BUILD` custom command copying `SDL3.dll` next to a built target's
  `.exe`. `CMakeLists.txt` line ~1203 calls it for `GreatTamanaEditor`;
  `tests/CMakeLists.txt` calls it for `GreatTamanaEngineTests`. This is the
  EXACT precedent this phase's own new "stage the MinGW runtime DLLs" step
  mirrors.
- `tools/ci/gte_core_player_link_probe/CMakeLists.txt` and
  `tools/ci/gte_core_standalone_probe/CMakeLists.txt` both use the
  "top-level, separate `project()`, `add_custom_target(... ALL COMMAND
  ${CMAKE_COMMAND} -S <repo root> -B <nested build dir> ...)`" nested-cmake-
  invocation pattern instead of `add_subdirectory()` — this phase's own new
  demo-plugin-adjacent `plugins/` folder does NOT need this pattern (plugin
  `.dll` projects are genuinely independent small CMake projects that only
  need `gte_plugin_abi`'s own headers, never the full repo re-configured) —
  see PHASE2 for exactly how a demo plugin project is wired in instead
  (a plain `add_subdirectory("plugins/demo_hello_world")` from the ROOT
  `CMakeLists.txt`, gated by `GTE_ENABLE_PLUGINS`).

---

## Step 3: The Plan

### 3.1 — New folder: `plugins/gte_plugin_abi/`

Create these files (all header-only, zero `.cpp` — every interface method is
pure virtual, matching `src/Core/EditorCapabilities.h`'s own existing
"header-only, no .cpp" precedent for pure-virtual-only headers):

**`plugins/gte_plugin_abi/GtePluginAbiFingerprint.h`**

```cpp
#pragma once

#include <cstdint>

// The frozen, versioned handshake every plugin .dll and the host process
// both compile against - see PHASE0_MASTER_STRATEGY.md's Locked Design
// Decision #5 and the source design doc's Section 8. A fixed-size POD,
// safe to read via GetProcAddress()+call, safe to memcmp() byte-for-byte.
// NEVER add a non-trivial member (std::string, a pointer, a vtable) to this
// struct - every field must be a fixed-size, trivially-copyable primitive.
namespace gte {

struct GtePluginAbiFingerprint {
    // Bumped BY HAND, in this exact header, whenever gte_plugin_abi itself
    // changes in a way that isn't purely additive (an existing interface's
    // method signature changes, a struct field is removed/reordered, ...).
    // Adding a brand-new interface/capability version string is NOT a
    // reason to bump this - see docs/conventions/plugin-architecture.md.
    std::uint32_t abiContractGeneration;

    // "GNU" for GCC/MinGW (this repository's only supported toolchain
    // today - see CMakeLists.txt's own WIN32-only gate). Not
    // null-terminated beyond its own fixed size if the id is ever exactly
    // 15 characters - always compare with memcmp() over the full fixed
    // width, never strcmp(), so a non-terminated edge case can never read
    // out of bounds.
    char compilerId[16];

    // From CMAKE_CXX_COMPILER_VERSION, split into three integers (baked in
    // via configure_file() at CMake configure time - see this phase's own
    // CMakeLists.txt changes below). A mismatch in ANY of the three fields
    // is a real fingerprint mismatch - this project does not attempt to
    // reason about "close enough" compiler versions, per the source design
    // doc's own Section 0.1 "no interpretation, no close enough" rule.
    std::uint32_t compilerVersionMajor;
    std::uint32_t compilerVersionMinor;
    std::uint32_t compilerVersionPatch;

    // "Debug" / "Release" / "RelWithDebInfo" / "MinSizeRel" - from
    // CMAKE_BUILD_TYPE at configure time. A Debug-built plugin loaded by a
    // Release-built host (or vice versa) is a real, historically-common
    // source of silent ODR/struct-layout mismatches (different assert()/
    // iterator-debugging settings) - refused, not merely warned about.
    char buildConfig[16];

    // sizeof(void*) at compile time - a cheap, first-line sanity guard
    // against a genuinely wrong-architecture .dll (e.g. an accidental
    // 32-bit build) being loaded into a 64-bit host process, independent of
    // (and checked before) every other field.
    std::uint32_t pointerSize;

    // 1 if this binary was built with shared (DLL) libgcc/libstdc++
    // linkage, 0 if statically linked - see PHASE0_MASTER_STRATEGY.md's
    // Locked Design Decision #4 and this phase's own Step 3.4 below. The
    // host REFUSES to load ANY plugin - and, more fundamentally, refuses to
    // even attempt plugin loading at all - unless its OWN fingerprint has
    // this field set to 1 (see PluginHost's own doc comment, PHASE2), since
    // a statically-linked host/plugin pair does not share one process-wide
    // heap even if every other field matches exactly.
    std::uint32_t sharedRuntimeLinkage;
};

// Byte-for-byte comparison - the ONLY correct way to compare two
// fingerprints (never compare field-by-field with any "is this close
// enough" logic - see the struct's own top comment).
inline bool operator==(const GtePluginAbiFingerprint& a, const GtePluginAbiFingerprint& b) noexcept
{
    return a.abiContractGeneration == b.abiContractGeneration
        && a.compilerVersionMajor == b.compilerVersionMajor
        && a.compilerVersionMinor == b.compilerVersionMinor
        && a.compilerVersionPatch == b.compilerVersionPatch
        && a.pointerSize == b.pointerSize
        && a.sharedRuntimeLinkage == b.sharedRuntimeLinkage
        && __builtin_memcmp(a.compilerId, b.compilerId, sizeof(a.compilerId)) == 0
        && __builtin_memcmp(a.buildConfig, b.buildConfig, sizeof(a.buildConfig)) == 0;
}

} // namespace gte
```

(`__builtin_memcmp` is fine here — GCC/MinGW is this repository's only
supported toolchain; if the implementer prefers, `<cstring>`'s `std::memcmp`
is equally correct and slightly more portable-looking — either is
acceptable, note the choice made in the completion report.)

**`plugins/gte_plugin_abi/GtePluginAbiFingerprintGenerated.h.in`** — a CMake
`configure_file()` template (new mechanism for this repo — no existing
precedent, but a standard, safe CMake technique):

```cpp
#pragma once

// GENERATED FILE - do not hand-edit. Produced by CMake's configure_file()
// from plugins/gte_plugin_abi/GtePluginAbiFingerprintGenerated.h.in at
// configure time - see plugins/gte_plugin_abi/CMakeLists.txt. Regenerated
// automatically every time CMake reconfigures, so it always reflects the
// REAL, live compiler/build-config values this exact build used - never
// hand-typed, never allowed to drift from reality.

#include "GtePluginAbiFingerprint.h"

namespace gte {

// The ONE frozen "ABI contract generation" integer for this whole
// campaign's initial shape - bump this by hand (and only this - every
// other field below is auto-derived from the real build) the day
// gte_plugin_abi's own interfaces change non-additively. Starts at 1.
inline constexpr std::uint32_t kGtePluginAbiContractGeneration = 1;

inline GtePluginAbiFingerprint MakeThisBuildsFingerprint() noexcept
{
    GtePluginAbiFingerprint fp{};
    fp.abiContractGeneration = kGtePluginAbiContractGeneration;

    constexpr const char* kCompilerId = "@CMAKE_CXX_COMPILER_ID@";
    constexpr const char* kBuildConfig = "@CMAKE_BUILD_TYPE@";
    for (int i = 0; kCompilerId[i] != '\0' && i < 15; ++i) { fp.compilerId[i] = kCompilerId[i]; }
    for (int i = 0; kBuildConfig[i] != '\0' && i < 15; ++i) { fp.buildConfig[i] = kBuildConfig[i]; }

    fp.compilerVersionMajor = @CMAKE_CXX_COMPILER_VERSION_MAJOR@;
    fp.compilerVersionMinor = @CMAKE_CXX_COMPILER_VERSION_MINOR@;
    fp.compilerVersionPatch = @CMAKE_CXX_COMPILER_VERSION_PATCH@;
    fp.pointerSize = static_cast<std::uint32_t>(sizeof(void*));
    fp.sharedRuntimeLinkage = @GTE_PLUGIN_SHARED_RUNTIME_LINKAGE_INT@; // 0 or 1 - see this phase's Step 3.4.
    return fp;
}

} // namespace gte
```

`plugins/gte_plugin_abi/CMakeLists.txt` (new, small — this creates an
`INTERFACE` library target, `gte_plugin_abi`, header-only, that both
`gte_core` and every plugin project link against, so its include path is
never hand-duplicated):

```cmake
# CMAKE_BUILD_TYPE can be empty (a single-config generator with no explicit
# type set) - default it to "Unspecified" rather than leaving the generated
# header's buildConfig field blank, so an empty string never silently
# "matches" a differently-empty string for the wrong reason.
if(NOT CMAKE_BUILD_TYPE)
    set(GTE_PLUGIN_ABI_BUILD_TYPE "Unspecified")
else()
    set(GTE_PLUGIN_ABI_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
endif()

string(REPLACE "." ";" GTE_CXX_COMPILER_VERSION_LIST "${CMAKE_CXX_COMPILER_VERSION}")
list(GET GTE_CXX_COMPILER_VERSION_LIST 0 CMAKE_CXX_COMPILER_VERSION_MAJOR)
list(GET GTE_CXX_COMPILER_VERSION_LIST 1 CMAKE_CXX_COMPILER_VERSION_MINOR)
list(LENGTH GTE_CXX_COMPILER_VERSION_LIST GTE_CXX_COMPILER_VERSION_PARTS)
if(GTE_CXX_COMPILER_VERSION_PARTS GREATER 2)
    list(GET GTE_CXX_COMPILER_VERSION_LIST 2 CMAKE_CXX_COMPILER_VERSION_PATCH)
else()
    set(CMAKE_CXX_COMPILER_VERSION_PATCH 0)
endif()
set(CMAKE_BUILD_TYPE "${GTE_PLUGIN_ABI_BUILD_TYPE}") # for the @CMAKE_BUILD_TYPE@ substitution only, scoped to this file
set(GTE_PLUGIN_SHARED_RUNTIME_LINKAGE_INT 1) # see Step 3.4 below - this project pins shared runtime linkage unconditionally once GTE_ENABLE_PLUGINS exists

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/GtePluginAbiFingerprintGenerated.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"
    @ONLY
)

add_library(gte_plugin_abi INTERFACE)
target_include_directories(gte_plugin_abi INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_compile_features(gte_plugin_abi INTERFACE cxx_std_20)
```

(Re-check `list(GET ...)` behavior if `CMAKE_CXX_COMPILER_VERSION` ever has
fewer than 2 dot-separated parts on the real build machine — GCC version
strings always have at least major.minor in practice, but confirm this with
a real `message(STATUS "CMAKE_CXX_COMPILER_VERSION=${CMAKE_CXX_COMPILER_VERSION}")`
print during this phase's own configure step before trusting it silently.)

**`plugins/gte_plugin_abi/GtePluginModuleInfo.h`**:

```cpp
#pragma once

namespace gte {

// A short, stable, human-readable identity for logs/diagnostics - see the
// source design doc's Section 3.2. Plain fixed-size char buffers, never
// std::string (Locked Design Decision #3, PHASE0_MASTER_STRATEGY.md).
struct GtePluginModuleInfo {
    char name[64] = {};        // e.g. "DemoRenderFeature"
    char version[16] = {};     // e.g. "1.0.0" - free-form, plugin author's own scheme, not compared by the host
    char description[128] = {}; // one-line, human-readable
};

} // namespace gte
```

**`plugins/gte_plugin_abi/IPluginModule.h`**:

```cpp
#pragma once

namespace gte {

struct GtePluginModuleInfo;

// The one interface every plugin implements directly (source design doc,
// Section 3.2). Pure virtual, no data members, no multiple inheritance, no
// exceptions crossing this boundary - safe under Locked Design Decision #3.
class IPluginModule {
public:
    virtual ~IPluginModule() = default;

    // The generic capability lookup (source design doc Section 4.3) -
    // COM/Source-Engine-style string-versioned interface query, e.g.
    // QueryCapability("IRenderFeatureModule_v1"). Returns nullptr for any
    // capability this plugin doesn't implement, or any version string it
    // doesn't recognize - never guesses, never returns a mismatched type
    // through a stale pointer. The returned pointer's REAL static type is
    // always one of gte_plugin_abi's own curated interface types (e.g.
    // IRenderFeatureModule_v1*) - the caller is responsible for calling
    // QueryCapability() with the exact name/version string documented next
    // to that interface's own declaration, and static_cast-ing the result
    // to that exact type, exactly mirroring COM's QueryInterface()
    // discipline.
    virtual void* QueryCapability(const char* capabilityNameAndVersion) = 0;

    virtual void GetModuleInfo(GtePluginModuleInfo& outInfo) const = 0;
};

} // namespace gte
```

**`plugins/gte_plugin_abi/PluginExports.h`** — documents (as C++
function-pointer typedefs, resolved by name via `GetProcAddress()` in
PHASE2's `PluginHost`, never called directly here) the three fixed
`extern "C"` exports every plugin `.dll` must provide:

```cpp
#pragma once

#include "GtePluginAbiFingerprint.h"
#include "IPluginModule.h"

namespace gte {

// Every plugin .dll exports EXACTLY these three extern "C" functions, under
// EXACTLY these three names (case-sensitive) - the ONLY functions ever
// resolved by literal name via GetProcAddress(). See PHASE2's PluginHost
// for the real GetProcAddress() call sites, and every demo plugin project
// (PHASE2/3/4) for the real, exported definitions.
using PFN_GTE_GetPluginAbiFingerprint = GtePluginAbiFingerprint(*)();
using PFN_GTE_CreatePluginModule = IPluginModule*(*)();
using PFN_GTE_DestroyPluginModule = void(*)(IPluginModule*);

inline constexpr const char* kGteGetPluginAbiFingerprintExportName = "GTE_GetPluginAbiFingerprint";
inline constexpr const char* kGteCreatePluginModuleExportName = "GTE_CreatePluginModule";
inline constexpr const char* kGteDestroyPluginModuleExportName = "GTE_DestroyPluginModule";

} // namespace gte
```

Note the deliberate, documented simplification from the source design doc's
own Section 3.1 signature `IPluginModule* GTE_CreatePluginModule(IEngineServices*
hostServices)`: **this campaign's curated capability interfaces (PHASE3's
`IPluginRenderPassBuilder`, PHASE4's `IPluginPanelDrawContext`) are each
handed to a plugin per-call, at the exact point a capability method is
invoked** (e.g. `AddRenderGraphPasses(IPluginRenderPassBuilder&)`) rather
than once, up-front, at construction time via a single monolithic
`IEngineServices*` — there is no host-services object this campaign's own
Milestones 0-3 actually need to hand over at construction time at all
(`GetModuleInfo()`/render-pass-building/panel-drawing need nothing else). If
a genuinely new capability later needs something at construction time,
`GTE_CreatePluginModule`'s signature is exactly where that grows a real
parameter — deliberately NOT invented here, ahead of any real need, per this
codebase's own "don't design for a need you don't have yet" precedent.

### 3.2 — `docs/conventions/plugin-architecture.md` (new)

Write a full convention doc mirroring the shape/tone of every existing file
under `docs/conventions/` (read `docs/conventions/logging.md` or
`docs/conventions/profiling.md` first for the expected length/tone). Must
cover, at minimum: what `gte_plugin_abi` is and why it exists; the fingerprint
gate and why a mismatch is a clean skip, never a crash; the "never link
`gte_core`/`gte_editor` directly, only curated wrapper interfaces" rule and
why (Locked Design Decision #2); where `plugins/` (source) and
`<build-dir>/plugins/` (runtime) each live and what goes in each; the
explicit list of what is deferred (owned-handle mechanism, hot reload) and
why. Add a short summary + link for it in `AGENTS.md`, mirroring every other
subsystem's own existing entry (a single new `## Plugin Architecture`
section, placed after `## gte_core / gte_editor Library Separation` and
before `## Testability & Regression Safety`, matching that file's own
existing "roughly chronological by when the subsystem was introduced" order).

### 3.3 — `plugins/gte_plugin_abi/PublicSurface.md` (new)

A short, explicit, reviewed list (source design doc Section 3.3) of exactly
which types cross the ABI boundary, as of this campaign: `GtePluginAbiFingerprint`,
`GtePluginModuleInfo`, `IPluginModule`, plus (added by PHASE3/PHASE4)
`IRenderFeatureModule_v1`/`IPluginRenderPassBuilder`,
`IEditorPanelModule_v1`/`IPluginPanelDrawContext`. State explicitly: **zero
real `gte_core`/`gte_editor` header is ever included by anything under
`plugins/gte_plugin_abi/`** — confirmed by this phase's own compile check
(building `gte_plugin_abi`'s own headers with `-I` limited to exactly this
folder plus its generated-headers folder, nothing else, must succeed).

### 3.4 — The CRT-linkage flip (Locked Design Decision #4)

1. Confirm today's real default (`objdump -p build/GreatTamanaEditor.exe`,
   already done once during this campaign's own investigation — re-confirm
   fresh at the start of this phase in case the build tree has since
   changed) still shows zero `libstdc++-6.dll`/`libgcc_s_seh-1.dll`/
   `libwinpthread-1.dll` dependency.
2. In root `CMakeLists.txt`, immediately after the existing
   `option(GTE_ENABLE_PROJECT_PANEL ...)`-style option block (a natural,
   consistent insertion point), add:
   ```cmake
   option(GTE_ENABLE_PLUGINS "Compile in the dynamic plugin-loading system (gte_plugin_abi + PluginHost)" ON)
   ```
3. New `cmake/MingwRuntime.cmake` (mirrors `cmake/FetchSDL3.cmake`'s own
   `sdl3_copy_runtime_dll()` shape exactly), defining TWO functions —
   `mingw_copy_runtime_dll()` (the DLL-staging step alone) and a second,
   higher-level `gte_apply_plugin_shared_crt_linkage()` that wraps BOTH the
   link-options flip AND the staging step in one call. **Every target that
   can ever sit on either side of the plugin ABI boundary must call the
   SECOND function — never apply the raw link-options flip by hand to just
   one target and assume that is enough** (see `PHASE0_MASTER_STRATEGY.md`'s
   Locked Design Decision #4, restated: a plugin `.dll` left on the old,
   static CRT while the host it loads into flips to shared CRT is not a safe
   matching pair, even though both may still independently compile and run):
   ```cmake
   # Stages libstdc++-6.dll/libgcc_s_seh-1.dll/libwinpthread-1.dll next to a
   # built target's own binary - the exact three runtime DLLs
   # -shared-libgcc/-shared-libstdc++ makes that target depend on at
   # runtime. Mirrors sdl3_copy_runtime_dll()'s own POST_BUILD
   # copy-if-different shape exactly (cmake/FetchSDL3.cmake). Safe to call
   # on ANY target kind this repo ever builds (an .exe or a plugin .dll
   # alike) - $<TARGET_FILE_DIR:...> resolves correctly for either.
   function(mingw_copy_runtime_dll target_name)
       get_filename_component(GTE_MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
       foreach(dll_name IN ITEMS libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
           if(EXISTS "${GTE_MINGW_BIN_DIR}/${dll_name}")
               add_custom_command(TARGET ${target_name} POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_if_different
                       "${GTE_MINGW_BIN_DIR}/${dll_name}"
                       "$<TARGET_FILE_DIR:${target_name}>/${dll_name}"
                   COMMENT "Staging ${dll_name} next to ${target_name}"
               )
           else()
               message(WARNING "mingw_copy_runtime_dll: ${dll_name} not found next to CMAKE_CXX_COMPILER (${GTE_MINGW_BIN_DIR}) - ${target_name} may fail to start on a machine without this exact MinGW toolchain installed.")
           endif()
       endforeach()
   endfunction()

   # Applies BOTH halves of Locked Design Decision #4 (PHASE0_MASTER_STRATEGY.md)
   # to one target in a single call: the shared/DLL CRT link-time flip AND
   # staging the resulting runtime DLL dependency next to that target's own
   # built binary. `target_name` must already exist (add_library/
   # add_executable already called for it) at the point this is invoked -
   # unlike target_link_libraries()'s own forward-reference tolerance for
   # LIST ITEMS, the target this command is called ON must already be a real
   # CMake target. A no-op when GTE_ENABLE_PLUGINS is OFF, matching that
   # configuration's own fingerprint sharedRuntimeLinkage=0 value. Call this
   # for the host executable (GreatTamanaEditor - see step 4 below) AND for
   # EVERY plugin .dll target (PHASE2/3/4's demo_hello_world/
   # demo_render_feature/demo_editor_panel) AND for every standalone probe
   # executable that ever loads a real plugin .dll (PHASE2's handshake
   # probe, PHASE5's isolation probe, PHASE5's extended
   # gte_core_player_link_probe) - never assume flipping just the host is
   # sufficient.
   function(gte_apply_plugin_shared_crt_linkage target_name)
       if(GTE_ENABLE_PLUGINS)
           target_link_options(${target_name} PRIVATE -shared-libgcc -shared-libstdc++)
           mingw_copy_runtime_dll(${target_name})
       endif()
   endfunction()
   ```
   `list(APPEND CMAKE_MODULE_PATH ...)` already includes `cmake/` (root
   `CMakeLists.txt` line ~104) so `include(MingwRuntime)` resolves
   automatically from anywhere, including from a `plugins/*/CMakeLists.txt`
   subdirectory (CMake functions defined via `include()` are visible
   process-wide once included, regardless of directory scope).
4. `add_executable(GreatTamanaEditor src/main.cpp)` is declared far later in
   root `CMakeLists.txt` than `add_library(gte_core STATIC ...)` — inside
   its own `if(NOT GTE_CORE_STANDALONE_PROBE_ONLY)` guard around the
   "--- Final executable ---" section (confirm the real, current line
   number via `search_in_dir` for `add_executable(GreatTamanaEditor` before
   editing; at the time of this campaign's own investigation it was ~line
   976). **`gte_apply_plugin_shared_crt_linkage(GreatTamanaEditor)` MUST be
   called only AFTER that `add_executable()` call — never near
   `add_library(gte_core STATIC ...)`'s own closing parenthesis, since
   `GreatTamanaEditor` is not yet a defined CMake target at that earlier
   point in the file, in ANY configuration (calling `target_link_options()`
   on a not-yet-existing target name is a hard CMake configure error, not a
   deferred generator-expression lookup).** The natural, correct insertion
   point is immediately before the existing
   `sdl3_copy_runtime_dll(GreatTamanaEditor)` call (confirmed real, current
   line ~1203, immediately before that same `if(NOT
   GTE_CORE_STANDALONE_PROBE_ONLY)` block's own closing `endif()`) — add:
   ```cmake
   gte_apply_plugin_shared_crt_linkage(GreatTamanaEditor)
   sdl3_copy_runtime_dll(GreatTamanaEditor)
   ```
   `GreatTamanaEngineTests` does NOT need this call anywhere in this
   campaign's own scope — it never loads a plugin `.dll` and never
   constructs a real `PluginHost` against a real `.dll` file (confirm this
   is still true via `search_in_dir` before skipping it; if a later phase's
   own investigation finds a genuine new reason it needs to participate in
   the plugin ABI, flag it via `ask_questions` rather than silently adding
   or silently skipping the call).
5. Re-set `GTE_PLUGIN_SHARED_RUNTIME_LINKAGE_INT` (Step 3.1's
   `plugins/gte_plugin_abi/CMakeLists.txt`) to read the real
   `GTE_ENABLE_PLUGINS` value instead of a hardcoded `1`, so a
   `GTE_ENABLE_PLUGINS=OFF` configuration (which never flips the link flag)
   produces a fingerprint whose `sharedRuntimeLinkage` field honestly
   reflects `0` — never claim shared linkage a given configuration didn't
   actually request.
6. **Cross-phase reminder, not this phase's own job to implement**: PHASE2
   defines the first plugin `.dll` (`demo_hello_world`) and a new standalone
   probe executable — both must call `gte_apply_plugin_shared_crt_linkage()`
   on themselves too (see PHASE2's own Step 3.5/3.6), and PHASE5's two new/
   extended probes must do the same. This phase only defines the reusable
   mechanism; it does not yet have any plugin `.dll` target to apply it to.

### 3.5 — Verify the flip actually worked

After step 3.4, do a full incremental rebuild of `GreatTamanaEditor` (target-
scoped, per Universal Rule 4) and re-run `objdump -p` — confirm
`libstdc++-6.dll`/`libgcc_s_seh-1.dll`/`libwinpthread-1.dll` now DO appear in
the import table, and confirm all three are genuinely staged next to the
built `.exe` (`browse_dir` on the build's output folder). Then actually
launch it (`run_app_background`) and confirm via `gte_send_request(
"/get_swapchain")` that it still renders correctly — the flip must be a
behavior-invisible, packaging-only change from the end user's point of view.

---

## Universal Rules

Follow `PHASE0_MASTER_STRATEGY.md`'s Universal Rules exactly (branch
discipline, incremental-build-only, `ask_questions` on genuine ambiguity, no
`delegate_task`, `PHASE1_COMPLETION_REPORT.md` + git commit at the end).

## This phase's own specific ambiguity to flag via `ask_questions` if hit

- If `CMAKE_CXX_COMPILER_VERSION` on the real build machine ever parses into
  something other than a clean `major.minor.patch` (e.g. a distro-specific
  suffix), do not guess a silent fallback — ask.
- If `-shared-libgcc -shared-libstdc++` causes any REAL, existing test or
  the main executable to fail to link/run for a reason unrelated to the
  three staged DLLs (e.g. a symbol visibility issue this phase's own
  investigation didn't anticipate), stop and ask before working around it
  silently.

## Non-Goals for this phase specifically

- No `PluginHost` class yet (PHASE2).
- No actual `LoadLibrary()` call anywhere yet.
- No demo plugin project yet.
