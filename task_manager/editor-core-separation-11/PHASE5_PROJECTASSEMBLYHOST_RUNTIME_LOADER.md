# PHASE5 of 8 — `ProjectAssemblyHost` RUNTIME LOADER

Read `PHASE0_MASTER_STRATEGY.md` first.
**Depends on:** PHASE3 (needs a real, buildable `_Game.dll`/`_Editor.dll` to
load — reuse `ProjectAssemblyProbe`).
**Blocks:** PHASE6, 7, 8 — none of them mean anything until a Project
Assembly `.dll` can actually be loaded and receive a live `gte::Core&`.

End state of this phase: at `GreatTamanaEditor.exe` startup,
`ProjectAssemblyProbe_Game.dll` (and `_Editor.dll`) are found, loaded, and
their `GTE_RegisterProject` export is called exactly once, with a real, live
`gte::Core&` (and, for the Editor one, a real `gte::EditorHost&`) — proven by
a temporary diagnostic log line, visible via `GET /get_logs`.

## STEP 1 — new files: `src/Core/Plugins/ProjectAssemblyHost.h`/`.cpp`

Modeled directly on the existing `src/Core/Plugins/PluginHost.h`/`.cpp`
(confirmed, current, ~85-line header) — every design choice below mirrors it
deliberately. Sibling to, but SEPARATE from, `PluginHost` — never edit
`PluginHost.h`/`.cpp` itself (LDD1).

```cpp
// src/Core/Plugins/ProjectAssemblyHost.h
//
// editor-core-separation-11 campaign (Project Assembly system), PHASE5.
// Scans GTE_PROJECT_ASSEMBLY_OUTPUT_DIR (PHASE3) for *_Game.dll/*_Editor.dll,
// LoadLibraryW()s each, resolves the ONE fixed export (GTE_RegisterProject),
// and calls it once per .dll, passing the real, live gte::Core& (and, for
// an _Editor assembly, the real, live gte::EditorHost&).
//
// DELIBERATE DIFFERENCES from PluginHost (read that file's own header
// comment first for the shape this mirrors):
//   1. No ABI fingerprint struct is generated or checked here at all - this
//      is unnecessary by construction (same-toolchain/same-build guaranteed,
//      since a Project Assembly .dll links GreatTamanaEditor directly -
//      PHASE2/3).
//   2. TWO different function-pointer signatures are resolved, depending on
//      whether the .dll's own filename ends in "_Game.dll" or
//      "_Editor.dll" - see LoadProjectAssemblies()'s own doc comment below
//      for exactly how this is distinguished, and why a single, uniform
//      resolution (like PluginHost's single IPluginModule* shape) does not
//      work here.
//   3. This class does NOT keep a live IPluginModule*-equivalent handle per
//      loaded assembly after registration - GTE_RegisterProject is a
//      one-shot call, there is no ongoing QueryCapability()-style
//      interaction afterward. This class's only ongoing job after
//      LoadProjectAssemblies() returns is keeping every loaded HMODULE
//      alive (never FreeLibrary()'d) for the rest of the process lifetime
//      (LDD4 - no hot reload, ever).
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace gte {

class Core;
class EditorHost;

class ProjectAssemblyHost {
public:
    ProjectAssemblyHost() = default;
    ~ProjectAssemblyHost();

    ProjectAssemblyHost(const ProjectAssemblyHost&) = delete;
    ProjectAssemblyHost& operator=(const ProjectAssemblyHost&) = delete;
    ProjectAssemblyHost(ProjectAssemblyHost&&) = delete;
    ProjectAssemblyHost& operator=(ProjectAssemblyHost&&) = delete;

    // Enumerates every "*_Game.dll"/"*_Editor.dll" directly inside
    // `outputDirectory` (no recursion - mirrors PluginHost::LoadPlugins()'s
    // own "directly inside, no subfolder recursion" rule), loads each, and
    // calls its GTE_RegisterProject export exactly once. Safe to call with
    // a non-existent directory (a fresh checkout with no Projects/ folder
    // yet must never crash - mirrors PluginHost's own identical guarantee).
    // `core` must outlive this call by the engine's entire remaining
    // process lifetime. `editorHost` is `nullptr` for a Player-shaped host
    // that never constructs one (mirrors IEditorPanelModule_v1's own "never
    // queried by a Player host" precedent) - a "*_Editor.dll" found while
    // `editorHost == nullptr` is skipped with a loud, logged warning, never
    // crashed on.
    void LoadProjectAssemblies(const std::filesystem::path& outputDirectory,
        Core& core, EditorHost* editorHost);

    std::size_t LoadedAssemblyCount() const noexcept { return m_loadedAssemblies.size(); }

private:
    struct LoadedAssembly {
        void* moduleHandle = nullptr; // HMODULE, stored as void* - mirrors PluginHost's identical convention.
        std::string dllFileName;
    };

    void TryLoadOneAssembly(const std::filesystem::path& dllPath, Core& core, EditorHost* editorHost);

    std::vector<LoadedAssembly> m_loadedAssemblies;
};

} // namespace gte
```

