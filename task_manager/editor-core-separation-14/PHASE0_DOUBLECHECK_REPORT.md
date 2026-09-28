# editor-core-separation-14 — PHASE0 Master Strategy Double-Check Report

Second iteration (full double-check) of this campaign's strategy files,
performed against the REAL, current source tree (every cited file path/
function name/line number re-verified with `read_file`/`read_line`/
`search_in_dir`, never trusted blindly from the phase files' own prose). A
prior, narrower pre-check pass had already reviewed PHASE4 specifically —
its content was re-read from disk (not assumed) and found accurate on every
citation checked, but it still shared one architecture-layering bug with
every other phase that touches the same function (see Finding 1 below),
since a narrower single-file pre-check could not see the cross-phase
signature implication.

## Verdict

The six strategy files were fundamentally sound in their file-path/line-
number/function-signature citations — every single citation I independently
re-verified against the real source tree (`Core.h` lines 351/624,
`ProjectAssemblyHost.cpp` lines 90-161/167-206, `HotReloadEngineStateMutex.h`
line 40, `EditorHotReloadDebugCapability.h` line 47, `EditorHost.h` lines
154-170, `EditorHost.cpp` lines 220/296/438-443, `NetworkServer.cpp` lines
1345-1389, `ProjectAssemblyBuildRunner.cpp`'s every cited line range) was
byte-for-byte correct. That is a genuinely high bar and the phase files
deserve credit for it. However, two real, independently-confirmed bugs and
several smaller gaps were found and fixed — described below.

## What was changed, and why

### 1. [CRITICAL] `gte_core` -> `gte_editor` layering violation in the planned orchestrator

**Found in:** PHASE4 (originally), propagating from PHASE3's signature.

The plan had `PerformProjectAssemblyHotReload()` — a function living in
`src/Core/Plugins/ProjectAssemblyHotReload.cpp`, the EXACT same folder whose
every other file (`ProjectAssemblyHost.cpp`, `ProjectAssemblyBuildRunner.cpp`,
etc.) is compiled into the `gte_core` static library — call
`gte::ExecutableDirectory()` directly to resolve the output/build
directories. `gte::ExecutableDirectory()` is defined ONLY in
`src/Editor/ProjectRootPath.cpp`, compiled ONLY into `gte_editor`. This is a
direct `gte_core -> gte_editor` dependency, forbidden by `AGENTS.md`'s own
Clean Architecture rule, and exactly the hazard `ProjectAssemblyBuildRunner.h`'s
OWN existing `ResolveProjectAssemblyOutputDirectory()`/
`ResolveCMakeBuildDirectory()` functions were deliberately designed to avoid
(both already take their starting directory as an explicit, caller-resolved
parameter, confirmed by re-reading that file's own header comments).

Worse: this would have compiled and linked completely fine in every ordinary
`cmake --build build` (both libraries always link together into
`GreatTamanaEditor`), and the one CI probe purpose-built to catch exactly
this class of violation, `tools/ci/gte_core_player_link_probe/`, is its own
separate, manually-invoked CMake project — confirmed NOT `add_subdirectory()`'d
from the root `CMakeLists.txt` — so it would never have run as part of this
campaign's own PHASE1-5 workflow at all. This would have shipped silently and
been discovered, if ever, only much later.

**Fix:** `PerformProjectAssemblyHotReload()`'s signature (PHASE3, which owns
"the permanent signature") now takes two additional explicit
`std::filesystem::path` parameters, `outputDirectory`/`buildDirectory`,
resolved by the CALLER — `EditorHost::Run()`'s drain point (PHASE3, itself
gte_editor-tier and already includes `ProjectRootPath.h`/
`ProjectAssemblyBuildRunner.h`) — mirroring the exact same "resolve outside,
pass in" precedent already established. PHASE4's orchestrator body no longer
includes `Editor/ProjectRootPath.h` or calls `gte::ExecutableDirectory()` at
all. Updated: PHASE0 (Section 2.2, new item 6), PHASE3 (header/`.cpp`
signature + temporary body + drain-point code), PHASE4 (Situation section +
`.cpp` body).

### 2. [CRITICAL] `ProjectAssemblyHotReloadCommandBridge::SubmitAndWait()` timeout semantics bug

**Found in:** PHASE3.

The phase file's own PROSE correctly stated the intended behavior ("unlike
`EngineCommandBridge`, a timeout here does NOT mean the main thread will
simply drop the request later... the underlying cycle keeps running to
completion regardless"), but its own pseudocode literally described clearing
`m_requested = false` on EVERY return path, including the timeout path —
exactly mirroring `EngineCommandBridge::SubmitAndWait()`'s own, deliberately
OPPOSITE, already-tested-and-locked-in behavior (confirmed via
`tests/Application/EngineCommandBridgeTests.cpp`'s own
`LateFulfillmentAfterTimeoutIsInertAndDoesNotCorruptNextRequest` test, which
proves and locks in "a timeout means give up entirely" for THAT bridge). Had
an implementer followed the pseudocode literally (a very easy, natural
mistake given how similar the two classes look), `TryPeekPendingProjectName()`
would find nothing pending the instant a client timed out, and the main
thread's drain point would NEVER run the hot-reload cycle that request had
already triggered — silently and completely breaking this whole feature's
central promise, with no crash or visible error to point at the bug.

**Fix:** Rewrote the `.cpp` implementation description in PHASE3 to state
precisely: `m_requested` is cleared in exactly ONE place, `FulfillPending()`
— never by `SubmitAndWait()` itself, on either the success or timeout path.
Added a concrete explanation of why this one divergence from
`EngineCommandBridge` is necessary, and added five new required Tier-1 tests
(3.7) that would have caught this exact class of bug immediately, most
importantly `RequestWithNoServicerTimesOutButStaysPending`.

### 3. `LDD-HRn` citations were never defined anywhere

PHASE0, PHASE3, and PHASE4 all cite `LDD-HR2`/`LDD-HR4`/`LDD-HR5` as if they
were established shorthand for specific locked decisions, but no "Locked
Design Decisions" section existed anywhere to define them (unlike this
codebase's own established convention elsewhere), and `LDD-HR1`/`LDD-HR3`
were never mentioned at all with no explanation for the gap. Added a new
"Locked Design Decisions" section to PHASE0 defining LDD-HR1 through
LDD-HR5 concretely, and tagged the Non-Goals bullet about
`IHotReloadDebugCapability` with `LDD-HR3` for consistency.

### 4. A pre-existing regression test would break with no plan to fix it

`tests/Network/ProjectAssemblyHotReloadEndpointsEndToEndTests.cpp`'s
`HotReloadValidNameReturns501NotImplemented` hard-codes the OLD "always
`501`" contract that PHASE4's own route-handler replacement deliberately
removes — nowhere did PHASE4 mention this pre-existing test needed updating,
meaning PHASE5's full `ctest` pass would have surfaced a "new failure" that
is not a real implementation bug, just a stale assumption, discovered late
and undocumented rather than fixed in the same change that caused it
(`AGENTS.md`'s own Testability rule). Added PHASE4 Section 3.6, instructing
replacement with two new tests: one confirming the new `503` behavior when
the bridge isn't wired, and one new `FakeHotReloadStandIn`-based test
(mirroring this same file's own existing `FakeSceneSnapshotStandIn`
precedent) proving the capability<->bridge<->route wiring succeeds
end-to-end with a real `200` response, without needing a live `EditorHost`.

### 5. New production files never mentioned in the CMakeLists.txt hand-maintained source list

Neither PHASE3's new `src/Application/ProjectAssemblyHotReloadCommandBridge.h/.cpp`
nor its new `src/Core/Plugins/ProjectAssemblyHotReload.h/.cpp` were flagged
as needing an entry in the root `CMakeLists.txt`'s hand-maintained
`add_library(gte_core STATIC ...)` source list — a recurring, previously-
documented mistake in this exact codebase (`editor-core-separation-11`'s own
PHASE5/PHASE6 completion reports). Added explicit reminders at both file
introduction points in PHASE3.

### 6. Minor clarifications

- PHASE3's own smoke test (3.6) now explicitly states the actual HTTP `500`
  response an implementer will see at the end of this phase (before PHASE4
  replaces the route handler), so it is not mistaken for a real failure.
- PHASE2's concurrency test (3.5) now names the existing, already-registered
  test file it should be added to, avoiding an unnecessary new
  `tests/CMakeLists.txt` entry.

## What was checked and found already correct (no change needed)

- PHASE0's two flagged corrections (no `ProjectAssemblyHost::Instance()`;
  the `GetHotReloadEngineStateMutex()` closure obligation) are concretely
  acted upon exactly where PHASE0 says they are (PHASE1 for the mutex,
  every phase for the accessor) — confirmed by direct source reading.
- Every "Depends on"/"Blocks" statement across all 5 phase files is
  mutually consistent with PHASE0's own dependency table.
- No phase asks a future implementer to change any
  `IHotReloadDebugCapability` method signature — confirmed by reading the
  real interface (`EditorCapabilities.h` line 173) and every phase's own
  usage of it.
- PHASE1's plan to lock `GetHotReloadEngineStateMutex()` around the WHOLE
  body of `TryLoadOneAssembly()`/`UnloadProjectAssembly()` (including the
  arbitrary `GTE_RegisterProject` call and every
  `ProjectAssemblyRegistrationLedger`/`ComponentTypeRegistry`/
  `EditorPanelRegistry` call it makes) is genuinely safe, non-recursive, and
  deadlock-free — confirmed by searching the whole codebase for every
  existing `GetHotReloadEngineStateMutex()` lock site (only 3, all in
  `EditorHotReloadDebugCapability.cpp`) and confirming
  `ProjectAssemblyRegistrationLedger`'s own methods never lock it.
- PHASE1/PHASE2's own file/line citations (`ProjectAssemblyBuildRunner.cpp`'s
  `g_inFlightMutex`/`RunOneBuildTarget`/`RunBuildThreadBody`/
  `TriggerProjectAssemblyCompile` line ranges) were all confirmed exactly
  correct against the live file, down to the exact closing-brace line.
