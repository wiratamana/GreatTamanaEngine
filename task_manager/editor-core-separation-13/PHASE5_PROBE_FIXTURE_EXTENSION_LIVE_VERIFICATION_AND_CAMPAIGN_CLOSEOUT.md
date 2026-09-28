# editor-core-separation-13 — PHASE5: Probe Fixture Extension, Live Isolation Test, Campaign Closeout

Parent: `PHASE0_MASTER_STRATEGY.md` — read it first.
Prior phase reports: read PHASE1-4's own `PHASEn_COMPLETION_REPORT.md` in
full before starting — this phase's own live test directly depends on every
prior phase's real, shipped behavior, especially PHASE3's answer to whether
`ProjectAssemblyProbe`'s ledger was already partially populated before this
phase runs.

Depends on: PHASE1, PHASE2, PHASE3, PHASE4 (needs a fully-linking, feature-
complete `GreatTamanaEditor.exe` with every new method from all four prior
phases).
Blocks: nothing (final phase of this campaign).

---

## Step 1: The Goal

Prove, live, against a real running `GreatTamanaEditor.exe`, that the WHOLE
unload sequence built across PHASE1-4 works correctly, END TO END, for a
REAL Project Assembly exercising all three hazards at once (custom Editor
panel, custom render pass, AND — new in this phase — a custom ECS component
type), and that the process survives, provably, for several seconds of
continued normal operation afterward. This is the ONE phase in this whole
campaign allowed to run a full build and a full `ctest` regression pass, per
this campaign's own workflow rule (every earlier phase used incremental/
targeted compiles only).

## Step 2: The Situation

Confirmed, current, `Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp` —
registers a real render-graph pass ("ProjectAssemblyProbe.FillTexture") but
registers **zero** ECS component types (confirmed, `search_in_dir` for
`RegisterComponentType<` inside `Projects/ProjectAssemblyProbe/` returns
nothing). `Assets/Editor/HelloEditorPanel.cpp` registers a real Editor panel
("Probe Panel"). This phase adds the missing third capability — a throwaway
custom component type, `ProbeHotReloadMarker` — specifically so this
campaign's own live isolation test actually exercises Hazard 1 (the
duplicate-registration `assert()`) for real, not just in the isolated
unit test PHASE1/PHASE3 already wrote.

Confirmed, current, `src/ECS/Reflection/ReflectFieldMacros.h`'s
`GTE_REFLECT_FIELD(ComponentType, member)` macro and
`src/ECS/Reflection/BuiltinComponentReflection.cpp`'s own worked examples —
this is the exact, only pattern to follow; do not invent a different
reflection mechanism for this one throwaway type.

`AGENTS.md`'s "Project Assembly System" section (lines 792-914) documents the
base system; its own "Project Assembly Hot Reload — Live Debug Surface
(BIG-STEP 1 only)" subsection immediately below it (lines 916-961) already
documents everything BIG-STEP 1 shipped — this phase appends a new BIG-STEP 2
subsection immediately after THAT subsection ends (i.e. after its closing
"...full four-phase writeup." line, right before the following
"## Testability & Regression Safety" heading), per this file's own
established convention of one subsection per campaign. Re-confirm these exact
line numbers by re-reading `AGENTS.md` before inserting — they will drift as
other unrelated edits land between now and when this phase actually runs.

## Step 3: The Plan

### 3.1 — Extend `Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp`

Add, inside the anonymous namespace, ABOVE `RegisterProbeGame()`:

```cpp
// editor-core-separation-13 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 2), PHASE5 - a deliberately trivial, throwaway custom ECS
// component type, added SPECIFICALLY to exercise Hazard 1
// (ComponentTypeRegistry's duplicate-registration assert) for real, against
// a genuine Project Assembly .dll, not just an isolated unit test. Carries
// no meaningful runtime data to preserve across a reload (state
// snapshot/restore is BIG-STEP 4's job, not this campaign's) - `value` only
// exists so the reflection macro has a real field to reflect.
struct ProbeHotReloadMarker {
    int value = 0;
};
```

Add, inside `RegisterProbeGame(gte::Core& core)`, immediately after the
existing `GTE_LOG_INFO` line (before the `RegisterProjectRenderPassProvider`
call, order does not matter functionally but keeps registrations grouped
logically):

