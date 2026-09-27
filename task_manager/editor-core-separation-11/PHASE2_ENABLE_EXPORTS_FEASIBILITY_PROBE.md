# PHASE2 of 8 — `ENABLE_EXPORTS` + LINK-AGAINST-`.exe` FEASIBILITY PROBE

Read `PHASE0_MASTER_STRATEGY.md` first (Finding A especially).
**Depends on:** PHASE1 (the probe is more meaningful, and its later ImGui
step is only valid, under the confirmed-shared-CRT toolchain — already true
per PHASE1, no waiting needed).
**Blocks:** PHASE3 (do not write the real `gte_add_project()` CMake
mechanics until this probe has actually passed).

## The hazard this probe exists to rule out

If `<ProjectName>_Game.dll` naively re-linked the compiled `gte_core`/
`gte_editor` static libraries into itself, the running process would end up
with TWO separate, independent copies of every `gte_core`/`gte_editor`
global (the `Logger` ring buffer, `EditorPanelRegistry`, etc.) — one inside
`GreatTamanaEditor.exe`, one freshly duplicated inside the `.dll`. The fix
(LDD2, PHASE0): `GreatTamanaEditor` gets
`set_target_properties(GreatTamanaEditor PROPERTIES ENABLE_EXPORTS ON)`, and
every Project Assembly `.dll` links `GreatTamanaEditor` (never `gte_core`/
`gte_editor` directly) — so its unresolved symbols resolve, at OS-loader
time, straight back into the ALREADY-RUNNING `.exe` image.

This is a real, standard Windows PE technique — but it is NEW to this
codebase (confirmed: `search_in_dir` for `"ENABLE_EXPORTS"` across the whole
repo, all file types, returns zero hits before this phase). Build one small,
disposable, standalone probe before touching the real executable target —
exactly this repo's own established habit (see `tools/ci/gte_plugin_isolation_probe/`
for the house style to copy).

## A second risk this probe must ALSO test, in the same probe (Finding A)

Root `CMakeLists.txt` line 1105 links `imgui`/`imguizmo` `PRIVATE` into
`gte_editor`. `ENABLE_EXPORTS` on `GreatTamanaEditor.exe` alone does not
necessarily export every symbol a statically-linked-in library like `imgui`
contributes — on Windows/PE with a GNU linker, `ENABLE_EXPORTS` by default
only exports symbols explicitly marked `__declspec(dllexport)` UNLESS the
CMake target property `WINDOWS_EXPORT_ALL_SYMBOLS` is ALSO `TRUE` (documented,
generic CMake/GNU-ld behavior — verify it against THIS repo's actual
toolchain in this exact probe, do not take it on faith either way — this
repo has already been burned once by MinGW/GCC deviating from generic
documentation, see `cmake/MingwRuntime.cmake`'s own `-shared-libstdc++`
discovery).

## Step-by-step task list

**Step 1 — Create the probe's own folder**, mirroring
`tools/ci/gte_plugin_isolation_probe/`'s exact house style:

```
tools/ci/gte_project_assembly_link_probe/
    CMakeLists.txt
    main.cpp            <- pretends to be "GreatTamanaEditor.exe"
    probe_module.cpp    <- pretends to be "<Name>_Game.dll"
```

