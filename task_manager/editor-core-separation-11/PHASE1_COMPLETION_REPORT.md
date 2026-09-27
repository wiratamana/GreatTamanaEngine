# PHASE1 — Toolchain Verification — COMPLETION REPORT

**Campaign:** `editor-core-separation-11` — "Project Assembly" System
**Phase:** PHASE1 of 8
**Status:** DONE
**Date of this mechanical re-confirmation:** 2026-09-28

---

## What was done

This phase writes zero new production code (as expected — it is a
re-confirmation phase, not an implementation phase). It mechanically
re-ran every one of `PHASE1_TOOLCHAIN_VERIFICATION.md`'s Step 1-4 checks,
fresh, right now, against the real, current repository state.

## Step 1 — The 6 facts, freshly re-confirmed with exact evidence

1. **`build/CMakeCache.txt` line 92:**
   `CMAKE_CXX_COMPILER:FILEPATH=C:/Users/F5954/scoop/apps/mingw/current/bin/c++.exe`
   — confirmed via `search_in_dir`. Still the shared-CRT-capable
   `mingw-builds-binaries` toolchain, not `scoop/apps/gcc/current`.
2. **`C:\Users\F5954\scoop\apps\mingw\current\bin\libstdc++-6.dll` exists** —
   confirmed via `browse_dir` of that folder (2.4 MB file listed, alongside
   `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`, `libatomic-1.dll`,
   `libgfortran-5.dll`, `libgomp-1.dll`, `libquadmath-0.dll`) — this exact
   toolchain's own libstdc++ was genuinely built `--enable-shared`.
3. **`build/CMakeCache.txt` line 930:**
   `GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED:INTERNAL=TRUE` — confirmed via
   `search_in_dir`.
4. **`build/plugins/gte_plugin_abi/generated/gte_plugin_abi/GtePluginAbiFingerprintGenerated.h`
   line 33:** `fp.sharedRuntimeLinkage = 1;` — confirmed via `read_file`.
   Also confirms `kCompilerId="GNU"`, `compilerVersionMajor/Minor/Patch =
   16/2/0` — matches the toolchain in fact 1/2 exactly.
5. **`build/libstdc++-6.dll`, `build/libgcc_s_seh-1.dll`,
   `build/libwinpthread-1.dll` all sit directly next to
   `build/GreatTamanaEditor.exe`** — confirmed via `browse_dir` of `build/`
   (all three present, `build/GreatTamanaEditor.exe` itself is 41.3 MB, all
   four last written 2026-09-25 20:24 as PHASE0 already noted — i.e. this
   is still the same, unchanged, already-built shared-CRT binary from before
   this campaign started; nobody has silently rebuilt/reverted it since).
6. **Re-reading `cmake/MingwRuntime.cmake` line 79:** the ELSE-branch
   warning text is
   `"MingwRuntime.cmake: the active CXX compiler (${CMAKE_CXX_COMPILER}) has "...`
   — confirmed via `search_in_dir`, so the exact string to grep for in Step
   2's reconfigure output is known precisely (not paraphrased from memory).