```cpp
gte::RegisterComponentType<ProbeHotReloadMarker>("ProbeHotReloadMarker", {
    GTE_REFLECT_FIELD(ProbeHotReloadMarker, value),
});
```

Add the two needed `#include`s at the top of `HelloGame.cpp`:
```cpp
#include "../../../src/ECS/Reflection/ComponentTypeRegistry.h"
#include "../../../src/ECS/Reflection/ReflectFieldMacros.h"
```
(confirm the exact relative path depth matches this file's own existing
`#include "../../../src/Core/Core.h"` convention — count `../` segments
against that already-working line, do not guess a different depth).

**Do not attach this component to any entity** — this phase, and this whole
campaign, only needs the TYPE registered/unregistered correctly; attaching
it to a live entity and proving its VALUE survives a reload is explicitly
BIG-STEP 4's job (a later, separate campaign), not this one's.

### 3.2 — Compile-only iterate on the probe project first (cheap, safe, zero engine-state risk)

Per BIG-STEP 1's own worked example (still valid, still available): before
touching the live running engine at all, use
`POST /project_assembly/debug/compile_only?name=ProjectAssemblyProbe`
against a `run_app_background`-launched `GreatTamanaEditor.exe`, polling
`GET /get_logs?category=ProjectAssemblyBuild`, to get fast, real compiler
feedback on 3.1's new code — zero risk to the running engine, since this
route never touches live state. Iterate until it compiles clean. Then
`stop_app_background` this throwaway instance before continuing (the
NEXT step needs a fresh instance whose `ProjectAssemblyProbe_Game.dll`
was loaded AFTER this new compiled output exists on disk).

### 3.3 — Full build

```
cmake --build build
```
(Working directory: repository root.) Fix any error. This also rebuilds
`ProjectAssemblyProbe_Game`/`_Editor` against the freshly-compiled-and-
verified (3.2) source, and picks up every engine-side change from
PHASE1-4.

### 3.4 — Launch and run the full live isolation test

