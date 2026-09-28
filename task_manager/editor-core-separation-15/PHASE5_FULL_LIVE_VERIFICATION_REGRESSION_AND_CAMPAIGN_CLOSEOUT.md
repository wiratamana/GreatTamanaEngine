# PHASE5 — Full Live Verification, Regression, Documentation Fix, and Campaign Closeout

Parent: `PHASE0_MASTER_STRATEGY.md`. Read `PHASE1` through `PHASE4`'s own
completion reports first — confirm every one of their own Definition of
Done checkboxes is still true before starting this phase (re-verify by
direct source reading, not by trusting the reports alone, per this whole
effort's own established discipline).

Depends on: PHASE1, PHASE2, PHASE3, PHASE4 (all fully done).
Blocks: nothing — this is the final phase of the final campaign of the
whole 4-BIG-STEP Hot Reload effort.

End state of this phase: the full success-path and rollback-path live
tests (mirroring `HOTRELOAD_BIGSTEP_04...txt`'s own Steps 3/4) both pass,
proving genuine ECS state persistence AND genuine new-code liveness in the
same breath, for both possible outcomes of a cycle; the ONE full build +
full `ctest` regression pass this whole campaign is permitted to run
succeeds with zero new regressions; `docs/conventions/
project-assembly-system.md`'s own stale `LDD4` is corrected; this
campaign's and the WHOLE 4-BIG-STEP effort's own completion reports are
written.

---

## STEP 1 — The success-path test (state survives AND new code is live,
together)

1. Launch `GreatTamanaEditor.exe` (`run_app_background`).
2. Baseline: `GET /project_assembly/debug/loaded_assemblies` (both probe
   `.dll`s present), `GET /project_assembly/debug/scene_snapshot`
   (`"ProbeHotReloadMarkerEntity"` / `value: 7`), `GET /render_graph`
   (`"ProjectAssemblyProbe.FillTexture"` present), `GET
   /get_texture?texture_name=ProjectAssemblyProbe.Output` (solid orange —
   the documented, permanent baseline appearance per
   `editor-core-separation-14`'s own PHASE5).
3. `POST /project_assembly/debug/set_probe_marker_value?value=42` —
   confirm `{"success": true}`. This is the "genuinely runtime-mutated
   state" this whole test hinges on (per `HOTRELOAD_BIGSTEP_04...txt`'s
   own Step 3: *"proves persistence... not merely whatever the source
   code would produce fresh"*).
4. Make a REAL, meaningful source change: `ProbeCompute.comp`'s fill color,
   orange -> blue (mirrors `editor-core-separation-14`'s own PHASE4/PHASE5
   precedent verbatim — same file, same throwaway-then-reverted change).
5. Trigger `POST /project_assembly/hot_reload?name=ProjectAssemblyProbe`.
   Poll `GET /project_assembly/hot_reload/status` from a second connection
   while the first is in flight — confirm `phase` genuinely advances
   (`CapturingState` -> `BackingUpBinaries` -> `Unloading` -> `Compiling`
   -> `ReloadingNewCode` -> `RestoringState` -> `Idle`), ending
   `lastOutcome: "Success"`.
6. **The combined proof**: `GET /project_assembly/debug/scene_snapshot` —
   confirm `"ProbeHotReloadMarkerEntity"` / `value: 42` (NOT `7` — proves
   HOOK POINT A/B's real bodies, PHASE2/3, genuinely preserve
   runtime-mutated custom-component state across a REAL reload, not just
   PHASE2/3's own narrower isolation tests). `GET
   /project_assembly/debug/component_types` — confirm
   `"ProbeHotReloadMarker"` appears EXACTLY ONCE (Hazard 1's fix,
   re-confirmed under real reload conditions, not just
   `editor-core-separation-13`'s own isolation test). `GET
   /get_texture?texture_name=ProjectAssemblyProbe.Output` — confirm solid
   BLUE (the new render feature is genuinely live). All three checks pass
   from the SAME single reload cycle.
7. Revert `ProbeCompute.comp` back to orange. Trigger one more hot reload
   (no marker-value change needed this time) to leave the fixture at its
   documented baseline appearance. **Watch for the pre-existing,
   already-documented shader-staging gap** (`editor-core-separation-14`'s
   own PHASE4/PHASE5 completion reports — a shader-only source change does
   not always get picked up by the build system's own `POST_BUILD` staging
   step) — if hit, work around it EXACTLY as those reports already did
   (manually copy the freshly-compiled `.spv` into place, never edit any
   shipped CMake file), and note it was re-encountered, not newly caused,
   in this phase's own completion report.
8. Confirm the marker value is back to `7` (`POST
   .../set_probe_marker_value?value=7`) before proceeding to Step 2.

## STEP 2 — The rollback-path test (state survives AND the OLD code stays
live, together — LDD-HR3, completed for real)

1. `POST /project_assembly/debug/set_probe_marker_value?value=99` —
   confirm `{"success": true}`.
2. Introduce a deliberate, temporary compile error into
   `HelloGame.cpp` (mirrors `editor-core-separation-14`'s own PHASE5
   precedent — a single invalid statement as the first line of
   `RegisterProbeGame()`). Confirm it is real via `POST
   /project_assembly/debug/compile_only?name=ProjectAssemblyProbe` +
   `GET /get_logs` BEFORE attempting the full cycle.
3. Trigger `POST /project_assembly/hot_reload?name=ProjectAssemblyProbe`.
   Confirm, via `GET /project_assembly/hot_reload/status`,
   `lastOutcome: "RolledBack"`.
4. **The combined proof**: `GET /project_assembly/debug/scene_snapshot` —
   confirm `"ProbeHotReloadMarkerEntity"` / `value: 99` is STILL present
   (NOT reset to `7`, NOT lost — HOOK POINT A captured it ONCE, before
   ANY teardown began, and HOOK POINT B restores that SAME captured
   snapshot regardless of which branch — success or rollback — actually
   ran). `GET /render_graph`/`GET /list_tabs` — confirm the OLD pass/panel
   are present and functioning. `GET
   /get_texture?texture_name=ProjectAssemblyProbe.Output` — confirm the
   texture is unchanged (whatever it was before this test started —
   nothing about the render feature was ever actually recompiled in).
5. Confirm the process did not crash across a multi-second span of
   continued, normal operation afterward.
6. Revert the deliberate compile error — confirm, via a full file re-read,
   byte-for-byte identical to its pre-test content.
7. Reset the marker value back to `7` before proceeding.

## STEP 3 — Full build + full `ctest` regression pass (the ONE gate this
whole campaign is permitted to run)

```
cmake --build build
```
Confirm success, zero new warnings from any file this campaign touched
(`Core.h/.cpp`, `ProjectAssemblyHotReload.h/.cpp`, `SceneBuilder.h/.cpp`,
`SceneIO.cpp`, `EditorHost.cpp`, `EditorCapabilities.h`,
`EditorHotReloadDebugCapability.h/.cpp`, `EngineCommandBridge.h`,
`EngineCommandResults.h`, `EngineCommandDispatch.cpp`,
`NetworkRoutes.h/.cpp`, `NetworkServer.cpp`, `HelloGame.cpp`).

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```
Compare against `editor-core-separation-14`'s own documented baseline
(1934 tests, 100% passing, 7 legitimate, environment-gated skips). Any NEW
failure must be diagnosed and fixed via `delegate_task` (per this whole
campaign's own top-level instructions — an implementation task fixing a
regression must NOT itself further delegate) before this phase can be
considered done. Pay particular attention to any test under `tests/
Editor/` or `tests/Scene/` covering `SceneIO`/`SceneBuilder` — PHASE3's
own refactor is the single highest-risk change in this whole campaign for
a latent regression to hide in.

## STEP 4 — Fix the stale documentation (LDD-HR9)

`docs/conventions/project-assembly-system.md` — confirmed, current, THREE
places (lines 63, 156, 307) state **"No hot reload, anywhere, ever"** as a
still-standing decision. Do not delete the historical record of why this
was once true — instead, at each of the three locations, add a clearly-
dated correction, e.g.:

```
- ~~No hot reload, anywhere, ever.~~ **SUPERSEDED, 2026-09-28 onward** —
  see `task_manager/editor-core-separation-12/` through `-15/` (the
  "Project Assembly Hot Reload" 4-campaign effort) and
  `HOTRELOAD_BIGSTEP_00_MASTER_INVESTIGATION_AND_ORCHESTRATION_2026-09-28.txt`.
  A Project Assembly's own `<Name>_Game.dll`/`_Editor.dll` pair CAN now be
  hot-reloaded, in place, via `POST /project_assembly/hot_reload?name=<X>`
  — see `docs/conventions/project-assembly-system.md`'s own new "Hot
  Reload" section below for the full, honest, current picture.
```

Then add ONE new, permanent section to this same file (near its own
existing structure — after whatever section currently documents the
Project Assembly system's own registration/load lifecycle), titled
`## Hot Reload`, stating PLAINLY:

- What it does (freeze -> backup -> unload -> recompile -> reload-or-
  rollback -> unfreeze, one project at a time, `gte_core`/`gte_editor`
  themselves never reload).
- **The honest boundary, stated as directly as
  `HOTRELOAD_BIGSTEP_00...txt`'s own Section 4.1 states it**: everything
  living in the ECS `Registry` (every entity, every built-in AND custom
  reflected component) survives, faithfully; anything a Project
  Assembly's own code keeps OUTSIDE the ECS Registry (a bare C++ global,
  a non-ECS manager object, GPU resources a render pass owns opaquely)
  does NOT survive — it is destroyed and rebuilt from scratch, exactly
  like a fresh process start, every single reload. Cite the SAME concrete,
  already-existing example `HOTRELOAD_BIGSTEP_04...txt`'s own Step 5
  cites: `ProbeEditorPanel::m_clickCount`, a plain `int` that resets to
  `0` on every reload, by construction.
- The two remaining, explicitly-out-of-scope limitations (LDD-HR7/HR8,
  `PHASE0_MASTER_STRATEGY.md` this campaign): the engine's own persistent
  `AssetDatabase` is not yet unified across every subsystem that owns one;
  a reload briefly clears and restores the ENTIRE live world, so every
  entity's numeric ID changes, for every currently-loaded project, not
  just the one being reloaded (documented as a known, currently-accepted
  limitation for the "more than one Project Assembly loaded at once"
  case).
- A one-line pointer to `docs/conventions/scene-serialization.md`,
  since `ReconstructSceneFromDocument()` (PHASE3) is now a second, real,
  production caller of that whole system's own core reconstruction logic,
  worth cross-referencing from there too (add a short, symmetrical note in
  `scene-serialization.md` itself, pointing back here).

## STEP 5 — Campaign closeout

Write `PHASE5_COMPLETION_REPORT.md` (this phase's own detailed writeup —
every live check performed, its result, any real deviation found) and
`CAMPAIGN_COMPLETION_REPORT.md` (mirrors `editor-core-separation-14`'s own
exact structure: what was built per phase, deviations found and resolved,
the full build/`ctest` result, the full live-verification checklist
result, and a final, honest "what BIG-STEP 4 — and therefore the WHOLE
4-BIG-STEP effort — actually delivers, and its exact, permanent remaining
limits" section, mirroring `editor-core-separation-14`'s own "Honest
restatement" section but flipped to now say the effort IS complete,
modulo the honest boundary Step 4 above just wrote down permanently).

Update `agents.md`'s own existing Hot Reload entry (confirmed present,
referencing `editor-core-separation-14`, lines ~1054-1130 — re-read the
real, current text before editing) to describe the now-COMPLETE, 4-
campaign effort, explicitly stating BIG-STEP 4 is now DONE (superseding
that entry's own prior "BIG-STEP 4 remains fully unimplemented" statement,
which was correct at the time it was written and must now be corrected,
not merely left to silently contradict this newer, real state).

---

## Definition of Done — this phase, and this whole campaign, and the
WHOLE 4-BIG-STEP effort

- [ ] Step 1's full success-path test passes: a runtime-mutated custom ECS
      component value AND a genuinely new/changed render feature are BOTH
      confirmed live, correctly, immediately after one real "Compile &
      Reload" cycle.
- [ ] Step 2's full rollback-path test passes: the same mutated component
      value AND the OLD, unchanged render feature are BOTH confirmed
      intact after a deliberately-broken compile, with a clear,
      correctly-streamed build-error explanation and no crash at any
      point.
- [ ] The full existing regression suite (`ctest`) passes, zero new
      failures versus `editor-core-separation-14`'s own documented
      baseline.
- [ ] `docs/conventions/project-assembly-system.md`'s own stale `LDD4` is
      corrected at all three locations, and a new, permanent, honest `##
      Hot Reload` section exists in that same file.
- [ ] `PHASE5_COMPLETION_REPORT.md` and `CAMPAIGN_COMPLETION_REPORT.md`
      both exist in this folder; `agents.md`'s own entry is updated to
      reflect the now-complete state.
- [ ] Every checkbox in every one of PHASE1-4's own Definition of Done is
      re-confirmed still true.

**Once this phase is done, the entire, 4-BIG-STEP "Project Assembly Hot
Reload" effort (`editor-core-separation-12` through `-15`) is DONE.**

## What this phase does NOT do

- Does NOT implement BIG-STEP 4's own named future option (b) (opt-in
  non-ECS state serialize/restore export hooks) — named, in the new docs
  section, as real future work, not built here.
- Does NOT attempt to make any of this survive a full process close/
  relaunch differently than before — unchanged, permanent non-goal of the
  whole effort.
- Does NOT run any build/test pass beyond the ONE full pass in Step 3 —
  every earlier phase's own verification was, correctly, incremental/live
  only, per this whole campaign's own top-level instructions.