All 6 facts: **freshly reconfirmed, identical to PHASE0's own investigation
findings.** Nothing drifted between 2026-09-28 (PHASE0's investigation) and
now (this phase's own re-run, same calendar day, several hours later).

## Step 2 — Fresh reconfigure of the existing `build/` tree

Ran `cmake -S . -B build` (working directory: repo root). Output: normal
third-party "already present, skipping download" lines, then
`-- Configuring done` / `-- Generating done` / `-- Build files have been
written to: .../build`. **Zero occurrences of
`"MingwRuntime.cmake: the active CXX compiler"`** in the full configure
output. The only warning present was the pre-existing, unrelated
`third_party/ktx/cmake/version.cmake` "Error retrieving version from GIT
tag. Falling back to 0.0.0-noversion" warning (this exact warning is
already called out as pre-existing/unrelated in
`editor-core-separation-10/PHASE1_COMPLETION_REPORT.md` — not new, not
caused by this phase). **Confirmed: the shared-CRT toolchain switch is
still genuinely active on the existing tree; no revert happened.**

## Step 3 — Both regression probes built and run, exit code 0

Re-verified the exact command/target/inner-build-directory names via
`read_file` on each probe's own `README.md` before running (LDD6) — both
matched the phase file's own listed commands exactly, **except one real
deviation**, documented below.

### `gte_plugin_isolation_probe`

```
cmake -S tools/ci/gte_plugin_isolation_probe -B build-plugin-isolation-probe -G Ninja
cmake --build build-plugin-isolation-probe
build-plugin-isolation-probe\gte_plugin_isolation_inner_build\gte_plugin_isolation_probe.exe
```

Ran exactly as documented, no deviation. Outer configure succeeded, inner
build compiled `gte_core` + 6 demo plugins + the probe executable cleanly,
final link succeeded with the shared-CRT DLLs staged next to the `.exe`.
Execution output:

```
Loaded 'DemoEditorPanelPlugin' - IRenderFeatureModule_v1: no
Loaded 'HelloWorldPlugin' - IRenderFeatureModule_v1: no
Loaded 'DemoRenderFeaturePlugin' - IRenderFeatureModule_v1: yes
Loaded 'DemoRenderFeaturePluginSecond' - IRenderFeatureModule_v1: yes
Loaded 'DemoRenderFeatureV2Plugin' - IRenderFeatureModule_v1: no
Loaded 'DemoRenderFeatureV2SecondPlugin' - IRenderFeatureModule_v1: no
PASS: 6 plugin(s) loaded, exactly 2 implement IRenderFeatureModule_v1, and
this file never once asked any plugin for IEditorPanelModule_v1.
```

Exit code: **0**.

### `gte_core_player_link_probe`

```
cmake -S tools/ci/gte_core_player_link_probe -B build-player-link-probe -G Ninja   <-- see deviation note below
cmake --build build-player-link-probe
build-player-link-probe\gte_core_inner_build\gte_core_player_link_probe.exe
```

Outer configure succeeded (after the deviation fix below), inner build
compiled `gte_core` + 3 demo plugins + the probe executable cleanly, final
link succeeded (this is the probe whose whole job is proving `gte_core.a`
has zero unresolved `gte_editor`-only symbols — a real link success here
IS the proof). Execution output:

```
Bonus check SKIPPED: this machine's Vulkan driver lacks
VK_EXT_headless_surface (matches the existing, documented
CoreHeadlessConstructionTest skip). Real reason: vkCreateInstance failed
(VkResult=-7)
```
(stderr: `[Vulkan] Validation was requested but VK_LAYER_KHRONOS_validation
is not available on this system - continuing without it.` — pre-existing,
unrelated to this phase, matches this probe's own documented "either PASS
or a clean, documented SKIPPED is acceptable" contract.)

Exit code: **0**.

### Deviation from the phase file's own Step 3 command (real, found, fixed here)

The phase file's own corrected Step 3 listing gives the
`gte_core_player_link_probe` outer configure command **without** `-G
Ninja`:
```
cmake -S tools/ci/gte_core_player_link_probe -B build-player-link-probe
```
Running that exact command on this machine failed outright:
```
CMake Error at CMakeLists.txt:18 (project):
  Running 'nmake' '-?' failed with: no such file or directory
```
This machine's CMake default generator resolves to `NMake Makefiles`
(Visual Studio toolchain detected on `PATH`), and `nmake.exe` is not
installed/reachable — even though this outer wrapper project declares
`project(GteCorePlayerLinkProbe LANGUAGES NONE)`, CMake still picks and
validates a generator before ever reaching the inner nested-cmake
`add_custom_target()` (which itself already correctly passes `-G Ninja`
for the REAL inner configure — this failure was purely in the OUTER
wrapper configure, one layer earlier). **Fix applied: added `-G Ninja` to
the outer configure command too** (`cmake -S tools/ci/gte_core_player_link_probe
-B build-player-link-probe -G Ninja`), mirroring exactly what
`gte_plugin_isolation_probe`'s own sibling command already does one line
above it in the same phase file. This is a machine-local PATH/generator
quirk, not a code bug in either probe's `CMakeLists.txt` — no probe file
was edited. **Recorded here as a note for whoever runs this phase file's
Step 3 commands verbatim on a similarly-configured machine**: always pass
`-G Ninja` explicitly to both probes' outer configure, not just the one
that already had it written down.

(Also required deleting the initially-failed `build-player-link-probe/`
tree, since a failed configure's own partial `CMakeCache.txt` still
records the old, wrong generator choice and refuses a second configure
attempt in the same directory with a different `-G` value — a normal CMake
behavior, not a bug.)

## Step 4 — Live HTTP-driven verification of `GreatTamanaEditor.exe`

1. `run_app_background` → `build/GreatTamanaEditor.exe` (PID 7752).
2. `GET /get_logs` → 30 entries returned. All 8 demo plugins load
   successfully (`DemoEditorPanelPlugin`, `HelloWorldPlugin`,
   `DemoRenderFeaturePlugin`, `DemoRenderFeaturePluginSecond`,
   `DemoRenderFeatureV2Plugin`, `DemoRenderFeatureV2SecondPlugin`,
   `DemoRenderFeatureV3Plugin` and 2 more V3 variants), `NetworkServer`
   reports `listening on 127.0.0.1:8080`, `EditorHost` reports full
   construction success. The only `Warning`-level entries are pre-existing,
   documented, unrelated ones ("2 loaded plugins implement
   IRenderFeatureModule_v1 — only the LAST-registered one's render output
   will be visible", "...both declared priority 0... falling back to a
   stable lexical name tie-break") — both point at
   `docs/conventions/plugin-architecture.md`, nothing to do with this
   phase, and nothing new versus what a pre-campaign run would show. No
   regression.
3. `GET /get_swapchain` → HTTP 200, real 217053-byte PNG image returned,
   loaded and visually inspected: a genuine rendered Editor window (menu
   bar, Hierarchy/Inspector/Scene panels, a rendered gradient-sphere scene
   view, dock tabs including "Demo Plugin Panel" showing "Hello from a
   plugin!"). Confirms the window actually renders end-to-end, not just
   that the process started.
4. `stop_app_background(pid: 7752)` → stopped cleanly.

**No regression versus this repo's pre-campaign behavior.**

## Step 5 — Draft `AGENTS.md` disclosure (NOT committed here — PHASE8's job)

Draft text, to be folded into `AGENTS.md` verbatim or near-verbatim by
PHASE8, once every other phase's own facts are also known:

> ## Shared-CRT MinGW Toolchain Switch (silent, undocumented, already done)
>
> Sometime between the `editor-core-separation-9` campaign and
> 2026-09-28, this repository's default `build/` tree quietly switched its
> active `CMAKE_CXX_COMPILER` from `scoop/apps/gcc/current` (a
> `--disable-shared` GCC, per every campaign through
> `editor-core-separation-9`/`editor-enchancements-1`) to
> `scoop/apps/mingw/current/bin/c++.exe` — GCC 16.2.0, the
> `mingw-builds-binaries` distribution, built `--enable-shared`. **No
> `task_manager/` campaign folder documents a deliberate decision to
> perform this switch** — it happened as ad-hoc local machine setup, never
> tracked as its own campaign. The `editor-core-separation-11` campaign
> ("Project Assembly" system) discovered this fact during its own PHASE0
> investigation (2026-09-28) and mechanically re-confirmed it still held,
> a second time, at its own PHASE1 (2026-09-28, same day, several hours
> later). **This is genuinely load-bearing**: every plugin/Project
> Assembly `.dll` boundary that crosses real `std::string`/`std::vector`/
> class-type C++ ABI is only safe because of this switch — do not assume
   it reverted just because an OLDER campaign's own completion report
> (anything before `editor-core-separation-11`) honestly says "no actual
> switch happened yet", which was true when THOSE reports were written and
> is now simply historical. Verify with:
> `build/CMakeCache.txt` → `CMAKE_CXX_COMPILER:FILEPATH` should read
> `.../scoop/apps/mingw/current/bin/c++.exe`, and
> `GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED:INTERNAL` should read `TRUE`.

## Definition of Done — checklist

- [x] All 6 facts freshly, mechanically reconfirmed today (2026-09-28),
      exact evidence recorded above.
- [x] A fresh `cmake -S . -B build` reconfigure of the existing tree
      produced NO `MingwRuntime.cmake` shared-CRT warning.
- [x] `gte_plugin_isolation_probe` and `gte_core_player_link_probe` both
      built and exited 0, each via its own nested outer/inner configure+
      build (never against the main `build/` tree).
- [x] A fresh `GreatTamanaEditor.exe` launch, checked via `GET /get_logs`
      and `GET /get_swapchain`, showed no regression.
- [x] The draft `AGENTS.md` disclosure text exists (above; NOT committed
      to `AGENTS.md` itself — that stays PHASE8's job, per the phase file's
      own explicit "What this phase does NOT do" list).

## What this phase did NOT do (as instructed)

- Did not create a new `build-shared-crt` (or any other new) build tree —
  the existing `build/` tree was reconfigured in place only.
- Did not touch `cmake/MingwRuntime.cmake`'s detection logic.
- Did not write any Project Assembly code (PHASE3 onward).
- Did not commit/finalize the `AGENTS.md` entry.

## New gap found (worth flagging for whoever runs this phase file again)

The phase file's own Step 3 command for `gte_core_player_link_probe`'s
OUTER configure omits `-G Ninja`, unlike the sibling command for
`gte_plugin_isolation_probe` immediately above it, which already includes
it. On this machine (default CMake generator resolves to `NMake Makefiles`,
and `nmake.exe` is not on `PATH`), the command as literally written in the
phase file fails outright at the outer `project()` call, before ever
reaching the inner nested-cmake configure. This is a one-line, mechanical,
non-ambiguous fix (add `-G Ninja`), not a design decision — resolved here
directly rather than raised via `ask_questions`, since the phase file's own
sibling command already establishes the correct, intended pattern one line
above. No `PHASE1_TOOLCHAIN_VERIFICATION.md` edit was made (LDD6/this
phase's own scope is verification, not phase-file editing) — this note is
the permanent record for future readers instead.

## Result

**PHASE1 is DONE.** The shared-CRT toolchain switch is mechanically
re-confirmed, live, right now, exactly as PHASE0 claimed. PHASE2 (the
`ENABLE_EXPORTS` + link-against-`.exe` feasibility probe) may proceed on
the assumption that real `std::string`/`std::vector`-crossing `.exe`/`.dll`
calls are safe on this toolchain.