`run_app_background` the freshly-built `GreatTamanaEditor.exe`. Wait a few
seconds for `NetworkServer::Start()` to bind. Then, via `gte_send_request`,
in this exact order (mirrors this campaign's own PHASE0 Step 1 success bar,
and BIG-STEP 2's external Definition of Done):

**Baseline (before any unload):**
1. `GET /project_assembly/debug/loaded_assemblies` → confirm both
   `ProjectAssemblyProbe_Game.dll` and `ProjectAssemblyProbe_Editor.dll`
   are listed.
2. `GET /project_assembly/debug/ledger?name=ProjectAssemblyProbe` → confirm
   the JSON body's `component_type_names` now contains
   `"ProbeHotReloadMarker"`, `panel_names` contains `"Probe Panel"`,
   `render_pass_names` contains `"ProjectAssemblyProbe.FillTexture"`
   (exact field names per `BuildLedgerEntryResponseJson()`,
   `src/Network/NetworkRoutes.cpp`) — **all three non-empty, for the
   first time in this whole campaign** (PHASE3's own live check could only
   confirm the panel/render-pass names; this is the first point this
   campaign can confirm the FULL three-way ledger).
3. `GET /project_assembly/debug/component_types` → confirm
   `"ProbeHotReloadMarker"` appears in the list, alongside the 5 built-ins.
4. `GET /list_tabs` → confirm `"Probe Panel"` appears.
5. `GET /render_graph` → confirm `"ProjectAssemblyProbe.FillTexture"`
   appears.
6. `GET /get_swapchain` → visual baseline screenshot, confirm normal
   rendering.

**The actual unload cannot be triggered over HTTP yet** (no BIG-STEP 3
orchestrator exists — `POST /project_assembly/hot_reload` still answers a
permanent `501`, unchanged by this campaign). Per this campaign's own
PHASE0/BIG-STEP 2's own Definition of Done, `UnloadProjectAssembly()` must
be exercised via a **temporary, throwaway direct call**, not a permanent
new HTTP route (adding a permanent "force-unload over HTTP" endpoint would
violate BIG-STEP 1's own explicit non-goal of never adding a mutating debug
route beyond the two it already locked in). Two acceptable ways to do this,
in order of preference — pick whichever is faster to implement correctly,
and document the choice in the completion report:

  (a) **Preferred**: temporarily repurpose the ALREADY-EXISTING
      `TriggerHotReload()`/`POST /project_assembly/hot_reload` PLACEHOLDER
      body, for THIS PHASE ONLY, to call `ProjectAssemblyHost::
      UnloadProjectAssembly()` directly (skipping the backup/compile/reload
      parts entirely — this is NOT a real hot reload, just this phase's own
      throwaway test hook), run the live test below, THEN REVERT this
      placeholder body back to its exact permanent `false`/`501` contract
      before this phase ends (confirmed via a second compile-check and a
      final `git diff` showing `EditorHotReloadDebugCapability::
      TriggerHotReload()`/the route handler are byte-for-byte unchanged
      versus PHASE4's own end state). This keeps the test genuinely live/
      HTTP-driven without shipping a new permanent capability. **Known, harmless
      side-effect of this exact approach, so it is not mistaken for a failure**:
      `NetworkServer.cpp`'s own `POST /project_assembly/hot_reload` handler
      (confirmed, current) only has a `false`→`501` branch coded — if
      `TriggerHotReload()` is temporarily made to return `true`, that handler
      falls through to its own defensive `res.status = 500;` "unexpected: ..."
      branch, since it was never given a real success path (that is BIG-STEP
      3's job, not this campaign's). The unload itself still happens
      SERVER-SIDE, synchronously, before that HTTP response is ever written —
      this 500 is purely a stale response-body artifact of the placeholder
      route, not a sign the unload failed. Verify success via the follow-up
      OBSERVE routes (steps 7-13 below), never via this POST's own status code.
  (b) **Fallback, if (a) proves awkward**: write a small, standalone,
      temporary Tier-1/integration test (or a short-lived scratch `.cpp`
      compiled as a one-off tool) that constructs the real engine objects
      and calls `UnloadProjectAssembly()` directly in-process, then use
      `gte_send_request` against a SEPARATELY, SIMULTANEOUSLY running
      `GreatTamanaEditor.exe` only for the BEFORE/AFTER observation steps
      (this loses the "single running instance" elegance of option (a) but
      still satisfies "verify live via the 5 OBSERVE routes").

Whichever option is used, immediately after the unload call returns:

**Post-unload verification:**
7. `GET /project_assembly/debug/loaded_assemblies` → confirm the list no
   longer contains `ProjectAssemblyProbe_Game.dll`/`_Editor.dll` (empty, if
   these were the only two loaded Project Assemblies in this environment —
   confirm via 3.4 step 1's own baseline whether any OTHER project might
   also be present; if so, confirm only the Probe entries are gone).
8. `GET /project_assembly/debug/ledger?name=ProjectAssemblyProbe` → confirm
   ALL THREE lists are now empty again.
9. `GET /project_assembly/debug/component_types` → confirm
   `"ProbeHotReloadMarker"` is GONE (only the 5 built-ins remain).
10. `GET /list_tabs` → confirm `"Probe Panel"` no longer appears (or, if
    `m_allNames` cleanup (PHASE2) is somehow bypassed by whichever unload
    path was actually taken, investigate why — PHASE2's own fix should make
    this unconditionally true).
11. `GET /render_graph` → confirm `"ProjectAssemblyProbe.FillTexture"` no
    longer appears.
12. `GET /get_swapchain` → confirm the Editor still renders normally (no
    crash, no corrupted frame, no missing Game/Scene view) — the render
    graph correctly no longer declares the removed pass, but everything
    else keeps working.
13. **Wait several real seconds (poll `GET /get_logs` a few times across
    that span, confirming new, ordinary per-frame-unrelated log activity
    keeps appearing, e.g. any periodic engine log line, or simply confirm
    repeated `GET /get_swapchain` calls keep succeeding) — confirm the
    process has NOT crashed and continues operating completely normally.**
    This is the single most important check in this whole campaign — Hazard
    2's own Definition of Done (BIG-STEP 2's external spec) explicitly
    calls out "a crash here means this fix is incomplete, not merely
    imperfect."

If option (a) was used, revert the placeholder now (see its own instruction
above) and re-run a quick compile-check to confirm the revert is clean.

`stop_app_background` once all checks pass.

### 3.5 — If anything in 3.4 fails

Diagnose using `GET /get_logs` (never guess, never add raw console
logging). This phase itself must NOT call `delegate_task` under any
circumstance, per this whole session's own top-level workflow rule — if the
root cause is a genuine defect in PHASE1-4's own work, fix it DIRECTLY,
within this same session (the broken code is almost certainly still fresh
in context from reading PHASE1-4's own files/reports at the start of this
phase). Cite the exact failing check number from 3.4, the exact
expected-vs-actual response body, and the exact log lines observed, in this
phase's own completion report (3.7 below) as a found-and-fixed regression.
Only escalate to a human via `ask_questions` if the failure turns out to be
a genuine design ambiguity, not a plain code defect.

### 3.6 — Full regression pass

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```

Compare against `editor-core-separation-12`'s own documented baseline
(**1903 tests, 100% passing, 2 legitimate environment-gated skips** — cite
this campaign's own final numbers once known, do not just repeat this
number blindly if `AGENTS.md` shows a more recent baseline from some other
campaign that ran in between). Any newly-failing test (not one of the two
pre-existing, environment-gated skips) is a real regression this campaign
introduced — diagnose and fix it DIRECTLY, within this same session (per
3.5's own rule: this phase must NOT call `delegate_task`), before declaring
this campaign done.

### 3.7 — Campaign completion report

Write `CAMPAIGN_COMPLETION_REPORT.md` in this same
`task_manager/editor-core-separation-13/` folder, mirroring
`editor-core-separation-12/CAMPAIGN_COMPLETION_REPORT.md`'s own structure:
what was actually built (per phase), the two deliberate deviations from the
external plan found and resolved by this campaign (the `m_allNames`
collision-guard fix, PHASE2; whichever option (a)/(b) was chosen for 3.4's
own test mechanism, and confirmation option (a)'s placeholder was correctly
reverted if used), the final `ctest` count, and an explicit, honest
restatement that **BIG-STEP 3 (synchronous compile/atomic swap orchestrator)
and BIG-STEP 4 (state snapshot/restore) remain FULLY UNIMPLEMENTED** —
`POST /project_assembly/hot_reload` still answers a permanent `501`; no
compile-triggered reload of any kind exists yet; this campaign only proved
that an ALREADY-LOADED Project Assembly can be safely, cleanly UNLOADED,
on command, in isolation.

Also append a new subsection to `AGENTS.md`'s "Project Assembly System" /
"Project Assembly Hot Reload" area (immediately after the existing BIG-STEP
1 subsection, lines 916-961-ish — confirm the real current line range before
inserting), documenting: `ComponentTypeRegistry::UnregisterDescriptor()`,
`EditorPanelRegistry::UnregisterPluginPanel()` (and the `m_allNames` fix),
`ProjectAssemblyRegistrationLedger`, `ProjectAssemblyHost::
UnloadProjectAssembly()`/`GetLoadedAssemblyFileNames()`,
`BackupProjectAssemblyBinaries()`/`RestoreProjectAssemblyBinariesFromBackup()`,
and the new `ProbeHotReloadMarker` fixture component — mirroring how every
other campaign in that file documents its own shipped surface, so BIG-STEP 3
can find this work without re-reading every phase file here from scratch.

### 3.8 — Git commit

`git_add`/`git_commit` this phase's changes (including the probe project
extension, `AGENTS.md` update, and `CAMPAIGN_COMPLETION_REPORT.md`) as the
final commit of this campaign.

## Definition of Done — this phase, and this whole campaign

- [ ] `Projects/ProjectAssemblyProbe/` registers a real, throwaway custom ECS
      component type (`ProbeHotReloadMarker`).
- [ ] All 13 checks in Step 3.4 pass against a real running engine,
      including the critical "process survives several seconds afterward"
      check (#13).
- [ ] If test option (a) was used, the `TriggerHotReload()`/`POST
      /project_assembly/hot_reload` placeholder is confirmed byte-for-byte
      reverted to its permanent `501` contract afterward.
- [ ] Full build succeeds; full `ctest` shows zero new regressions.
- [ ] `CAMPAIGN_COMPLETION_REPORT.md` exists in this folder.
- [ ] `AGENTS.md` has a new, accurate entry for this campaign's shipped
      surface, explicitly stating BIG-STEP 3/4 remain unimplemented.
- [ ] Every checkbox in every one of PHASE1-4's own Definition of Done is
      confirmed still true (a quick final re-check, not a re-implementation).
