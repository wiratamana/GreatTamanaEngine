# PHASE2 — `ENABLE_EXPORTS` + Link-Against-`.exe` Feasibility Probe — COMPLETION REPORT

**Campaign:** `editor-core-separation-11` — "Project Assembly" System
**Phase:** PHASE2 of 8
**Status:** DONE
**Date:** 2026-09-28

---

## What was built

`tools/ci/gte_project_assembly_link_probe/` — a new, permanent, **genuinely
standalone** CMake project (NOT a nested-invocation wrapper around the real
root `CMakeLists.txt`, per this phase file's own explicit correction):

- `CMakeLists.txt` — fetches its own independent copies of SDL3 /
  Vulkan-Headers+volk / ImGui via this repo's real `cmake/Fetch*.cmake`
  modules (added to `CMAKE_MODULE_PATH`), builds
  `gte_project_assembly_probe_host.exe` (`ENABLE_EXPORTS ON`,
  `WINDOWS_EXPORT_ALL_SYMBOLS ON`, **plus** the real, load-bearing GNU-ld
  flag `-Wl,--export-all-symbols` — see Finding below) and
  `probe_module.dll` (linked against the host `.exe` alone).
- `main.cpp` — stands in for `GreatTamanaEditor.exe`.
- `probe_module.cpp` — stands in for a Project Assembly `_Game.dll`/
  `_Editor.dll`.
- `README.md` — house-style documentation (mirrors
  `tools/ci/gte_plugin_isolation_probe/README.md`'s shape).
- `.gitignore` — new entries for `/build-project-assembly-link-probe/`
  (the permanent build tree) and for this probe's own locally-fetched
  third-party artifacts (`include/`, `lib/`, `SDL3.dll`, `third_party/` under
  the probe's own source directory — these are NOT covered by the existing
  root-anchored `/include/SDL3/` etc. patterns, since this probe's
  `CMAKE_SOURCE_DIR` is its own nested directory, not the repo root).

## Toolchain/generator this was tested under

- `cmake --version` → **4.4.2**.
- Generator: **Ninja** (explicit `-G Ninja`, matching PHASE0 §2.5's own
  note that the main `build/` tree's `CMAKE_GENERATOR:INTERNAL` also reads
  `Ninja` — re-verified true, not assumed).
- Compiler: `C:/Users/F5954/scoop/apps/mingw/current/bin/c++.exe`, GNU
  16.2.0, `mingw-builds-binaries` (the same shared-CRT-capable toolchain
  PHASE1 re-confirmed).

## Step 3/4 — the basic global-sharing probe

**Result: PASS**, but only after fixing a real, load-bearing deviation from
the phase file's own literal Step 3 CMake snippet (see "Real deviations"
below). With the fix in place:

- `g_probeCounter` (explicit `__declspec(dllexport)`) — resolves correctly
  across the `.exe`/`.dll` boundary regardless of any export-all-symbols
  mechanism (expected; explicit `dllexport` always works).
- `HostPlainAdd(int,int)` (a plain, **non**-annotated free function, added
  beyond the phase file's own minimal sample specifically to exercise Step
  4's own "re-test with a real, non-trivial internal symbol" escape hatch)
  — resolves correctly ONLY when the real export-all mechanism (see below)
  is active.
- Step 4(a) (export-all ON): both link and run correctly, exit code 0.
- Step 4(b) (export-all OFF, reproduced in a throwaway scratch binary dir,
  `-DGTE_PROBE_EXPORT_ALL_SYMBOLS=OFF`, deleted immediately after): the
  build **genuinely fails to link** `probe_module.dll`, with exactly the
  expected two `undefined reference` errors —
  `HostPlainAdd(int, int)` and `ImGui::GetCurrentContext()` — while
  `g_probeCounter` (explicit dllexport) is conspicuously absent from the
  error list, confirming it linked fine either way. **This confirms the
  export-all mechanism is genuinely load-bearing, not superstition** — the
  phase file's own Step 4(b) "should fail to link" expectation held, exactly
  as anticipated, once the real (not the documented-but-inert) mechanism was
  used.

## Step 5 — the ImGui-context probe

**Result: PASS.** `GetHostImGuiContext()` and `GetModuleImGuiContext()`
returned the **identical pointer value** (confirmed live, printed:
`host ImGui context (0x...) == module ImGui context (0x...)`) — exactly one
`GImGui` context in the process, proving Finding A's hazard does not
materialize when the correct linking recipe (below) is used.

## REAL DEVIATIONS FOUND (both required to make this probe pass at all)

### Deviation 1 — `project(... LANGUAGES CXX)` alone fails to configure

The phase file's own Step 1 does not spell out the `project()` language
list. Using `LANGUAGES CXX` alone (the obvious first guess) fails outright
at CMake **generate** time (not even build time):

```
CMake Error: CMake can not determine linker language for target: volk
CMake Generate step failed.
```

**Cause:** `fetch_vulkan()`'s own `volk` target compiles `volk.c` — a plain
C translation unit — but the project never enabled the C language at all.
**Fix:** `project(GteProjectAssemblyLinkProbe LANGUAGES CXX C)`, mirroring
root `CMakeLists.txt` line 11's own identical
`project(GreatTamanaEngine LANGUAGES CXX C)`. Not mentioned anywhere in
PHASE0/PHASE2's own text — a genuinely new, mechanical finding, now recorded
in this probe's own `CMakeLists.txt` header comment.

### Deviation 2 (THE MAJOR ONE) — `WINDOWS_EXPORT_ALL_SYMBOLS` is a SILENT NO-OP on this repo's real MinGW/GNU-ld toolchain

This is exactly the class of hazard PHASE2's own file warned about ("this
repo has already been burned once by MinGW/GCC deviating from generic
documentation, see `cmake/MingwRuntime.cmake`'s own `-shared-libstdc++`
discovery") — and it is real, confirmed, and load-bearing for PHASE3.

**What was tried first (the phase file's own literal Step 3 snippet,
`WINDOWS_EXPORT_ALL_SYMBOLS ON`, nothing else):** linking `probe_module.dll`
failed:

```
undefined reference to `HostPlainAdd(int, int)'
undefined reference to `ImGui::GetCurrentContext()'
```

even though `WINDOWS_EXPORT_ALL_SYMBOLS` was `ON`. Inspecting the generated
`build.ninja` confirmed **zero** occurrence of `--export-all-symbols` (or
any other extra export-related flag) anywhere in the host executable's own
link command — CMake's property genuinely did nothing on this toolchain.

**Root cause, confirmed via `cmake --help-property WINDOWS_EXPORT_ALL_SYMBOLS`
directly (this machine's real, installed CMake 4.4.2), quoted verbatim:**

> "This property is implemented only for MS-compatible tools on Windows."

MinGW/GNU `ld` is **not** an MS-compatible tool in CMake's own
classification — the property is a documented, intentional no-op outside
the MSVC/`lib.exe`/clang-cl toolchains. This is not a bug in CMake; it is a
real, concrete gap between the original design docs' assumption (which
followed the generic, MSVC-flavored CMake documentation for
`ENABLE_EXPORTS`/`WINDOWS_EXPORT_ALL_SYMBOLS`) and this repo's actual,
locked-in GNU/MinGW toolchain.

**Real, working fix, confirmed live:** GNU `ld`'s own native
`--export-all-symbols` linker flag, applied directly via
`target_link_options(gte_project_assembly_probe_host PRIVATE "-Wl,--export-all-symbols")`.
With this flag added (`WINDOWS_EXPORT_ALL_SYMBOLS ON` left in place too,
harmless/forward-compatible in case of a future MSVC/clang-cl switch, but
doing nothing on this toolchain), both the explicit-dllexport symbol
(`g_probeCounter`) and the un-annotated symbols (`HostPlainAdd`,
`ImGui::GetCurrentContext()`) resolve correctly, and Step 4(b) (flag
removed) genuinely fails to link with exactly the expected two
`undefined reference` errors.

**PHASE3 must apply this exact recipe to the real `GreatTamanaEditor`
target:**

```cmake
set_target_properties(GreatTamanaEditor PROPERTIES
    ENABLE_EXPORTS ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON   # harmless; a no-op on this toolchain, kept for forward-compatibility
)
target_link_options(GreatTamanaEditor PRIVATE "-Wl,--export-all-symbols")
```

**not** `WINDOWS_EXPORT_ALL_SYMBOLS ON` alone, as the original design docs'
own PHASE03 sketch (and this phase file's own literal Step 3 snippet)
would have it. This is the single most important, concrete finding of this
phase.

### Deviation 3 (minor, mechanical) — the phase file's own Step 2 sample code names the module `probe_module.dll`, but the natural CMake `OUTPUT_NAME` for a target literally named `gte_project_assembly_probe_module` is `gte_project_assembly_probe_module.dll`

`main.cpp`'s `LoadLibraryW(L"probe_module.dll")` (copied verbatim from the
phase file's own Step 2 code) failed at runtime with
`GetLastError()=126` (`ERROR_MOD_NOT_FOUND`) until `probe_module`'s own
`set_target_properties(... OUTPUT_NAME "probe_module")` was added — the
CMake **target name** stays `gte_project_assembly_probe_module` (matching
Step 3's own naming convention, consistent with every other target in this
project), but the **file on disk** is renamed to `probe_module.dll` via
`OUTPUT_NAME`, matching what `main.cpp` actually loads. A one-line, purely
mechanical fix, diagnosed via `objdump -p` (confirming the actual built
filename) — not a design ambiguity.

## The exact CMake recipe for "get imgui's headers without re-linking its compiled code" (PHASE7 reuses this verbatim)

```cmake
target_include_directories(gte_project_assembly_probe_module PRIVATE
    $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>
)
```

— applied to the **module** target, which links `gte_project_assembly_probe_host`
(the host `.exe`'s import library) ALONE via `target_link_libraries(...)`,
**never** naming `imgui` in any `target_link_libraries()` call for that
target. Confirmed working: the module's own compiled object
(`probe_module.cpp.obj`) never pulls in any `imgui*.cpp.obj` object file
(none of the `imgui` static-library object files appear anywhere in the
module's own link line — only `libgte_project_assembly_probe_host.dll.a`
does), yet `#include <imgui.h>` compiles fine and
`ImGui::GetCurrentContext()` resolves, at runtime, to the exact same
pointer the host itself sees. Linking the host target alone does **not**
transitively expose `imgui`'s include directory on its own (a `PRIVATE`
link boundary breaks usage-requirement propagation for header search paths,
the identical shape to Finding C's real `GreatTamanaEditor` `PRIVATE`
link) — the explicit `target_include_directories()` generator-expression
call above is the exact, minimal, necessary addition.

## Definition of Done — checklist

- [x] Step 3/4's basic global-sharing probe passes;
      `WINDOWS_EXPORT_ALL_SYMBOLS`'s necessity is confirmed **and** a
      genuinely surprising, load-bearing correction was found and written
      down (the CMake property itself is inert on this toolchain; the real
      mechanism is `-Wl,--export-all-symbols`).
- [x] Step 5's ImGui-context probe passes and the exact CMake recipe for
      "get imgui's headers without re-linking its compiled code" is written
      down concretely above.
- [x] This report records the actual toolchain/generator (Ninja, CMake
      4.4.2, GNU 16.2.0 `mingw-builds-binaries`), freshly re-verified, not
      assumed from PHASE0 alone.
- [x] `tools/ci/gte_project_assembly_link_probe/` left in the repo
      permanently; its build directory
      (`build-project-assembly-link-probe/`) has its own new `.gitignore`
      entry with a documented comment matching the style of the 4 existing
      entries.

## What this phase did NOT do (as instructed)

- Did not touch the root `CMakeLists.txt`'s real
  `add_executable(GreatTamanaEditor ...)` block — PHASE3's job.
- Did not write any real Project Assembly folder/CMake scaffolding —
  PHASE3.
- Did not run a full build/regression of the main repo (LDD11) — only this
  probe's own small, standalone build, plus `search_in_dir`/`read_file`
  anchor re-verification per LDD6.

## New gap found, worth flagging for PHASE3

**The single biggest actionable finding of this phase:** PHASE3's own
planned CMake snippet for `GreatTamanaEditor` (per PHASE0 §2.3, Finding
A/§3.2 PHASE3 description) must **not** rely on
`WINDOWS_EXPORT_ALL_SYMBOLS` alone — it is a confirmed no-op on this
toolchain. PHASE3 must add `target_link_options(GreatTamanaEditor PRIVATE "-Wl,--export-all-symbols")`
as the actual, working mechanism (see Deviation 2 above for the full
recipe). This is not a hypothetical risk — it was mechanically reproduced
and fixed in this phase, live, on this exact machine/toolchain.

## Result

**PHASE2 is DONE.** The `ENABLE_EXPORTS` + link-against-`.exe` mechanism is
proven sound on this repo's real toolchain, including Finding A's ImGui
-context risk — but only with the corrected recipe above, which PHASE3 must
use verbatim instead of the original design docs' `WINDOWS_EXPORT_ALL_SYMBOLS`
-only sketch.
