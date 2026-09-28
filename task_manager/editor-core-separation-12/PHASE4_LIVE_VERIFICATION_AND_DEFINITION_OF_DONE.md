# editor-core-separation-12 — PHASE4: Live Verification & Definition of Done

Parent: `PHASE0_MASTER_STRATEGY.md` — read it first.
Prior phase report: read PHASE3's own completion report before starting.

Depends on: PHASE1, PHASE2, PHASE3 (needs a fully-linking, feature-complete
`GreatTamanaEditor.exe`).
Blocks: nothing (final phase of this campaign).

---

## Step 1: The Goal

Prove, live, against a real running `GreatTamanaEditor.exe`, that every one
of the 7 new routes behaves exactly as PHASE0's Definition of Done
describes — real data where real data was promised, honest empty
placeholders where a placeholder was promised, a real compiler invocation
for the trigger route, and a stable `501` for the not-yet-wired route. This
is the ONE phase in this whole campaign allowed to run a full build and a
full `ctest` regression pass, per this campaign's own workflow rule
(previous phases used incremental/targeted compiles only).

## Step 2: The Situation

`Projects/ProjectAssemblyProbe/` is this engine's own permanent Project
Assembly smoke-test fixture (confirmed, `AGENTS.md`, "Project Assembly
System" section) — it already registers a custom Editor panel and a custom
render-graph pass, and (per that same section) is the correct, existing
target for `POST /project_assembly/debug/compile_only` — no new test
project needs to be created for this phase.

`gte_send_request` is this environment's tool for talking to a running
engine's embedded HTTP server (default `http://127.0.0.1:8080`) —
`run_app_background` starts `GreatTamanaEditor.exe` without blocking this
session, `stop_app_background` cleanly terminates it afterward.

## Step 3: The Plan

### 3.1 — Full build

```
cmake --build build
```
(Working directory: the repository root.) Fix any compile/link error found
here before proceeding — do not attempt live verification against a build
that didn't actually succeed.

### 3.2 — Launch and verify each route live

Use `run_app_background` to start the built `GreatTamanaEditor.exe`
(confirm its exact output path from the build log — typically under
`build/` — do not guess a path that wasn't actually produced).

Wait a few seconds for `NetworkServer::Start()` to bind, then, via
`gte_send_request`:

1. `GET /project_assembly/hot_reload/status` → expect `200`,
   `{"phase":"Idle","project_name":"","cycle_id":0,...}`.
2. `GET /project_assembly/debug/ledger?name=ProjectAssemblyProbe` → expect
   `200`, all three lists empty (`[]`) — the honest PHASE2 placeholder.
3. `GET /project_assembly/debug/ledger` (no `name` param) → expect `400`
   with the "'name' query parameter is required" message — confirms
   `ParseProjectNameQuery()`'s validation path.
4. `GET /project_assembly/debug/loaded_assemblies` → expect `200`,
   `{"dll_file_names":[]}` — the honest PHASE2 placeholder (real data is a
   future BIG-STEP 2 campaign's job).
5. `GET /project_assembly/debug/component_types` → expect `200` with a
   REAL, non-empty list including at least the engine's own built-in types
   (`"Transform"`, `"Name"`, `"Camera"`, `"DirectionalLight"`,
   `"PrimitiveSource"`, confirmed real registered types per
   `tests/ECS/Reflection/BuiltinComponentReflectionTests.cpp`) — if this
   list is empty, something is genuinely wrong (this is NOT a placeholder
   route) — investigate before proceeding, do not treat an empty result
   here as acceptable.
6. `GET /project_assembly/debug/scene_snapshot` → expect `200` with a real
   JSON `SceneDocument` body (an `"entities"` array, or whatever
   `SerializeSceneDocument()`'s real top-level shape is — confirm against
   `src/Scene/SceneJsonFormat.h`/`.cpp` if the shape looks unexpected)
   describing the engine's default startup scene (at minimum, a default
   Camera). This is NOT a placeholder route either.
7. `POST /project_assembly/debug/compile_only?name=ProjectAssemblyProbe` →
   expect `200`, `{"started":true}`. Immediately after, poll
   `GET /get_logs?category=ProjectAssemblyBuild` a few times over the next
   30-60 seconds and confirm real `cmake --build`/compiler output actually
   streams in, ending with the existing
   `"Build finished with exit code ..."` log line
   (`ProjectAssemblyBuildRunner.cpp`'s own existing, unmodified log
   message).
8. Immediately re-issue the SAME `POST /project_assembly/debug/compile_only?name=ProjectAssemblyProbe`
   WHILE the first build from step 7 is still running → expect `200`,
   `{"started":false,"reason":"a build for this project is already in progress"}`
   — confirms `TriggerCompileOnly()`'s in-flight-guard path.
9. `POST /project_assembly/hot_reload?name=ProjectAssemblyProbe` → expect
   `501` with the "hot reload orchestrator not yet wired - see BIG-STEP 3"
   message.
10. Take a screenshot via `GET /get_swapchain` (through `gte_send_request`)
    before and after step 7's compile finishes, and visually confirm the
    Game View still renders normally throughout — `compile_only` must never
    visibly disturb the currently-running engine (per this feature's own
    Section 2(f) guarantee).

Use `stop_app_background` to terminate the engine process once all 10
checks above pass.

### 3.3 — If anything in 3.2 fails

Diagnose using `GET /get_logs` (never guess) — this engine's own internal
logging IS the debugging tool here, per this whole session's own standing
instruction to never use raw C/C++ console logging. If the root cause is a
genuine code defect in PHASE1/2/3's own work, use `delegate_task` to send a
precise, targeted fix request back — cite the exact failing route, the
exact expected-vs-actual response body, and the exact log lines observed —
rather than attempting a large, unscoped fix inline in this phase.

### 3.4 — Full regression pass

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```

Compare the total test count and pass rate against the pre-campaign
baseline (check `AGENTS.md`'s own most recent campaign entry for the
last-recorded total, e.g. "1882 tests..." — cite whatever the actual most
recent number in that file is at the time this phase runs, do not hardcode
a number from this document). Any NEWLY failing test (not a pre-existing,
already-documented environment-gated skip) is a real regression this
campaign introduced — diagnose and fix via `delegate_task` before declaring
this campaign done, per this workflow's own rule ("if a test fails,
diagnose the issue and use delegate_task to fix whatever is broken").

### 3.5 — Campaign completion report

Once every check in 3.2 passes and 3.4 shows zero new regressions, write
`CAMPAIGN_COMPLETION_REPORT.md` in this same
`task_manager/editor-core-separation-12/` folder — mirroring
`editor-core-separation-11/CAMPAIGN_COMPLETION_REPORT.md`'s own structure:
what was actually built, the 3 corrections from `PHASE0_MASTER_STRATEGY.md`
Section 3.1 restated as shipped fact (not "planned"), the final `ctest`
count, and an explicit statement that BIG-STEP 2/3/4 remain fully
unimplemented, future work (do not let a future reader assume this
campaign did more than BIG-STEP 1).

Also append a short new entry to `AGENTS.md`'s "Project Assembly System"
section (or a small new subsection right after it) documenting the 7 new
routes and the `IHotReloadDebugCapability` interface, mirroring how every
other campaign in that file documents its own shipped surface — this is
what makes the NEXT campaign (BIG-STEP 2) able to find this work without
re-reading every phase file in this folder from scratch.

## Definition of Done — this phase, and this whole campaign

- [ ] All 10 checks in Step 3.2 pass against a real running engine.
- [ ] `ctest` regression pass shows zero new failures.
- [ ] `CAMPAIGN_COMPLETION_REPORT.md` exists in this folder.
- [ ] `AGENTS.md` has a new, accurate entry for this campaign's shipped
      surface.
- [ ] Every checkbox in `PHASE0_MASTER_STRATEGY.md`'s own Section 3.5
      Definition of Done is checked off.
