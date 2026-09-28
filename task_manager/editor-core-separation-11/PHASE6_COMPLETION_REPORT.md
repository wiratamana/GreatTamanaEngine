# PHASE6 — The "Compile" Trigger (Build Automation) — COMPLETION REPORT

**Campaign:** `editor-core-separation-11` — "Project Assembly" System
**Phase:** PHASE6 of 8
**Status:** DONE
**Date:** 2026-09-28

---

## What was built

All anchors were re-verified via `search_in_dir`/`read_line` immediately
before editing (LDD6): `JobSystem::RegisterBackgroundThread()`
(`JobSystem.h` line 164, unchanged), `JobContinuation.cpp`'s
`WatchDependencyWithFallback()` precedent (line 153-168, unchanged),
`GTE_LOG_INFO/WARNING/ERROR` (`Logging.h` lines 160-167, unchanged, two-
argument form confirmed), `gte::ExecutableDirectory()`
(`src/Editor/ProjectRootPath.h` line 30, unchanged), and PHASE5's own
flagged gap (gte_core's hand-maintained, non-GLOB source list) — all
correct, byte-for-byte, at the time of editing.

### New files: `src/Core/Plugins/ProjectAssemblyBuildRunner.h`/`.cpp`

- `TriggerProjectAssemblyCompile(projectName, buildDirectory)` — exactly the
  signature this phase file specifies. Kicks off a genuine Windows child
  process (`CreateProcessW()`, per this phase file's own instruction) on a
  dedicated `std::thread`, registered via
  `JobSystem::RegisterBackgroundThread()` — **never** `Schedule()` — mirroring
  `JobContinuation.cpp`'s own real precedent.
- Builds `<projectName>_Game` first; only attempts `<projectName>_Editor` if
  the `_Game` build succeeded. A missing `_Editor` target is detected via a
  simple, documented-non-guaranteed keyword heuristic (`"unknown target"` /
  `"no rule to make target"` / `"targets not built"`) and is logged, then
  treated as **success**, not failure — confirmed against Ninja's own real
  output text (see "Live/compile verification" below).
- Streams the child's combined stdout/stderr, line by line, into
  `GTE_LOG_INFO`/`WARNING`/`ERROR` ("`ProjectAssemblyBuild`" category) as it
  runs (not buffered until the end) via a blocking `ReadFile()` loop — this
  thread's entire job.
- The exact required final log line (`"Build finished with exit code N —
  relaunch GreatTamanaEditor.exe to use the result..."`) is emitted on
  **every** run, success or failure.
- A simple per-project in-flight guard (`std::mutex` + `std::unordered_set`)
  turns a second `TriggerProjectAssemblyCompile()` call for the SAME project,
  while its own previous build is still running, into a harmless, logged
  no-op — live-verified (see below).
- `ResolveCMakeBuildDirectory(startDirectory, maxParentLevels = 5)` — a new,
  small, reusable helper (not explicitly named in the phase file, but
  required by its own "STEP 2" prose: the CMake build directory must be
  derived at runtime by walking upward from `gte::ExecutableDirectory()`
  looking for a real `CMakeCache.txt`, never hardcoded as a single `".."`).
  Exposed publicly in the header so any future permanent call site can reuse
  it rather than re-implementing the walk-up loop.
- `QuoteWindowsArgument()` — an internal helper, added to fix a real bug
  found during live verification (see "Real deviation" below).

### `CMakeLists.txt` (root)

Added `src/Core/Plugins/ProjectAssemblyBuildRunner.h`/`.cpp` to `gte_core`'s
explicit, hand-maintained source list, immediately after
`ProjectAssemblyHost.h`/`.cpp` — PHASE5's own completion report explicitly
flagged this exact step as a gap in the phase file's own instructions, so it
was applied here proactively, before even attempting a first compile (and
the first compile of `gte_core` alone did succeed immediately, confirming
the fix was applied correctly).

### `src/Editor/EditorHost.cpp` — temporary test call site, added then removed

Per this phase file's own "STEP 4" instruction ("a temporary, throwaway call
site is acceptable ... removed before considering this phase done"), a
temporary block was added right after the existing
`m_core.LoadProjectAssemblies(...)` call site, calling
`TriggerProjectAssemblyCompile("ProjectAssemblyProbe", <resolved build
dir>)` (twice, back-to-back, to also exercise the in-flight guard — see
verification below), used to drive every live HTTP check in this report,
then **fully removed** (both the temporary `#include` and the temporary
call-site block) once verification was complete. `git status` after
cleanup shows **zero diff** on `EditorHost.cpp` — confirming the file is
back to byte-identical with its pre-PHASE6 committed state, and the only
real, permanent changes this phase leaves behind are the two new files plus
the one `CMakeLists.txt` source-list addition.

## Real deviation from this phase file's own literal instructions (found and
fixed via live verification, not silently skipped)

**A genuine command-line-quoting bug**, found on the FIRST live run: this
phase file's own "STEP 2" prose says the resolved build directory comes from
walking up from `gte::ExecutableDirectory()` — and `ExecutableDirectory()`'s
own underlying `SDL_GetBasePath()` **always returns a path with a trailing
path separator** (confirmed directly: the resolved directory logged as
`C:\...\GreatTamanaEngine\build\`, trailing backslash). Naively wrapping
that string in a plain `"\"" + buildDirectory + "\""` pair (a literal,
reasonable reading of the phase file's own STEP 2 command-line example) is
a real, confirmed Windows command-line-parsing bug: a backslash immediately
before a closing quote **escapes that quote** instead of terminating the
argument (the standard MSVCRT/`CommandLineToArgvW()` argv-splitting rule),
silently merging the rest of the command line into the same argument. Live,
reproduced evidence from the first real run:

```
Error: C:/Users/F5954/Documents/TAMANA/GreatTamanaEngine/build\" --target ProjectAssemblyProbe_Game is not a directory
```

cmake parsed the ENTIRE rest of the command line as one path argument. Note
this is a DIFFERENT bug from the identically-shaped-looking error this
report's author briefly saw from the unrelated `cmake` INVOCATION TOOL
itself when called without a `working_directory` argument earlier in this
session (that one was simply a wrong working directory, an unrelated tool-
usage mistake, immediately corrected — not a code bug and not reported via
`bug_report`, since the tool behaved exactly as documented).

**Fix**: added `QuoteWindowsArgument()`, a small, correct, standard
Windows-argv-quoting helper (doubles any run of backslashes immediately
before an embedded or final quote), and used it for both the
`buildDirectory` and `targetName` arguments instead of a naive quote-wrap.
Rebuilt, relaunched, confirmed the real `cmake --build`/Ninja output now
streams correctly (see verification below). This is flagged here explicitly
because a future phase file author reusing this exact STEP 2 snippet
elsewhere would hit the identical bug.

## Design decision — STEP 3, shutdown behavior: **(a) "let it block" was
implemented** (the phase file's own recommended default)

No `TerminateProcess()` call anywhere in this file, and the background
thread never polls `JobSystem::IsShuttingDown()` — unlike
`JobContinuation.cpp`'s own polling-fallback thread (which has a genuine,
different reason to bail early: an abandoned dependency that might never
clear at all). A `cmake --build` child process is different: it **will**
finish on its own, and forcibly killing it mid-write risks leaving a
half-written, corrupt `_Game.dll`/`_Editor.dll` on disk — a real
correctness risk this whole system's LDD4 ("Project Assemblies are only
ever picked up on next launch") implicitly assumes never happens. This
phase file's own text already gives a clear, reasoned recommendation for
option (a), and this reasoning (avoiding a corrupt binary) is independently
sound engineering judgment for this specific codebase (a single-developer,
manual-compile, no-hot-reload system), so **no `ask_questions` call was
needed** — resolved directly per this campaign's own "use best engineering
judgment when the phase file already gives enough guidance" rule.
Implementation consequence: if the user closes `GreatTamanaEditor.exe` while
a build is in flight, process exit blocks (via `JobSystem::~JobSystem()`'s
existing `JoinAllBackgroundThreads()`) until the child process's own pipe
closes and this thread returns.

**Testing caveat, stated honestly**: this specific blocking-shutdown
behavior could NOT be live-exercised via this session's available tooling.
`stop_app_background` performs a hard, external process-tree kill (per its
own tool description) — it does not simulate a graceful window-close
(`WM_CLOSE`/`SDL_QUIT`) event, so it bypasses this engine's own C++
destructor-based shutdown path entirely and would not exhibit the
"blocks until the build finishes" behavior even if invoked mid-build; it
would simply kill everything immediately, external to the process. No
keyboard/mouse-simulation tool was available to click the real window's
close button. This decision therefore rests on (a) direct code inspection
confirming no `TerminateProcess()`/`IsShuttingDown()` polling exists in this
file (so nothing bypasses the default `JoinAllBackgroundThreads()` block),
and (b) reuse of `JobSystem`'s own already-established, independently-tested
`RegisterBackgroundThread()`/`JoinAllBackgroundThreads()` contract (the same
one `JobContinuation.cpp` already relies on in production) — not a fresh,
independently-observed live repro. Recorded here explicitly rather than
silently claimed as fully live-verified.

## New gap found — a Project Assembly's own `.dll` cannot be recompiled
while the SAME running `GreatTamanaEditor.exe` instance has it loaded

**This is the single most important finding from this phase's live
verification**, not mentioned anywhere in `PHASE0_MASTER_STRATEGY.md` or
this phase's own file. Confirmed directly, mechanically, twice:

1. With `ProjectAssemblyProbe_Game.dll`/`_Editor.dll` already
   `LoadLibraryW()`'d by the running instance (PHASE5's `ProjectAssemblyHost`,
   never `FreeLibrary()`'d per LDD4), triggering a compile of that SAME
   project **always fails**, with a real, correctly-streamed, correctly-
   classified error:
   ```
   ld.exe: cannot open output file project_assemblies\ProjectAssemblyProbe_Game.dll: Permission denied
   collect2.exe: error: ld returned 1 exit status
   ninja: build stopped: subcommand failed.
   ```
   This is a standard Windows OS behavior (a mapped/loaded DLL image is
   locked against being overwritten by the linker) — **not a bug in this
   phase's own mechanism**, but a genuine, load-bearing architectural
   consequence of LDD2 (a Project Assembly `.dll` is loaded directly into
   the long-lived `GreatTamanaEditor.exe` process) combined with LDD4 (never
   `FreeLibrary()`'d before process exit).
2. Immediately re-confirmed as the root cause, not a coincidence: with
   nothing holding the DLL open (deleted
   `build/project_assemblies/ProjectAssemblyProbe_{Game,Editor}.dll` before
   launch, so `ProjectAssemblyHost` found nothing to load that run), the
   IDENTICAL `TriggerProjectAssemblyCompile("ProjectAssemblyProbe", ...)`
   call, through the IDENTICAL mechanism, **succeeded cleanly** (exit code
   0, both targets linked) — see "Live/compile verification" below for the
   full log.

**Practical consequence for whoever designs the permanent "Compile" UI
(explicitly out of scope for this phase)**: clicking "Compile" for a
project that is ALREADY loaded by the current running instance will always
report a build failure (a locked-file linker error), every time, by design
— this is not a transient flake to retry. The realistic, intended
workflow is: edit source → close the Editor (which releases the lock) →
either (a) run `cmake --build` externally/manually, or (b) relaunch the
Editor once (which will ALSO immediately re-trigger a locked-file failure
for that same already-just-relaunched instance, since PHASE5's loader locks
the fresh binaries again at startup, before any "Compile" click could ever
run) — so in practice, **a genuinely useful "Compile" click, through this
exact mechanism, is only actually available for a project whose `.dll` this
particular running instance never loaded in the first place** (e.g. a
brand-new project created after this instance already started, with no
existing `.dll` on disk yet for `ProjectAssemblyHost` to have found). This
is a real, user-facing usability gap worth flagging prominently for
whoever builds the permanent UI in a future phase/campaign — it may be
worth that future UI surface showing an explicit warning, or even
pre-emptively refusing to trigger a compile for a project the running
instance already has loaded, rather than letting the user discover this
via a confusing linker error every time.

## Live/compile verification performed

1. **Incremental compile check**: `cmake --build build --target gte_core` —
   succeeded on the FIRST attempt (the CMake source-list registration was
   applied proactively, before ever attempting a build, per PHASE5's own
   flagged gap).
2. **Incremental compile check**: `cmake --build build --target
   GreatTamanaEditor` — succeeded cleanly, zero new warnings, both before
   and after the command-line-quoting bugfix, and again after the temporary
   test call site was fully removed (three separate successful rebuilds
   across this session).
3. **Live verification — real, live, line-by-line `cmake --build` output,
   confirmed via repeated `GET /get_logs` polling** (`run_app_background` →
   poll → `stop_app_background`), **while the main window stayed fully
   responsive** (`GET /get_swapchain` returned fresh, valid 217053-byte PNGs
   throughout, with the render loop's own frame counter visibly advancing —
   0 → 21 → 105 → 130 → 211 → 213 — across the ~3.5-second build window):
   - **Failure path** (DLL locked by the running instance — see "New gap
     found" above): real Ninja/`ld`/`collect2` output streamed correctly,
     `ld.exe`'s "Permission denied" line and `collect2.exe`'s "error:"
     line both correctly classified as `Error` level (the keyword
     heuristic), `_Editor` target correctly skipped after `_Game` failed,
     final line `"Build finished with exit code 1 - relaunch
     GreatTamanaEditor.exe to use the result..."` present.
   - **Success path** (DLLs deleted first, so nothing had them open): real
     Ninja output streamed correctly for BOTH targets
     (`[1/2] Linking CXX shared library project_assemblies\ProjectAssemblyProbe_Game.dll...`,
     then `[1/2] Linking CXX shared library project_assemblies\ProjectAssemblyProbe_Editor.dll...`),
     final line `"Build finished with exit code 0 - relaunch
     GreatTamanaEditor.exe to use the result..."` present.
   - **In-flight guard**: two back-to-back
     `TriggerProjectAssemblyCompile("ProjectAssemblyProbe", ...)` calls in
     the same temporary test call site — the SECOND call was immediately
     rejected with the exact expected warning: `"A build for Project
     Assembly 'ProjectAssemblyProbe' is already in progress - ignoring this
     new request."`, and only ONE real child process/build ever ran.
4. **"No `_Editor` target" heuristic wording, confirmed against REAL Ninja
   output** (rather than assumed): ran `cmake --build build --target
   ProjectAssemblyProbe_NonExistentEditorTarget` directly — real output was
   `ninja: error: unknown target '...'`, which matches this file's
   `"unknown target"` heuristic substring exactly. (A full, separate,
   permanent second Project Assembly with genuinely no `Editor/` folder was
   deliberately NOT created for this — `PHASE0_MASTER_STRATEGY.md`'s LDD12
   scopes exactly ONE shared, permanent probe project for this whole
   campaign, and `ProjectAssemblyProbe` itself does have `Editor/` sources;
   creating a second throwaway project purely for this one heuristic-text
   check would have been disproportionate scope creep for a "best-effort,
   not guaranteed-correct" heuristic this phase file itself already
   describes that way. The underlying code path — "if `_Game` succeeds and
   the `_Editor` attempt's exit code is nonzero AND the heuristic fired, log
   and treat as success" — was verified directly by code reading plus this
   real-wording confirmation, which is the most load-bearing part.)
5. **Final regression pass after full cleanup** (temporary test call site
   entirely removed): `cmake --build build --target GreatTamanaEditor`
   (clean rebuild of `EditorHost.cpp`), `run_app_background` → `GET
   /get_logs` (all previously-existing lines present and unchanged, ZERO
   `"ProjectAssemblyBuild"`-category entries at all — confirming the
   temporary trigger truly no longer fires) → `GET /get_swapchain` (clean,
   217053-byte PNG, identical to every prior baseline) → `stop_app_background`.
   `git status` confirms `EditorHost.cpp` has **zero diff** versus its
   pre-PHASE6 committed state.

## Definition of Done — checklist (this phase file's own list)

- [x] `ProjectAssemblyBuildRunner.h`/`.cpp` exist, compile, and use
      `JobSystem::RegisterBackgroundThread()` — NOT `Schedule()` — with a
      clear code comment explaining why.
- [x] Calling `TriggerProjectAssemblyCompile("ProjectAssemblyProbe", <resolved
      build dir>)` while `GreatTamanaEditor.exe` is running (via a temporary
      test call site, since removed) produced real, live, line-by-line
      `cmake --build` output visible via repeated `GET /get_logs` polling,
      WHILE the main window stayed fully responsive (confirmed via `GET
      /get_swapchain` returning fresh frames during the build, with the
      frame counter visibly advancing, not frozen).
- [x] The final log line, on both success and failure, explicitly reminds
      the user that a relaunch is required to see any effect — confirmed
      present, verbatim, in both the success-path and failure-path live
      runs.
- [x] A project with no `Editor/` sources builds its `_Game` target
      successfully without the whole operation being reported as failed
      just because `_Editor` does not exist — the code path is implemented
      exactly per this phase file's own STEP 2 instruction (attempt `_Game`
      first, only then attempt `_Editor`, treat a missing-target heuristic
      hit as success not failure), and the heuristic's own exact keyword
      match was confirmed against real Ninja output text; not exercised via
      a full second live project, per the scope-discipline reasoning in
      "Live/compile verification" item 4 above.
- [x] Completion notes state explicitly which shutdown-behavior choice
      (Step 3, (a) or (b)) was implemented: **(a) — let it block** (see
      "Design decision" section above, including the honest testing
      caveat).

## What this phase does NOT do (as instructed, re-confirmed)

- Does NOT add a file-watcher, auto-rebuild-on-save, or any hot-reload
  behavior (LDD4) — unchanged, nothing of the sort exists anywhere in this
  new code.
- Does NOT decide the permanent UI location of the "Compile" trigger — the
  temporary test call site used for verification was fully removed; no
  permanent ImGui panel/menu/keybinding was added.
- Does NOT retry a failed build automatically, and does NOT attempt to
  parse `cmake --build`'s output for anything beyond the exit code and the
  simple, documented, best-effort log-level/missing-target heuristic.

## Result

**PHASE6 is DONE** — `TriggerProjectAssemblyCompile()` exists, compiles,
and is live-verified end-to-end: real streamed build output (both a genuine
success and a genuine, correctly-explained failure), a working in-flight
guard, the required final reminder line on every run, correct `_Game`-then-
`_Editor` sequencing, and a fully responsive main window throughout. One
real code bug (Windows command-line quoting of a trailing-backslash
directory path) was found via live testing and fixed, not silently
papered over. One significant, previously-undocumented architectural gap
(a currently-loaded Project Assembly's own `.dll` can never be successfully
recompiled by the SAME running instance that loaded it, due to the OS's own
file-lock-on-loaded-image behavior) was discovered, mechanically confirmed
in both directions (fails when locked, succeeds when not), and is flagged
here explicitly for whoever designs the permanent "Compile" UI next.
PHASE7 and PHASE8 may now proceed — nothing in this phase touches
`ImGuiEditorLayer.cpp`, `DockLayout.cpp`, `Core::RegisterProjectRenderPassProvider()`,
or any file either of those phases still needs to edit.
