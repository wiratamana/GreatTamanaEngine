# PHASE1 of 8 — TOOLCHAIN VERIFICATION (the switch is already done — confirm it, don't redo it)

Read `PHASE0_MASTER_STRATEGY.md` first, especially §2.2 ("The single biggest
fact this campaign's own investigation changed"). This phase implements
almost nothing new — it exists to turn a documented CLAIM (this campaign's
own investigation, dated 2026-09-28) into a mechanically-reconfirmed FACT at
the moment implementation actually begins, since time will have passed and
this repo changes fast.

**Depends on:** nothing (first phase).
**Blocks:** PHASE2's own probe is far more meaningful once this is reconfirmed
(though PHASE2's core claim — "one physical copy of a global" — is testable
even without shared CRT linkage, since it never itself crosses a
`std::string` boundary).

## The problem, stated plainly

A Project Assembly's whole reason to exist is calling real `gte_core`/
`gte_editor` functions across the `.exe`/`.dll` boundary using real
`std::string`, `std::vector`, `std::filesystem::path`, and real class types,
by value and by reference. The moment any of those crosses that boundary,
every participant MUST share one process-wide C++ runtime heap (shared/DLL
CRT linkage) — otherwise the first `std::string` constructed on one side and
destroyed on the other is instant, silent-until-it-isn't undefined behavior.

## What this campaign's own investigation already found (2026-09-28) — verify
every one of these mechanically, do not just trust this list

1. `build/CMakeCache.txt` → `CMAKE_CXX_COMPILER` points at
   `.../scoop/apps/mingw/current/bin/c++.exe` (the shared-CRT-capable
   toolchain), not `.../scoop/apps/gcc/current/...`.
2. `.../scoop/apps/mingw/current/bin/libstdc++-6.dll` exists (confirms this
   exact toolchain's own libstdc++ was built `--enable-shared`).
3. `build/CMakeCache.txt` → `GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED:INTERNAL=TRUE`.
4. `build/plugins/gte_plugin_abi/generated/gte_plugin_abi/GtePluginAbiFingerprintGenerated.h`
   → `fp.sharedRuntimeLinkage = 1;`.
5. `build/libstdc++-6.dll`, `build/libgcc_s_seh-1.dll`, `build/libwinpthread-1.dll`
   all sit directly next to `build/GreatTamanaEditor.exe`.
6. Re-reading `cmake/MingwRuntime.cmake`'s own top-of-file comment: its
   `message(WARNING "...has no libstdc++-6.dll next to it...")` branch (the
   ELSE branch of its `if(EXISTS "${GTE_MINGW_TOOLCHAIN_BIN_DIR}/libstdc++-6.dll")`
   check) must NOT fire during a fresh `cmake -S . -B build` reconfigure of
   the EXISTING `build/` tree — its absence is the confirmation signal, exactly
   as that file's own detection logic already promises.

## Step-by-step task list

**Step 1 — Re-run every one of the 6 checks above, fresh, right now, and
record the exact evidence (paths, exact strings read) in this phase's own
completion report.** Do not skip this step just because PHASE0 already lists
the same 6 facts — PHASE0 was written at investigation time; this step
re-confirms them at IMPLEMENTATION time, which may be a different moment.

**Step 2 — Do a real, fresh reconfigure of the EXISTING `build/` tree (never
a new, separate tree) and confirm the warning genuinely does not appear:**

```
cmake -S . -B build
```

Read the full configure output. Confirm zero occurrences of
`"MingwRuntime.cmake: the active CXX compiler"`. If it DOES appear, STOP —
this means the campaign's own investigation was wrong, or something reverted
the switch between 2026-09-28 and now — use `ask_questions` immediately
rather than silently treating PHASE3 onward as safe.

**Step 3 — Run the existing plugin regression probes to confirm the
(already-shared-CRT) toolchain still works end-to-end, not just that it
configures cleanly.**

**Important, confirmed correction: these two probes are NOT buildable
targets inside the main `build/` tree.** `gte_plugin_isolation_probe` and
`gte_core_player_link_probe` ARE both real `add_executable()` targets in root
`CMakeLists.txt` (confirmed, lines 1267 and 1226) — but each is wrapped in
its own `if(GTE_CORE_STANDALONE_PROBE_ONLY) ... endif()` guard, and the main
`build/` tree's own `CMakeCache.txt` reads `GTE_CORE_STANDALONE_PROBE_ONLY:BOOL=OFF`
(confirmed) — so neither target exists at all in that configuration. Running
`cmake --build build --target gte_plugin_isolation_probe` against the main
tree fails outright ("unknown target"). Each probe is instead its own tiny,
separate, manually-invocable CMake project under `tools/ci/<probe-name>/`
that nests a SECOND, independent `cmake -S . -B <inner-dir>` configure of
this same repository root with `-DGTE_CORE_STANDALONE_PROBE_ONLY=ON` (see
each probe's own `README.md` for the exact, authoritative command — do not
improvise a different invocation). Run both, from the repository root:

```
cmake -S tools/ci/gte_plugin_isolation_probe -B build-plugin-isolation-probe -G Ninja
cmake --build build-plugin-isolation-probe
build-plugin-isolation-probe\gte_plugin_isolation_inner_build\gte_plugin_isolation_probe.exe

cmake -S tools/ci/gte_core_player_link_probe -B build-player-link-probe
cmake --build build-player-link-probe
build-player-link-probe\gte_core_inner_build\gte_core_player_link_probe.exe
```

(Confirm these exact inner-build-directory names and target names still
match each probe's own `CMakeLists.txt`/`README.md` via `search_in_dir`
before running — do not guess; the two inner-build folder names differ,
`gte_plugin_isolation_inner_build` vs. `gte_core_inner_build`.) Confirm both
`.exe`s exit with code 0. These already exercise the EXISTING plugin `.dll`
boundary under real shared-CRT linkage (each nested configure inherits the
SAME real MinGW toolchain this phase already reconfirmed in Steps 1-2,
since it is picked up from the same default `CMAKE_CXX_COMPILER` — no
separate toolchain selection needed for these nested configures) — a green
run here is real, mechanical proof the switch this campaign is about to
build on top of is genuinely load-bearing today, not just a stale cache
flag. Both probes' own build trees (`build-plugin-isolation-probe/`,
`build-player-link-probe/`) already have permanent, documented `.gitignore`
entries from earlier campaigns — do not add new ones.

**Step 4 — Launch the real, current `GreatTamanaEditor.exe` from the existing
`build/` tree and confirm it still works normally, via the engine's own HTTP
diagnostics (never a manual visual-only check):**

1. `run_app_background` → `build/GreatTamanaEditor.exe`.
2. `gte_send_request` → `GET /get_logs` — confirm the existing plugin-load
   log lines from `plugins/demo_hello_world.dll` etc. still appear, exactly
   as before this phase touched anything (this phase changes nothing about
   plugin loading — a regression here would mean Step 2's reconfigure
   somehow broke something unrelated).
3. `gte_send_request` → `GET /get_swapchain` — confirm a real, non-error
   image comes back (the window actually renders).
4. `stop_app_background`.

**Step 5 — Draft (do not yet finalize) the permanent `AGENTS.md` disclosure
this campaign will commit in PHASE8**, recording: the toolchain switch's
existence, that no `task_manager/` campaign explicitly performed/documented
it, the exact toolchain path/version, and the exact date this phase
mechanically reconfirmed it (so a future reader is never misled by the many
OLDER campaigns' own honest "no actual switch happened" caveats, which were
true when THEY were written and are simply now historical).

## Definition of Done

- [ ] All 6 facts in this phase's own "what was already found" list are
      freshly, mechanically reconfirmed today, with the exact evidence
      recorded in `PHASE1_COMPLETION_REPORT.md`.
- [ ] A fresh `cmake -S . -B build` reconfigure of the existing tree produces
      NO `MingwRuntime.cmake` shared-CRT warning.
- [ ] `gte_plugin_isolation_probe` and `gte_core_player_link_probe` both build
      and exit 0, each via its OWN nested `cmake -S tools/ci/<probe-name> -B
      build-<probe-name>` configure+build (never `cmake --build build --target
      ...` against the main tree, where these targets do not exist at all).
- [ ] A fresh `GreatTamanaEditor.exe` launch, checked via `GET /get_logs` and
      `GET /get_swapchain`, shows no regression versus this repo's
      pre-campaign behavior.
- [ ] The draft `AGENTS.md` disclosure text exists (finalized/committed in
      PHASE8, not here).

## What this phase does NOT do

- Does NOT create a new `build-shared-crt` (or any other new) build tree —
  the existing `build/` tree already IS the shared-CRT tree (PHASE0 §2.2,
  LDD7).
- Does NOT touch `cmake/MingwRuntime.cmake`'s detection logic itself — it is
  already correct and generic; PHASE3 only ADDS one new function to this same
  file (Finding D), never modifies the existing detection block.
- Does NOT write any Project Assembly code yet (PHASE3 onward).
- Does NOT finalize/commit the `AGENTS.md` entry — that is PHASE8's job, once
  every other phase's own facts are also known and can be folded into one
  coherent, final write-up.
