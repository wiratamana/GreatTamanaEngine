# PHASE2 — `PluginHost`, Wired Into `Core`, Plus a Real Hello-World Handshake Probe

**Parent**: `PHASE0_MASTER_STRATEGY.md`. **Predecessor**:
`PHASE1_GTE_PLUGIN_ABI_FOUNDATION.md` — `gte_plugin_abi` must already exist
and compile before starting this phase.

Maps to the source design doc's Milestone 0 (the actual "prove the
handshake" proof).

---

## Step 1: The Goal (Where are we going, this phase specifically?)

By the end of this phase:

- A real `gte::PluginHost` class exists (`src/Core/Plugins/PluginHost.h/.cpp`,
  compiled into `gte_core`), implementing the source design doc's Section 4.1
  loader procedure exactly: enumerate `*.dll` in a folder, resolve the 3 fixed
  exports by name, check the fingerprint, call `GTE_CreatePluginModule()`,
  store the result — never crashing on a missing/incompatible `.dll`, always
  a clean, logged skip.
- `Core` owns one `PluginHost` instance and exposes
  `Core::LoadPlugins(const std::filesystem::path& pluginsDirectory)` (Locked
  Design Decision #9) plus a read accessor,
  `Core::GetPluginHost() -> const PluginHost&` (or equivalent), for PHASE3/4's
  own later consumers.
- One real, trivial demo plugin `.dll` exists (`plugins/demo_hello_world/`),
  implementing `IPluginModule::GetModuleInfo()` and nothing else (no
  capability at all) — proving the ABI boundary works in complete isolation,
  before any real capability exists, exactly mirroring the source design
  doc's own Milestone 0 wording.
- `EditorHost`'s constructor calls `m_core.LoadPlugins(...)` once, pointed at
  `<build-dir>/plugins/` next to the built `.exe`, and the demo plugin's
  `.dll` is confirmed loaded via `GET /get_logs` (a real `GTE_LOG_INFO` line
  naming it, emitted by `PluginHost` itself).
- A tiny, standalone, non-engine probe program mechanically re-proves the
  whole handshake end-to-end outside of `GreatTamanaEditor.exe` entirely —
  loads the same `.dll`, checks the fingerprint, calls `GetModuleInfo()`,
  unloads cleanly, exits `0`.

---

## Step 2: The Situation

- `src/Core/Core.h`/`Core.cpp` (read in full during this campaign's own
  investigation) already has the exact established shape a new "big,
  nullable, host-supplied hook" fits into — see `SetEditorLayerHook()`'s own
  doc comment (Locked Design Decision #8 of `editor-core-separation-1`) for
  the precedent this phase's own `LoadPlugins()` mirrors: a small, explicitly
  host-called method, not a constructor parameter.
- `src/Core/EditorCapabilities.h`'s own top-of-file doc comment already
  states this codebase's established rule: "many small, single-purpose
  interfaces, never one big god interface" — `PluginHost` itself is NOT a
  capability interface (it is a concrete, `gte_core`-owned mechanism class,
  like `Logger`/`GpuMemoryTracker`), so this rule does not constrain its own
  shape, only the capability interfaces it looks up (PHASE3/4).
- `EditorHost.cpp`'s constructor (read in full) already wires five
  automation bridges plus `ISceneIOCapability`/`ILogQueryCapability` in a
  fixed, documented order, ending with `NetworkServer::Start()`. This
  phase's own `m_core.LoadPlugins(...)` call is a natural, small addition to
  that same sequence — see Step 3.4 below for the exact placement.
- No existing file in this repository calls `LoadLibraryW`/`GetProcAddress`/
  `FreeLibrary` (confirmed, Step 2.1 of PHASE0). `<windows.h>` is safe to
  `#include` directly from `src/Core/Plugins/PluginHost.cpp` (this repo
  targets Windows only) — but MUST be included only from the `.cpp`, never
  from `PluginHost.h`, so `<windows.h>`'s notoriously invasive macros
  (`min`/`max`/etc.) never leak into any other `gte_core` header that
  transitively includes `PluginHost.h`. Mirrors this codebase's own existing
  discipline of keeping SDL/ImGui/VMA headers out of widely-included headers
  wherever possible.

---

## Step 3: The Plan

### 3.1 — `src/Core/Plugins/PluginHost.h`

```cpp
#pragma once

#include "../../../plugins/gte_plugin_abi/IPluginModule.h"
#include "../../../plugins/gte_plugin_abi/GtePluginModuleInfo.h"

#include <filesystem>
#include <string>
#include <vector>

namespace gte {

// gte_core's own Plugin Host (source design doc, Section 1/4.1) - scans a
// folder for *.dll, LoadLibraryW()s each, checks the fingerprint, and holds
// the resulting registry. Mechanical and CAPABILITY-AGNOSTIC: this class has
// ZERO built-in knowledge of IRenderFeatureModule_v1/IEditorPanelModule_v1 or
// any other capability - every actual feature interaction happens through
// QueryCapability() results, requested by capability-consuming code
// elsewhere (Core::RegisterOffscreenRenderPipelineProviders() - PHASE3;
// gte_editor's DockLayout - PHASE4), never by this class itself.
//
// Exactly ONE instance exists per process (PHASE0_MASTER_STRATEGY.md's
// Locked Design Decision #8) - owned by Core, populated once via LoadPlugins()
// (Locked Design Decision #9), never re-scanned per frame.
class PluginHost {
public:
    PluginHost() = default;
    ~PluginHost();

    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;
    PluginHost(PluginHost&&) = delete;
    PluginHost& operator=(PluginHost&&) = delete;

    // Enumerates every *.dll directly inside `pluginsDirectory` (no
    // recursion into subfolders), attempts to load each per the source
    // design doc's Section 4.1 procedure. Safe to call with a
    // non-existent directory (logs one line, loads nothing, does not
    // throw) - a Player-shaped host with no plugins/ folder at all must
    // never crash. Safe to call more than once (idempotent per call -
    // each call scans fresh and APPENDS to the existing registry; calling
    // it twice with the same folder will load every .dll twice as two
    // separate module instances - callers must only ever call this once
    // per process, exactly as PHASE0's Locked Design Decision #9 requires;
    // this method itself does not defend against a caller violating that,
    // matching PHASE0's own "load once, at host-construction time" rule
    // being an explicitly documented CALLER discipline, not a class
    // invariant this class enforces itself).
    void LoadPlugins(const std::filesystem::path& pluginsDirectory);

    // Every module that successfully passed the fingerprint check and
    // returned a non-null IPluginModule* from GTE_CreatePluginModule().
    const std::vector<IPluginModule*>& AllLoadedModules() const noexcept { return m_modules; }

    std::size_t LoadedModuleCount() const noexcept { return m_modules.size(); }

private:
    struct LoadedPlugin {
        void* moduleHandle = nullptr; // HMODULE, stored as void* so this header never needs <windows.h>
        IPluginModule* module = nullptr;
        // The plugin's own exported GTE_DestroyPluginModule, resolved once
        // at load time, called exactly once, from ~PluginHost(), in
        // REVERSE load order (last-loaded, first-destroyed) - mirrors
        // ordinary RAII/stack-unwind destruction order convention.
        void (*destroyFn)(IPluginModule*) = nullptr;
    };

    // Real implementation lives entirely in PluginHost.cpp - keeps
    // <windows.h> and every plugins/gte_plugin_abi/PluginExports.h
    // function-pointer typedef out of this header.
    void TryLoadOnePlugin(const std::filesystem::path& dllPath);

    std::vector<LoadedPlugin> m_loadedPlugins;
    std::vector<IPluginModule*> m_modules; // parallel, public-facing view - m_loadedPlugins[i].module == m_modules[i]
};

} // namespace gte
```

### 3.2 — `src/Core/Plugins/PluginHost.cpp`

Real body: `#include <windows.h>` (only here), `#include "../Logging.h"` for
`GTE_LOG_*`, `#include "../../../plugins/gte_plugin_abi/PluginExports.h"`,
`#include "../../../plugins/gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"`.

`LoadPlugins()` body: `std::filesystem::exists(pluginsDirectory)` guard (log
`GTE_LOG_INFO` "no plugins directory found at <path>, skipping" and return if
missing — this is a normal, expected, non-error condition for e.g. this
phase's own compile-check runs before any demo plugin `.dll` is actually
staged there yet); otherwise
`for (const auto& entry : std::filesystem::directory_iterator(pluginsDirectory))`,
skip non-`.dll` extensions, call `TryLoadOnePlugin(entry.path())` for each.

`TryLoadOnePlugin()` body, following the source design doc's Section 4.1
procedure exactly, with a `GTE_LOG_*` line at every branch (matching this
codebase's own "every skip is loud, in the log, never silent" convention
already established for e.g. `NetworkServer`'s bind-failure handling):

1. `HMODULE handle = LoadLibraryW(dllPath.wstring().c_str());` — null →
   `GTE_LOG_WARNING("PluginHost", "Failed to LoadLibraryW: <path>")`, return.
2. Resolve `GTE_GetPluginAbiFingerprint`/`GTE_CreatePluginModule`/
   `GTE_DestroyPluginModule` via `GetProcAddress()` by the exact
   `kGteGetPluginAbiFingerprintExportName`/etc. string constants
   (`plugins/gte_plugin_abi/PluginExports.h`). Any missing →
   `GTE_LOG_WARNING(..., "<path> is missing export <name>, not a valid plugin, skipping")`,
   `FreeLibrary(handle)`, return.
3. Call the resolved fingerprint function. Compare (`operator==`) against
   `gte::MakeThisBuildsFingerprint()` (this exe's own, real, generated
   fingerprint). Mismatch → log EVERY differing field by name (mirrors the
   source design doc's own Section 4.1 example: `"built with MSVC 19.38,
   host is MSVC 19.42"` — this project's own equivalent: compiler id/
   version/build config/pointer size/shared-runtime-linkage, whichever
   fields actually differ), `FreeLibrary(handle)`, return. **This is the
   single most important check in this whole class — never skip it, never
   soften it into a warning-only "load anyway."**
4. Call `GTE_CreatePluginModule()`. `nullptr` is a legal "I decline to load"
   signal (source design doc Section 3.1) — log
   `GTE_LOG_INFO(..., "<path>'s GTE_CreatePluginModule() returned nullptr, plugin declined to load")`,
   `FreeLibrary(handle)`, return; NOT a warning (a plugin choosing not to
   load is not an error).
5. Success: call `module->GetModuleInfo(info)`, log
   `GTE_LOG_INFO("PluginHost", "Loaded plugin '<info.name>' v<info.version> from <path>")`,
   push a `LoadedPlugin{handle, module, destroyFn}` onto `m_loadedPlugins`,
   push `module` onto `m_modules`.

`~PluginHost()`: iterate `m_loadedPlugins` in REVERSE, call
`plugin.destroyFn(plugin.module)` then `FreeLibrary(static_cast<HMODULE>(plugin.moduleHandle))`
for each — this order matters (destroy the C++ object living inside the
`.dll`'s own code segment BEFORE unmapping that code segment, never the
reverse).

### 3.3 — Wire into `Core`

`Core.h`: add
`#include "Plugins/PluginHost.h"` (file-scope, same placement discipline as
`FrameDebuggerCaptureRecorder.h`'s own doc comment already explains — NOT
from inside the `namespace gte { ... }` block), a new public method
`void LoadPlugins(const std::filesystem::path& pluginsDirectory);`, a new
public accessor `const PluginHost& GetPluginHost() const noexcept { return m_pluginHost; }`,
and a new private member `PluginHost m_pluginHost;` (declaration-order
placement: anywhere safe — it has no constructor dependency on any other
`Core` member, so append it near the end of the private member list,
immediately before `m_gameTargetThisFrame`/`m_sceneTargetThisFrame`, which
similarly have no cross-member dependency).

`Core.cpp`: `#include "Plugins/PluginHost.h"` already pulled in via `Core.h`;
add:
```cpp
void Core::LoadPlugins(const std::filesystem::path& pluginsDirectory)
{
    m_pluginHost.LoadPlugins(pluginsDirectory);
}
```
Gate the REAL call site (not this pass-through method itself, which always
compiles) with `#if GTE_ENABLE_PLUGINS` at the CALLER (`EditorHost.cpp`,
Step 3.4 below) — mirrors `GTE_ENABLE_NETWORK`'s own existing precedent of
gating the call site (`NetworkServer::Start()`), not the class itself.

`GTE_ENABLE_PLUGINS` must be a PUBLIC compile definition on the `gte_core`
target (mirrors `GTE_ENABLE_PROJECT_PANEL`'s own existing
`target_compile_definitions(gte_core PUBLIC ...)` precedent, root
`CMakeLists.txt`) so it is visible from `EditorHost.cpp` (`gte_editor`) too.

Add `src/Core/Plugins/PluginHost.h`/`.cpp` to `gte_core`'s own
`target_sources()` list in root `CMakeLists.txt`, immediately after the
existing `src/Core/FrameDebuggerCaptureRecorder.h` entry (a natural,
consistent insertion point — same `src/Core/` subtree). `target_link_libraries(gte_core PUBLIC gte_plugin_abi)`.

### 3.4 — Wire into `EditorHost`

`EditorHost.cpp`'s constructor, immediately AFTER
`m_core.SetPresentImGuiRecorder(...)` and BEFORE the
`static EditorSceneIOCapability s_editorSceneIOCapability;` block (a natural
point — every other per-subsystem wiring call already lives in this same
stretch of the constructor body):

```cpp
#if GTE_ENABLE_PLUGINS
    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision #9 - loaded
    // exactly once, here, at EditorHost construction time. The plugins/
    // folder lives NEXT TO the built executable (SDL_GetBasePath(), the
    // same base-path resolution src/Editor/ProjectRootPath.h already uses
    // for the Project panel's own root folder - see that file for the
    // precedent this mirrors) - never relative to the current working
    // directory, which is not guaranteed to be the exe's own folder.
    m_core.LoadPlugins(std::filesystem::path(ProjectRootPath::ExecutableDirectory()) / "plugins");
#endif
```

Re-read `src/Editor/ProjectRootPath.h/.cpp` before writing this call — if it
does not already expose a plain "executable's own directory" accessor
(distinct from whatever "Project root" folder it resolves for the Project
panel), add one small, additive function there (e.g.
`ProjectRootPath::ExecutableDirectory()`) rather than duplicating
`SDL_GetBasePath()` call logic a second time in `EditorHost.cpp` — confirm
the exact existing shape via `read_file` before deciding whether to add or
reuse; if genuinely ambiguous, use `ask_questions`.

`#include <filesystem>` and the relevant `ProjectRootPath.h` header at the
top of `EditorHost.cpp` if not already present (confirm via `search_in_dir`
first).

### 3.5 — Demo plugin: `plugins/demo_hello_world/`

`plugins/demo_hello_world/CMakeLists.txt`:
```cmake
add_library(demo_hello_world SHARED
    HelloWorldPlugin.cpp
)
target_link_libraries(demo_hello_world PRIVATE gte_plugin_abi)
set_target_properties(demo_hello_world PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${GTE_PLUGIN_RUNTIME_OUTPUT_DIR}"
)
gte_apply_plugin_shared_crt_linkage(demo_hello_world)
```
`GTE_PLUGIN_RUNTIME_OUTPUT_DIR` is a plain CMake variable, NOT a
hardcoded `$<TARGET_FILE_DIR:GreatTamanaEditor>` generator expression —
`GreatTamanaEditor` does not exist as a target at all in a
`GTE_CORE_STANDALONE_PROBE_ONLY=ON` configuration (see the guard around its
own `add_executable()` call further down root `CMakeLists.txt`), and BOTH
this phase's own new standalone probe (Step 3.6) AND PHASE5's isolation
probe configure with `GTE_CORE_STANDALONE_PROBE_ONLY=ON` specifically so
they need `demo_hello_world.dll` to still build successfully there. Define
it once, in root `CMakeLists.txt`, immediately before the `add_subdirectory`
calls below, branching on the same guard:
```cmake
if(GTE_CORE_STANDALONE_PROBE_ONLY)
    set(GTE_PLUGIN_RUNTIME_OUTPUT_DIR "${CMAKE_BINARY_DIR}/plugins")
else()
    set(GTE_PLUGIN_RUNTIME_OUTPUT_DIR "$<TARGET_FILE_DIR:GreatTamanaEditor>/plugins")
endif()
```
(Confirm, via `browse_dir`, that a plain `add_executable()` target with no
explicit `RUNTIME_OUTPUT_DIRECTORY` property really does land directly in
`${CMAKE_BINARY_DIR}` for this repo's actual single-config Ninja generator —
already true today for `GreatTamanaEditor.exe` itself, confirmed during this
campaign's own investigation — before trusting the `GTE_CORE_STANDALONE_PROBE_ONLY`
branch above; if it does not for any real, discovered reason, use whatever
the real, current output directory of `gte_core_player_link_probe`/
`gte_plugin_abi_handshake_probe` actually is instead, and document the
correction.)

Root `CMakeLists.txt`: `if(GTE_ENABLE_PLUGINS)` (defining
`GTE_PLUGIN_RUNTIME_OUTPUT_DIR` as shown above, then)
`add_subdirectory(plugins/gte_plugin_abi)`
`add_subdirectory(plugins/demo_hello_world)` `endif()` — placed
IMMEDIATELY after `gte_core`'s own `add_library()` block's closing
parenthesis (so it is reached unconditionally, in BOTH a normal configure
AND a `GTE_CORE_STANDALONE_PROBE_ONLY=ON` one — confirm via `read_file` that
this insertion point genuinely sits OUTSIDE/BEFORE the `if(NOT
GTE_CORE_STANDALONE_PROBE_ONLY)` guard that wraps the `gte_editor` library
block immediately after it, since `demo_hello_world` must build even when
`gte_editor` itself is skipped) and so `demo_hello_world` can depend on
nothing from `gte_core` at all, per Locked Design Decision #2 — it links
ONLY `gte_plugin_abi`.

`plugins/demo_hello_world/HelloWorldPlugin.cpp`:
```cpp
#include "../gte_plugin_abi/IPluginModule.h"
#include "../gte_plugin_abi/GtePluginModuleInfo.h"
#include "../gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"

#include <cstring>

namespace gte {

namespace {
class HelloWorldPluginModule final : public IPluginModule {
public:
    void* QueryCapability(const char*) override { return nullptr; } // implements zero capabilities - Milestone 0 proves ONLY the handshake itself
    void GetModuleInfo(GtePluginModuleInfo& outInfo) const override
    {
        std::strncpy(outInfo.name, "HelloWorldPlugin", sizeof(outInfo.name) - 1);
        std::strncpy(outInfo.version, "1.0.0", sizeof(outInfo.version) - 1);
        std::strncpy(outInfo.description, "Milestone 0 handshake proof - implements zero capabilities.", sizeof(outInfo.description) - 1);
    }
};
} // namespace

} // namespace gte

extern "C" {

__declspec(dllexport) gte::GtePluginAbiFingerprint GTE_GetPluginAbiFingerprint()
{
    return gte::MakeThisBuildsFingerprint();
}

__declspec(dllexport) gte::IPluginModule* GTE_CreatePluginModule()
{
    return new gte::HelloWorldPluginModule();
}

__declspec(dllexport) void GTE_DestroyPluginModule(gte::IPluginModule* module)
{
    delete module;
}

} // extern "C"
```

(MinGW's `__declspec(dllexport)` works identically to MSVC's for this
purpose — confirm the produced `.dll`'s export table actually contains all
three names via `run_shell("objdump -p <path to demo_hello_world.dll> | findstr Ordinal")`-
style check, or the simpler `dumpbin /exports` if available, as this phase's
own mandatory verification step, per PHASE0's Universal Rule 11.)

### 3.6 — Standalone handshake probe: `tools/ci/gte_plugin_abi_handshake_probe/`

Mirrors `tools/ci/gte_core_player_link_probe`'s own real, current shape
exactly, not just `tools/ci/gte_core_standalone_probe`'s in the abstract —
that probe's own executable target (`gte_core_player_link_probe`) is
declared directly inside the REAL root `CMakeLists.txt`, gated by
`if(GTE_CORE_STANDALONE_PROBE_ONLY)` (confirm the real, current line via
`search_in_dir` for `add_executable(gte_core_player_link_probe`; at the
time of this campaign's own investigation it was ~line 965), with only its
OUTER wrapper project (`tools/ci/gte_plugin_abi_handshake_probe/CMakeLists.txt`)
living outside the real root file, doing the nested `cmake -S <repo root>
-B <its own private build dir>` invocation. This new probe needs the exact
same treatment — do not skip adding its own `add_executable()` to the real
root `CMakeLists.txt`, mirroring `gte_core_player_link_probe`'s own
placement exactly:
```cmake
if(GTE_CORE_STANDALONE_PROBE_ONLY)
    add_executable(gte_core_player_link_probe tools/ci/gte_core_player_link_probe/main.cpp)
    target_link_libraries(gte_core_player_link_probe PRIVATE gte_core)

    add_executable(gte_plugin_abi_handshake_probe tools/ci/gte_plugin_abi_handshake_probe/main.cpp)
    target_link_libraries(gte_plugin_abi_handshake_probe PRIVATE gte_plugin_abi)
    gte_apply_plugin_shared_crt_linkage(gte_plugin_abi_handshake_probe)
    # This probe LoadLibraryW()s demo_hello_world.dll at runtime rather than
    # linking it - CMake has no way to know that dependency on its own, so
    # it must be spelled out explicitly or a `cmake --build` of only this
    # probe's own target will never actually produce the .dll it needs.
    add_dependencies(gte_plugin_abi_handshake_probe demo_hello_world)
endif()
```
This new executable target only exists in a `GTE_CORE_STANDALONE_PROBE_ONLY=ON`
configuration, exactly like `gte_core_player_link_probe` — it needs zero
`gte_core`/`gte_editor` at all, only `gte_plugin_abi` + a real,
already-built `demo_hello_world.dll` to load. Its own nested-cmake-invocation
wrapper (`tools/ci/gte_plugin_abi_handshake_probe/CMakeLists.txt`, mirroring
`tools/ci/gte_core_player_link_probe/CMakeLists.txt`'s exact real shape —
`read_file` it before writing this one) must pass **all three** flags on the
inner configure, not just two:
```cmake
"-DGTE_CORE_STANDALONE_PROBE_ONLY=ON"
"-DGTE_ENABLE_PLUGINS=ON"
"-DGTE_BUILD_TESTS=OFF"
```
and its own `--build` step must build this probe's own target (building
`demo_hello_world` transitively via the `add_dependencies()` above, so a
single `--target gte_plugin_abi_handshake_probe` build step is sufficient —
confirm this transitive dependency actually triggers a build of
`demo_hello_world.dll` during this phase's own verification, and add an
explicit second `--target demo_hello_world` build step instead if it does
not, for any real, discovered reason).

New `main.cpp`, `#include`ing ONLY `plugins/gte_plugin_abi/`'s own headers
(zero `gte_core` dependency, proving Milestone 0's own "prove the ABI
boundary itself works before any real capability exists" claim in complete
isolation) — `LoadLibraryW()`s `demo_hello_world.dll` directly (resolved
relative to its own known build location, or accept a command-line argument
for the `.dll` path — simplest: hardcode the relative path from this
probe's own known build tree location, documented plainly), checks the
fingerprint via `MakeThisBuildsFingerprint()` + `operator==`, calls
`GetModuleInfo()`, prints it via plain `std::printf` (Universal Rule 5's
own explicit exception), calls the destroy function, `FreeLibrary()`s,
returns `0` on success or a non-zero code with a clear
`std::fprintf(stderr, ...)` message on ANY failure at ANY step (missing
export, fingerprint mismatch, null module).

---

## Step 4: Verification (this phase's own mandatory checkpoint)

1. Incremental compile: `gte_plugin_abi` → `demo_hello_world` → `gte_core` →
   `gte_editor` → `GreatTamanaEditor`.
2. Confirm `demo_hello_world.dll` genuinely exists in
   `<build-dir>/plugins/` after the build (`browse_dir`).
3. `run_app_background` the built `GreatTamanaEditor.exe`; `gte_send_request(
   "/get_logs?limit=20")`; confirm a real `"Loaded plugin 'HelloWorldPlugin'
   v1.0.0 ..."` line appears. `stop_app_background`.
4. Configure + build `tools/ci/gte_plugin_abi_handshake_probe` fresh
   (nested-cmake pattern); confirm it prints the expected module info and
   exits `0`.
5. Deliberately break the fingerprint ONCE, temporarily, as a real negative
   test (e.g. bump `kGtePluginAbiContractGeneration` in a scratch copy, or
   simpler: temporarily rename one of `demo_hello_world`'s three exports)
   and confirm `PluginHost`/the probe both log a clear mismatch/missing-
   export reason and continue running (never crash) — then revert the
   deliberate breakage before committing. Document this negative-test result
   in the completion report — proving the failure PATH works is exactly as
   important as proving the success path, and is explicitly worth the extra
   few minutes per PHASE0's "Ignore any verification steps that do not
   involve writing or editing code" instruction not applying here (this
   negative test IS a real code-level check, not a passive observation).

---

## Universal Rules

Follow `PHASE0_MASTER_STRATEGY.md`'s Universal Rules exactly.

## Non-Goals for this phase specifically

- No real capability (`IRenderFeatureModule_v1`/`IEditorPanelModule_v1`) yet
  — `demo_hello_world` implements zero capabilities on purpose.
- No `gte_editor`-side plugin consumption yet (PHASE4).
