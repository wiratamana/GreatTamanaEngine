# Project Assembly Link Probe

## What this is

A tiny, **manually-invocable**, genuinely standalone local CMake project (NOT
a nested-cmake wrapper around the real root `CMakeLists.txt`, unlike every
other `tools/ci/*` probe - see this project's own `CMakeLists.txt` for the
full "why standalone, not nested" reasoning). It proves the mechanism the
`editor-core-separation-11` campaign's ("Project Assembly" system) whole
design depends on:

- `<ProjectName>_Game.dll`/`<ProjectName>_Editor.dll` link **ONLY** against
  `GreatTamanaEditor.exe` itself (never `gte_core`/`gte_editor` directly) -
  `set_target_properties(GreatTamanaEditor PROPERTIES ENABLE_EXPORTS ON)`
  makes the exe export symbols so a `.dll`'s unresolved references resolve,
  at OS-loader time, straight back into the ALREADY-RUNNING `.exe` image,
  instead of duplicating every `gte_core`/`gte_editor` global (Logger,
  `EditorPanelRegistry`, `GImGui`, ...) a second time inside the `.dll`.
- On Windows/MinGW, `ENABLE_EXPORTS` alone only auto-exports symbols
  explicitly marked `__declspec(dllexport)` - `WINDOWS_EXPORT_ALL_SYMBOLS`
  is (usually) needed too, for ordinary, un-annotated engine code that was
  never written with DLL export in mind.
- A Project Assembly `_Editor.dll` needs ImGui's real headers to call
  `ImGui::*` directly, WITHOUT re-linking the compiled `imgui` library a
  second time (which would create a second, independent `GImGui` context
  pointer in the process).

`task_manager/editor-core-separation-11/PHASE2_ENABLE_EXPORTS_FEASIBILITY_PROBE.md`
is this probe's own phase file - read it for the full task list this project
implements.

## How it works

- `main.cpp` stands in for `GreatTamanaEditor.exe`. It exports:
  - `g_probeCounter` (a plain `int` global, explicitly marked
    `__declspec(dllexport)` - mirrors a real `gte_core` singleton).
  - `HostPlainAdd(int, int)` (a plain, DELIBERATELY **non**-annotated free
    function - mirrors a typical, un-exported `gte_core` method - this is
    the phase file's own Step 4 "re-test with a real, non-trivial internal
    symbol" escape hatch).
  - `GetHostImGuiContext()`, returning `ImGui::GetCurrentContext()` after
    `main()` calls `ImGui::CreateContext()`.
  - It then `LoadLibraryW`s `probe_module.dll` and calls its 3 exports.
- `probe_module.cpp` stands in for a Project Assembly's own `_Game.dll`/
  `_Editor.dll`. It has **unresolved** references to `g_probeCounter` and
  `HostPlainAdd()`, and calls `ImGui::GetCurrentContext()` itself (through
  ImGui headers made available WITHOUT re-linking imgui's compiled code -
  see `CMakeLists.txt`'s own comment for the exact recipe). It exports
  `ProbeEntry()` (increments `g_probeCounter` through the host), `ProbeAddViaHost()`
  (calls `HostPlainAdd()` through the host) and `GetModuleImGuiContext()`.
- `main()`'s own exit code is the PASS/FAIL signal (0 == every check passed):
  `g_probeCounter == 43` after `ProbeEntry()`, `ProbeAddViaHost(2, 3) == 5`,
  and `GetHostImGuiContext() == GetModuleImGuiContext()` (one single,
  physical ImGui context in the process, not two).

## Exact commands to run this by hand

From the repository root, the permanent, default (Step 4(a),
`WINDOWS_EXPORT_ALL_SYMBOLS ON`) configuration:

```
cmake -S tools/ci/gte_project_assembly_link_probe -B build-project-assembly-link-probe -G Ninja
cmake --build build-project-assembly-link-probe
cd build-project-assembly-link-probe && gte_project_assembly_probe_host.exe
```

(`-G Ninja` is required on a machine whose default CMake generator isn't
already Ninja - mirrors every other `tools/ci/*` probe's own README note.)

First configure requires internet access - unlike the main repo's `build/`
tree (which already has `third_party/imgui`, `include/SDL3`, `include/vulkan`
staged), THIS project's `CMAKE_SOURCE_DIR` is its own source directory, so
`fetch_sdl3()`/`fetch_vulkan()`/`fetch_imgui()` stage a fresh, independent
copy under `tools/ci/gte_project_assembly_link_probe/{include,lib,third_party,SDL3.dll}`
the first time it is configured. This is expected and harmless, not a bug -
see `CMakeLists.txt`'s own header comment.

To reproduce Step 4(b) (`WINDOWS_EXPORT_ALL_SYMBOLS OFF` - expected to FAIL
to link `probe_module.dll`), use a SEPARATE, throwaway binary directory (do
NOT reuse `build-project-assembly-link-probe` - CMake refuses a second
configure of the same binary dir with different top-level option values
mid-stream is fine here since it's a plain `option()`, but keeping the
experiment fully separate avoids ever leaving the permanent tree in the
non-default state by accident):

```
cmake -S tools/ci/gte_project_assembly_link_probe -B build-project-assembly-link-probe-scratch-off -G Ninja -DGTE_PROBE_WINDOWS_EXPORT_ALL_SYMBOLS=OFF
cmake --build build-project-assembly-link-probe-scratch-off
```

Delete `build-project-assembly-link-probe-scratch-off/` afterward - it is a
throwaway, not part of this probe's permanent fixture (only
`build-project-assembly-link-probe/` is `.gitignore`d permanently).

## What a successful run proves

- `ENABLE_EXPORTS` + linking a `.dll` against the resulting `.exe`'s own
  import library genuinely keeps exactly ONE physical copy of a
  `.exe`-side global in the process - the real, load-bearing mechanism
  behind LDD2 (`PHASE0_MASTER_STRATEGY.md`).
- The exact conditions under which `WINDOWS_EXPORT_ALL_SYMBOLS` is/isn't
  needed (see `PHASE2_COMPLETION_REPORT.md` for the concrete finding).
- The exact CMake recipe for "get imgui's headers without re-linking its
  compiled code a second time" (`target_include_directories(... PRIVATE
  $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>)`, linking the HOST
  target alone, never `imgui` itself) - reused verbatim by PHASE7.

## What this probe deliberately does NOT do

- It does not touch the real root `CMakeLists.txt`/`GreatTamanaEditor` target
  at all - that real change is PHASE3's job, once this probe has proven the
  mechanism sound in isolation.
- It is not wired into any GitHub Actions workflow or other real CI system -
  none exists in this repository. Run it by hand whenever you want to
  re-confirm this mechanism still holds.
- It never appears as a user-facing option inside the main build.