- PHASE5 was judged already good enough as written — left unchanged.

## Files changed

- `PHASE0_MASTER_STRATEGY.md` — new "Locked Design Decisions" section,
  new Section 2.2 item 6, `LDD-HR3` tag added to an existing bullet.
- `PHASE2_SHARED_SYNCHRONOUS_BUILD_HELPER_AND_MESSAGE_PUMP.md` — test file
  placement clarification.
- `PHASE3_HOT_RELOAD_COMMAND_BRIDGE_AND_MAIN_LOOP_INTEGRATION.md` —
  `SubmitAndWait()`/`FulfillPending()` semantics rewritten and corrected,
  `PerformProjectAssemblyHotReload()`'s signature gained two new
  parameters, new Section 3.7 (Tier-1 bridge tests), smoke-test
  clarification, two new `CMakeLists.txt` reminders.
- `PHASE4_ORCHESTRATOR_AND_ROUTE_WIRING.md` — Situation section and the
  real orchestrator body updated to match PHASE3's revised signature (no
  more `gte::ExecutableDirectory()` call), new Section 3.6 (obsolete test
  replacement), new Definition-of-Done bullet.
- `PHASE1_PROJECT_ASSEMBLY_HOST_EXACT_PATH_LOADERS_AND_MUTEX_CLOSURE.md` —
  unchanged (found already correct).
- `PHASE5_PROBE_FIXTURE_LIVE_VERIFICATION_AND_CAMPAIGN_CLOSEOUT.md` —
  unchanged (found already correct).

No new `.md` file was created — file count remains exactly 6 (PHASE0-5) plus
this report.

## Questions asked

None — every fix above was a clear, objective correction (an architecture
rule violation, a semantics bug contradicting the plan's own stated intent,
and documentation/test-coverage gaps), not a design decision requiring human
judgment. `ask_questions` was not invoked.