`.cpp` body — mirror `PluginHost.cpp`'s own `LoadLibraryW`/`GetProcAddress`/
logging idiom exactly (`GTE_LOG_INFO`/`GTE_LOG_WARNING`, `src/Core/Logging.h`
line 162 — confirmed two-argument signature, `GTE_LOG_INFO(category,
message)`):

```cpp
void ProjectAssemblyHost::TryLoadOneAssembly(
    const std::filesystem::path& dllPath, Core& core, EditorHost* editorHost)
{
    const std::string fileName = dllPath.filename().string();
    const bool isEditorAssembly = fileName.ends_with("_Editor.dll");
    const bool isGameAssembly = fileName.ends_with("_Game.dll");
    if (!isEditorAssembly && !isGameAssembly) {
        return; // Not a Project Assembly output - ignore silently.
    }

    HMODULE module = LoadLibraryW(dllPath.c_str());
    if (module == nullptr) {
        // GTE_LOG_WARNING("ProjectAssembly", ...) - include GetLastError(),
        // continue to next file, never crash.
        return;
    }

    if (isEditorAssembly) {
        if (editorHost == nullptr) {
            // GTE_LOG_WARNING: "<fileName> is an _Editor assembly but no
            // EditorHost exists in this process - skipped."
            FreeLibrary(module); // Safe here - its export was never called.
            return;
        }
        using EditorEntryFn = void (*)(Core&, EditorHost&);
        auto entry = reinterpret_cast<EditorEntryFn>(GetProcAddress(module, "GTE_RegisterProject"));
        if (entry == nullptr) {
            // GTE_LOG_WARNING: missing export, skip, do not crash.
            FreeLibrary(module);
            return;
        }
        entry(core, *editorHost);
    } else { // isGameAssembly
        using GameEntryFn = void (*)(Core&);
        auto entry = reinterpret_cast<GameEntryFn>(GetProcAddress(module, "GTE_RegisterProject"));
        if (entry == nullptr) {
            FreeLibrary(module);
            return;
        }
        entry(core);
    }

    m_loadedAssemblies.push_back(LoadedAssembly{ module, fileName });
}
```