Build it as its OWN, separate, minimal, standalone CMake project — a plain
`project(...)` with its own two targets (Step 3 below), never a nested
invocation of this repo's own root `CMakeLists.txt`. **This is a real,
confirmed constraint, not a style choice: all 4 existing `tools/ci/*` probes
use a nested-invocation wrapper (their own `add_custom_target` runs a
SECOND, independent `cmake -S <repo-root> -B <inner-dir>
-DGTE_CORE_STANDALONE_PROBE_ONLY=ON` configure of the real root
`CMakeLists.txt`, never `add_subdirectory()`), but `GTE_CORE_STANDALONE_PROBE_ONLY=ON`
specifically SKIPS fetching/defining the `imgui`/`imguizmo` CMake targets
entirely** (confirmed: root `CMakeLists.txt`'s `include(FetchImGui)` +
`fetch_imgui()` call sits inside its own
`if(NOT GTE_CORE_STANDALONE_PROBE_ONLY)` guard) — so reusing that same
nested-invocation pattern here would make Step 5's ImGui-context test
impossible to run at all. A fully standalone project avoids this: it must
itself `include(FetchSDL3)`/`fetch_sdl3()`, `include(FetchVulkan)`/
`fetch_vulkan()`, then `include(FetchImGui)`/`fetch_imgui()` (in that order —
`fetch_imgui()`'s own doc comment states it must run after both) by adding
this repo's REAL `cmake/` folder to its own `CMAKE_MODULE_PATH`
(`list(APPEND CMAKE_MODULE_PATH "<repo-root>/cmake")`, mirroring root
`CMakeLists.txt` line 118's own identical mechanism) — this produces a real,
genuine `imgui` STATIC target, built from the exact same fetch module and
exact same source/backend file list the real engine uses, satisfying Finding
A's own representativeness requirement, without needing the main `build/`
tree or any nested configure at all. (`fetch_imgui()` stages its files under
`${CMAKE_SOURCE_DIR}/third_party/imgui` of WHICHEVER project calls it — for
this standalone probe, that is the probe's own source directory, a fresh,
independent copy, not the main repo's already-fetched one; this is expected
and harmless, not a bug.)

**Step 2 — `main.cpp`'s minimal shape** (a stand-in for
`GreatTamanaEditor.exe` — do NOT link real `gte_core`/`gte_editor` into this
probe, that would defeat the whole point of testing the mechanism in
isolation first):

```cpp
// A plain global, standing in for a real gte_core singleton (e.g. Logger/
// EditorPanelRegistry) - the ONE thing this probe exists to prove stays a
// SINGLE physical instance across the .exe/.dll boundary.
extern "C" __declspec(dllexport) int g_probeCounter = 0;

int main() {
    g_probeCounter = 42;
    HMODULE dll = LoadLibraryW(L"probe_module.dll");
    if (!dll) { return 1; } // print GetLastError() in the real implementation
    using EntryFn = void(*)();
    auto entry = reinterpret_cast<EntryFn>(GetProcAddress(dll, "ProbeEntry"));
    if (!entry) { return 1; } // print GetLastError() in the real implementation
    entry(); // probe_module.dll mutates g_probeCounter through its OWN
             // unresolved reference, resolved back into THIS exe
    // PASS: g_probeCounter is now 43 (a private, duplicated copy inside the
    // .dll would leave THIS copy untouched at 42).
    return (g_probeCounter == 43) ? 0 : 1;
}
```

`probe_module.cpp` (a stand-in for `<Name>_Game.dll`):

```cpp
extern int g_probeCounter; // unresolved reference - must resolve back into
                            // main.cpp's own exported global, never a
                            // private copy.
extern "C" __declspec(dllexport) void ProbeEntry() {
    g_probeCounter += 1;
}
```

**Step 3 — the CMake wiring under test:**

```cmake
add_executable(gte_project_assembly_probe_host main.cpp)
set_target_properties(gte_project_assembly_probe_host PROPERTIES
    ENABLE_EXPORTS ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

add_library(gte_project_assembly_probe_module SHARED probe_module.cpp)
target_link_libraries(gte_project_assembly_probe_module
    PRIVATE gte_project_assembly_probe_host)
set_target_properties(gte_project_assembly_probe_module PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:gte_project_assembly_probe_host>"
    PREFIX "")
```

Build BOTH targets, run `gte_project_assembly_probe_host.exe` from its own
output directory (so the relative `LoadLibraryW(L"probe_module.dll")`
resolves), check its process exit code (0 == pass).

**Step 4 — repeat Step 3's exact experiment TWICE:**
  (a) once with `WINDOWS_EXPORT_ALL_SYMBOLS ON` (as shown above);
  (b) once with it OFF (or omitted) — this run SHOULD FAIL to even LINK
      `probe_module.dll` (unresolved external symbol `g_probeCounter`),
      proving the property is genuinely load-bearing, not superstition. If
      (b) links successfully anyway, that is itself an important, surprising
      finding — write it down plainly, do not silently drop the property
      from PHASE3's real recipe just because this one micro-experiment
      happened not to need it; re-test with a real, non-trivial internal
      symbol (a tiny slice of real `gte_core`, e.g. a `Logging.h` log
      function) before trusting a negative result from a single trivial
      `int` global, which the compiler may optimize/inline differently than
      a real class method.

**Step 5 — extend the SAME probe to test Finding A's ImGui-context risk
directly** (safe to do now — PHASE1 already reconfirmed shared-CRT linkage
is active, so this step's result is representative of the real target
configuration from the start, unlike the original design docs' own
"only after the switch" caveat):

  1. `main.cpp` `#include <imgui.h>`, call `ImGui::CreateContext()`, export
     `ImGuiContext* GetHostImGuiContext()` returning
     `ImGui::GetCurrentContext()`.
  2. `probe_module.cpp` links `gte_project_assembly_probe_host` ONLY (never
     `imgui` a second time directly). Its own `#include <imgui.h>` needs a
     resolvable include path — determine and document EXACTLY which
     mechanism makes this work: does linking the host target alone
     transitively expose `imgui`'s include directory, or must the probe
     module ALSO explicitly add
     `target_include_directories(... PRIVATE $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>)`
     WITHOUT ALSO re-linking the `imgui` library target itself? This exact
     distinction — "get the headers without getting a second copy of the
     compiled library" — is the crux of Finding A; resolve it concretely
     here, write the exact working recipe down (PHASE7 reuses it verbatim).
     Export `ImGuiContext* GetModuleImGuiContext()` from its own call site.
  3. PASS condition: `GetHostImGuiContext() == GetModuleImGuiContext()`
     (same pointer value) — exactly one ImGui context in the process. A
     mismatch is a real, serious finding that must be resolved BEFORE
     PHASE7 is attempted (it would mean any ImGui call from inside a Project
     Assembly `_Editor.dll` operates on a dead/wrong context and either does
     nothing or crashes).

## Definition of Done

- [ ] Step 3/4's basic global-sharing probe passes; `WINDOWS_EXPORT_ALL_SYMBOLS`'s
      necessity is confirmed one way or the other, written down either way.
- [ ] Step 5's ImGui-context probe passes AND the exact CMake recipe for
      "get imgui's headers without re-linking its compiled code" is written
      down concretely (PHASE7 reuses it verbatim — do not leave it vague).
- [ ] `PHASE2_COMPLETION_REPORT.md` records the actual toolchain/CMake
      generator this was tested under (Ninja, confirmed via `build/CMakeCache.txt`
      → `CMAKE_GENERATOR:INTERNAL=Ninja`, per PHASE0 §2.5 — re-verify it is
      still Ninja before assuming this).
- [ ] `tools/ci/gte_project_assembly_link_probe/` is left in the repo
      PERMANENTLY as a regression check — mirroring every other probe under
      `tools/ci/` — never deleted after use. Its own build directory
      (configured directly, e.g. `cmake -S tools/ci/gte_project_assembly_link_probe
      -B build-project-assembly-link-probe`, per this phase's own Step 1 —
      never a nested nested-invocation nor a shared folder with the main
      `build/` tree) gets its own new `.gitignore` entry,
      `/build-project-assembly-link-probe/`, with a documented comment
      matching the style of the 4 existing entries already there.

## What this phase does NOT do

- Does NOT touch the root `CMakeLists.txt`'s real
  `add_executable(GreatTamanaEditor ...)` block yet — that real change is
  PHASE3's job, once this probe has proven the mechanism sound in isolation.
- Does NOT write any real Project Assembly folder/CMake scaffolding —
  PHASE3.