**Flag this concretely, do not silently resolve it without checking:** the
two `reinterpret_cast` targets above (`void(*)(Core&)` vs.
`void(*)(Core&, EditorHost&)`) both resolve the SAME exported symbol name,
`GTE_RegisterProject` — `ProjectAssemblyHost` MUST correctly determine
`_Game` vs. `_Editor` from the FILENAME suffix BEFORE resolving/calling the
export, never from the signature itself (no runtime introspection of a
`.dll` export's parameter count exists here). This is why PHASE3's
`gte_add_project()` unconditionally names its targets exactly
`${NAME}_Game`/`${NAME}_Editor` — if a future edit ever loosens that naming
convention, THIS class breaks silently (calling a 2-argument function
through a 1-argument pointer is undefined behavior with no guaranteed crash
at the call site). Document this coupling loudly in both files' own
comments.

## STEP 2 — give `Core` a thin pass-through, mirroring `Core::LoadPlugins()`
exactly (`src/Core/Core.h`/`.cpp`)

`Core` already owns `m_pluginHost` (a `PluginHost`, confirmed `Core.h` line
561, `private:`) and exposes a thin `LoadPlugins()` pass-through (confirmed
`Core.h` line 295, `public:`; body at `Core.cpp` line 293, forwarding to
`m_pluginHost.LoadPlugins(pluginsDirectory)`). Mirror this exactly:

```cpp
// Core.h, private members section (near m_pluginHost, line 561):
ProjectAssemblyHost m_projectAssemblyHost;

// Core.h, public methods section (near LoadPlugins(), line 295):
void LoadProjectAssemblies(const std::filesystem::path& outputDirectory, EditorHost* editorHost);
```

`Core.h` needs a forward declaration of `EditorHost` — confirmed, current
`Core.h` has NO such forward declaration today (only comment mentions of the
name); mirror the existing `class IEditorLayer;` forward-declaration
precedent (confirmed, `Core.h` line 64) — add `class EditorHost;` right
alongside it. `Core.h` also needs a full `#include` of
`Plugins/ProjectAssemblyHost.h` (mirroring its existing `#include
"Plugins/PluginHost.h"` at line 45 — a concrete, `gte_core`-owned mechanism
class, included by name).

`Core.cpp`:

```cpp
void Core::LoadProjectAssemblies(const std::filesystem::path& outputDirectory, EditorHost* editorHost)
{
    m_projectAssemblyHost.LoadProjectAssemblies(outputDirectory, *this, editorHost);
}
```

## STEP 3 — wire the real call site in `src/Editor/EditorHost.cpp`

Re-locate the existing `m_core.LoadPlugins(gte::ExecutableDirectory() /
"plugins");` call (confirmed, current, line 250, inside an `#if
GTE_ENABLE_PLUGINS` block that opens at line 231 and closes at line 251 —
re-verify via `search_in_dir` for the exact literal text, do not trust these
line numbers unchanged). Add the new call IMMEDIATELY AFTER that `#endif`,
gated by the NEW, SEPARATE `GTE_ENABLE_PROJECT_ASSEMBLIES` flag (PHASE3,
LDD9) — never `GTE_ENABLE_PLUGINS`, which controls the unrelated
`gte_plugin_abi` system:

```cpp
// editor-core-separation-11 campaign (Project Assembly system), PHASE5 -
// loaded exactly once, here, at EditorHost construction time, mirroring
// Core::LoadPlugins()'s own call immediately above. Deliberately its OWN,
// separate scan (GTE_PROJECT_ASSEMBLY_OUTPUT_DIR's own runtime folder,
// "<exe dir>/project_assemblies/" - NEVER "<exe dir>/plugins/") - the two
// coexisting systems must never be confused for one another. `this`
// (EditorHost) is passed as the live gte::EditorHost& every loaded
// "*_Editor.dll" receives. Gated by GTE_ENABLE_PROJECT_ASSEMBLIES - its OWN
// flag, never GTE_ENABLE_PLUGINS (PHASE0_MASTER_STRATEGY.md, Finding D).
#if GTE_ENABLE_PROJECT_ASSEMBLIES
m_core.LoadProjectAssemblies(gte::ExecutableDirectory() / "project_assemblies", this);
#endif
```

(`GTE_ENABLE_PROJECT_ASSEMBLIES` must be threaded through as a real
preprocessor define, mirroring how `GTE_ENABLE_PLUGINS` already is — confirm
the exact `target_compile_definitions(... PUBLIC GTE_ENABLE_PLUGINS=$<BOOL:...>)`-style
call site for `GTE_ENABLE_PLUGINS` via `search_in_dir` and add an identical
one for the new flag, in root `CMakeLists.txt`, near PHASE3's own `option()`
declaration.)

## STEP 4 — temporary proof, then decide whether to keep it

Edit the test project's `RegisterProbeGame`/`RegisterProbeEditor` functions
(PHASE3's `Assets/HelloGame.cpp`/`Assets/Editor/HelloEditorPanel.cpp`) to log
something visible:

```cpp
void RegisterProbeGame(gte::Core& core) {
    (void)core;
    GTE_LOG_INFO("ProjectAssembly", "ProjectAssemblyProbe_Game.dll: GTE_RegisterProject called with a real, live gte::Core&.");
}
```

Launch `GreatTamanaEditor.exe` (`run_app_background`), confirm this exact
line appears exactly once, at startup, via `GET /get_logs` (never open the
Log panel visually — this campaign's whole verification loop is HTTP-driven).
Do the same for `RegisterProbeEditor`. `stop_app_background` when done.
Either leave this diagnostic line in place permanently (recommended — a
cheap, always-on "is this working at all" smoke signal) or remove it once
PHASE7/8 give the test project something real to do instead — document
which one you picked.

## Definition of Done

- [ ] `src/Core/Plugins/ProjectAssemblyHost.h`/`.cpp` exist, compile, and
      never `#include` anything from `plugins/gte_plugin_abi/`.
- [ ] `Core::LoadProjectAssemblies()` thin pass-through exists, mirroring
      `Core::LoadPlugins()` exactly; `Core.h` gained the `class EditorHost;`
      forward declaration.
- [ ] `EditorHost.cpp` calls it once, at construction time, gated by
      `GTE_ENABLE_PROJECT_ASSEMBLIES` (its own, independent flag), scanning
      a SEPARATE folder from the existing plugin scan.
- [ ] A fresh launch of `GreatTamanaEditor.exe`, checked via `GET /get_logs`,
      shows the diagnostic log line from BOTH `ProjectAssemblyProbe_Game.dll`
      and `ProjectAssemblyProbe_Editor.dll`, exactly once each, at startup.
- [ ] Deleting `Projects/ProjectAssemblyProbe/` entirely and rebuilding (so
      neither `.dll` exists) still launches `GreatTamanaEditor.exe` cleanly
      (`GET /get_swapchain` still returns a real image), with no crash — test
      this explicitly, do not assume it from `PluginHost`'s own analogous
      guarantee without testing THIS class specifically.

## What this phase does NOT do

- Does NOT implement the "Compile" button (PHASE6) — a changed Project
  Assembly source file today still requires a manual `cmake --build` and a
  manual relaunch of `GreatTamanaEditor.exe` (LDD4 — no hot reload).
- Does NOT implement any real Editor panel or render pass registration —
  `RegisterProbeGame`/`RegisterProbeEditor` do nothing but log. PHASE7/8.
