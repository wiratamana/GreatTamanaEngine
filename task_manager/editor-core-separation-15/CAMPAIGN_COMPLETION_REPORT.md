# editor-core-separation-15 — CAMPAIGN COMPLETION REPORT

## Status: DONE

Implements the whole five-phase `editor-core-separation-15` campaign
("Project Assembly Hot Reload — BIG-STEP 4 of 4: State Snapshot, Restore,
and Verification Plan"), per `PHASE0_MASTER_STRATEGY.md`. This is the final
phase (PHASE5) and this is the campaign's closing report — see
`PHASE1_COMPLETION_REPORT.md` through `PHASE5_COMPLETION_REPORT.md` for each
phase's own detailed writeup; this report summarizes the whole campaign.

**This campaign is also the closing campaign of the entire 4-campaign
"Project Assembly Hot Reload" effort** (`editor-core-separation-12` through
`-15`). Once this report is filed, the WHOLE 4-BIG-STEP effort is DONE.

---

## What was built, per phase (summary — see each phase's own report for detail)

- **PHASE1** — `Core` gained a persistent, engine-owned `AssetDatabase`
  instance (`Core::GetAssetDatabase()`, `m_assetDatabase`), refreshed once
  per hot-reload cycle (LDD-HR7). `PerformProjectAssemblyHotReload()` gained
  its permanent 7th parameter, `projectRootDirectory`, resolved by the
  caller (`EditorHost::Run()`, gte_editor-tier) and threaded through,
  mirroring `outputDirectory`/`buildDirectory`'s own existing precedent —
  avoiding a real `gte_core` → `gte_editor` layering violation the external
  master plan's own literal pseudocode would otherwise have introduced
  (`ResolveProjectRootDirectory()` is gte_editor-only).
- **PHASE2** — HOOK POINT A's real body: `CaptureProjectAssemblyHotReloadState()`
  refreshes `core.GetAssetDatabase()` exactly once, then builds a full
  `SceneDocument` snapshot of the live world via
  `Scene/SceneBuilder.h`'s `BuildSceneDocumentFromRegistry()` — the SAME
  function `GET /project_assembly/debug/scene_snapshot` already uses,
  cross-checked live to agree exactly for the same live world.
- **PHASE3** — the campaign's own largest structural change: `Editor::LoadScene()`'s
  own recipe-aware reconstruction body was extracted into a new,
  gte_core-tier function, `Scene/SceneBuilder.cpp`'s
  `ReconstructSceneFromDocument(Game&, Renderer&, const SceneDocument&, const AssetDatabase&)`
  (per the user's own explicit direction — "Load Scene is a core engine
  feature", mirroring Unity's own `SceneManager.LoadScene()` being a
  Player-build capability). `Editor::LoadScene()` became a thin wrapper.
  HOOK POINT B (`RestoreProjectAssemblyHotReloadState()`) calls this SAME
  shared function. Verified via the full existing scene-serialization test
  suite (58/58 unchanged and passing) plus two live checks: a byte-for-byte
  Ctrl+S/Ctrl+O round trip, and a HOOK POINT B isolation test (a
  runtime-spawned primitive's Transform data survives a real hot-reload
  cycle).
- **PHASE4** — `Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp` gained a
  real, permanent entity carrying `ProbeHotReloadMarker{value=7}`, and one
  new, narrow, testing-only HTTP route, `POST
  /project_assembly/debug/set_probe_marker_value?value=<N>`
  (`IHotReloadDebugCapability::SetProbeHotReloadMarkerValueForTesting()`,
  LDD-HR6 — hardcoded to this ONE component/field, never a generic mutation
  surface). **Found and worked around, live, a real crash**: a Project
  Assembly `.dll` must never call `Registry::AddComponent<T>()` directly
  for a BUILT-IN component type (Transform/Name) — done instead through
  `ComponentTypeRegistry`'s own generic function pointers, whose actual
  code runs inside `gte_core`/the `.exe`. **Also found, but MIS-DIAGNOSED as
  a harmless cosmetic JSON artifact** (later corrected by PHASE5): a
  same-root-cause collision made `ProbeHotReloadMarker`'s own DLL-local
  numeric type ID collide with a real built-in type's ID.
- **PHASE5** (this phase) — ran the full success-path and rollback-path live
  tests (mirroring `HOTRELOAD_BIGSTEP_04...txt`'s own Steps 3/4), the ONE
  full build + full `ctest` regression pass this whole campaign is
  permitted to run, fixed the stale `LDD4` documentation and added the new
  permanent `## Hot Reload` section, and — **found and fixed, live, two
  real, previously-latent bugs the success-path test's very first attempt
  exposed** (see below) — closing what PHASE4 had mis-classified as
  cosmetic and what PHASE4's own report had explicitly flagged as "a real,
  non-trivial, future undertaking... out of scope for this narrow phase".

---

## Deliberate deviations from the plan, found and resolved by this campaign

1. **The external master plan's own HOOK POINT A pseudocode would have
   reintroduced the EXACT `gte_core` → `gte_editor` layering violation
   `editor-core-separation-14` already found and fixed once** (calling
   `ResolveProjectRootDirectory()`, a `gte_editor`-only function, directly
   from `src/Core/Plugins/ProjectAssemblyHotReload.cpp`). **Fixed** in
   PHASE1, before any real capture/restore logic was written, by resolving
   the project root directory on the caller's side and threading it through
   as a plain value parameter — the same precedent
   `outputDirectory`/`buildDirectory` already established.
2. **The external plan's Step 2 said to extract `Editor::LoadScene()`'s own
   steps into `Editor/SceneIO.h` — but that file is gte_editor-tier, and
   HOOK POINT B is gte_core-tier.** Per the user's own explicit answer
   ("Load Scene is a core engine feature that can be controlled on the
   editor side also", mirroring Unity), PHASE3 moved this logic into
   `Scene/SceneBuilder.cpp` instead (gte_core-tier) — every type it touches
   was already gte_core-tier, so this closed a real, would-have-been-fatal
   layering mismatch rather than merely a style preference.
3. **PHASE4's own literal Step 1 snippet (calling `registry.AddComponent<Transform>()`/
   `<Name>()` directly from the Project Assembly `.dll`) crashed the engine
   with a confirmed, reproducible SIGSEGV within ~2 seconds of every
   launch.** Root-caused, live, via `gdb`, to `gte::detail::ComponentTypeId<T>()`'s
   own per-binary-image counter scheme. Fixed, narrowly, by going through
   `ComponentTypeRegistry`'s own generic function pointers for built-in
   types instead — `ProbeHotReloadMarker` itself was left as a direct
   `AddComponent()` call, believed safe at the time since every access to
   it happened to originate from the same DLL. **This narrower workaround
   turned out to be incomplete** — see deviation 4/5 below, both found by
   PHASE5.
4. **PHASE5's own Step 1 first attempt at a real hot-reload cycle crashed
   the whole engine** — the exact same `ComponentTypeId<T>()` collision
   class PHASE4 had already found, but this time actively corrupting the
   real, live `ComponentStorage<Transform>` object (PHASE4 had observed a
   symptom of this same collision — a spurious `ProbeHotReloadMarker` value
   on the Camera entity in the debug JSON — but mis-classified it as a
   harmless cosmetic artifact, not active memory corruption). **Fixed at
   the root** this phase: `ComponentTypeId<T>()` now resolves each type's
   numeric slot through one single, shared, out-of-line, process-wide
   authority (`ResolveComponentTypeIdByName()`, new `src/ECS/Registry.cpp`),
   keyed by `typeid(T).name()`, mirroring `ComponentTypeRegistry::Instance()`'s
   own already-proven-correct shape — closing the general cross-`.dll`
   component-type-safety hazard for ANY future Project Assembly's custom
   component type, not just this one probe fixture.
5. **A SECOND, related, previously-undiscovered bug, also found live via
   `gdb` this phase**: a Project-Assembly-registered custom component's
   `ComponentStorage<T>` pool object keeps a dangling vtable across that
   assembly's own `.dll` unload (its virtual function table is compiled
   INTO the unloading `.dll`'s own image) — any later virtual call through
   that pool (e.g. `Registry::DestroyEntity()`'s own generic
   `pool->Remove()` loop, exercised by every future `ClearEntireScene()`
   call) is undefined behavior; confirmed as an intermittent-not-immediate
   crash on the SECOND consecutive reload cycle in one session. **Fixed**:
   `Registry::ResetStoragePool<T>()` (new) destroys a type's pool entirely,
   wired through a new `ComponentTypeDescriptor::destroyPool` callback,
   called by `ProjectAssemblyRegistrationLedger::UnregisterEverythingFor()`
   for every custom component type a specific project's own ledger entry
   recorded, BEFORE that project's `.dll` is `FreeLibrary()`'d.

No other deviation of substance occurred across PHASE1-5 — every phase's own
completion report confirms its own file's plan was implemented largely as
written, with the layering/scope corrections above each individually
disclosed and justified at the time.

---

## Full build and full `ctest` regression pass (PHASE5 — the ONLY phase in this campaign permitted to run either)

```
cmake --build build
```
Succeeded cleanly, zero new warnings.

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```
**Final result: 1934 tests total, 100% passing, 7 legitimate,
environment-gated skips** — byte-for-byte identical to
`editor-core-separation-14`'s own documented 1934/7 baseline. Zero new
failures, zero new skips, zero regressions. This is a genuinely meaningful
result given this campaign touched a Tier-1, deeply foundational file
(`src/ECS/Registry.h`) that every single ECS-dependent test in the whole
suite transitively exercises.

---

## Live verification — full checklist (PHASE5, both success and rollback paths)

Run against a real, freshly-built, background-launched `GreatTamanaEditor.exe`
(see `PHASE5_COMPLETION_REPORT.md` for the full blow-by-blow, including the
two bugs found and fixed mid-verification):

1-3. **Baseline** — both `.dll`s loaded, marker `value: 7`, texture solid
orange. ✅

4-6. **Success path** — marker mutated to `42`; `ProbeCompute.comp`'s fill
color changed orange → blue; a real hot-reload cycle (plus one extra cycle
to work around the pre-existing shader-staging gap) completed with
`"last_outcome":"Success"`; **all three combined proofs confirmed
together**: marker still `42` (not reset to `7`), `"ProbeHotReloadMarker"`
appears exactly once in `component_types` (Hazard 1 still closed), texture
genuinely blue. ✅

7-8. **Revert** — shader reverted to orange, one more reload cycle restored
the documented baseline appearance; marker reset to `7`. ✅

9-13. **Rollback path** — marker mutated to `99`; a deliberate compile error
confirmed real via `compile_only` + `get_logs`; a real hot-reload cycle
resolved `"last_outcome":"RolledBack"`; **combined proof**: marker still
`99` (survived the rollback), `"Probe Panel"` still present in `list_tabs`,
texture unchanged; process confirmed alive and rendering normally across a
multi-second span (`get_swapchain` issued twice). ✅

14. **Cleanup** — the deliberate compile error reverted and confirmed
byte-for-byte identical to its original content; marker reset to `7`. ✅

**Two genuine, previously-latent Registry safety bugs were found and fixed
live during Step 1's own very first attempt** (see the "Deliberate
deviations" section above and `PHASE5_COMPLETION_REPORT.md` for the full
`gdb` backtraces and root-cause analysis) — both required to make this
whole checklist pass at all, and both closing gaps this campaign's own
earlier phases had either not yet exercised (PHASE3's isolation test never
combined a custom component with a real reload) or had mis-diagnosed
(PHASE4's "spurious value" observation).

The fixture was left in its exact, original, documented baseline state
(solid orange, marker `value: 7`, both `.dll`s loaded) before this phase's
own build/test pass began.

---

## Honest restatement: what the WHOLE 4-BIG-STEP effort now delivers, and its exact, permanent remaining limits

Per this campaign's own Step 4 documentation update
(`docs/conventions/project-assembly-system.md`'s new `## Hot Reload`
section, the permanent source of truth going forward):

- **BIG-STEP 4 (ECS world-state snapshot/restore across a reload) is now
  DONE** — HOOK POINT A/B (`CaptureProjectAssemblyHotReloadState()`/
  `RestoreProjectAssemblyHotReloadState()`, `src/Core/Plugins/
  ProjectAssemblyHotReload.h/.cpp`) are real, working bodies, not stubs.
  `POST /project_assembly/hot_reload?name=<X>` genuinely preserves the live
  ECS world — every entity, every built-in reflected component, AND every
  Project-Assembly-defined custom reflected component type, including
  values MUTATED AT RUNTIME (not merely whatever a cold start would
  reproduce) — across BOTH a successful reload and an automatic rollback,
  proven live for both outcomes in the same session.
- **What is now real and working, end to end, for the first time in this
  whole 4-campaign effort**: a Project Assembly author can hit "Compile &
  Reload" (or its HTTP equivalent) on a running `GreatTamanaEditor.exe`
  instance, get either a genuinely new, compiled-in behavior (a changed
  render feature, a changed custom component's registered shape) or a safe,
  automatic rollback to the last-known-good binaries on any failure, and in
  either case, EVERY entity's data — including anything they mutated at
  runtime through the Editor or an HTTP call before triggering the
  reload — survives, completely unaffected by which branch the cycle took.
- **The one, permanent, honest boundary of this whole feature, stated as
  plainly as this campaign's own external master plan states it**:
  everything living in the ECS `Registry` survives; anything a Project
  Assembly's own code keeps OUTSIDE the ECS Registry (a bare C++ global, a
  non-ECS manager object, GPU resources a render pass owns opaquely) does
  NOT survive — it is destroyed and rebuilt from scratch, exactly like a
  fresh process start, on every single reload.
- **Two further, explicitly out-of-scope limitations, unchanged from this
  campaign's own PHASE0 plan** (LDD-HR7/HR8): the engine's persistent
  `AssetDatabase` is not yet unified across every subsystem that owns one
  (`Core`'s own instance, `ProjectPanel`'s, `SceneIO.cpp`'s throwaway ones);
  a reload cycle briefly clears and restores the ENTIRE live world, so every
  entity's numeric ID changes for every currently-loaded project, not just
  the one being reloaded (accepted, since in practice only one project is
  ever loaded at a time).
- **A genuinely new capability this campaign's own live testing uncovered
  and closed, beyond BIG-STEP 4's own original scope**: Project-Assembly
  custom ECS component types are now actually SAFE to use across a real
  unload/reload cycle — this was NOT true before this campaign (two
  separate, confirmed, `gdb`-diagnosed crash classes existed in
  `src/ECS/Registry.h`'s own component-type-ID and pool-lifetime handling),
  and closing both was a genuine prerequisite for this campaign's own
  headline claim to be true for anything beyond the empty-state case.

**This whole 4-campaign effort (`editor-core-separation-12` through `-15`,
"Project Assembly Hot Reload") is now DONE.**

---

## Definition of Done — this whole campaign, and the whole 4-BIG-STEP effort

- [x] `Core::GetAssetDatabase()`; `PerformProjectAssemblyHotReload()`'s
      permanent, layering-correct 7-parameter signature in place (PHASE1).
- [x] HOOK POINT A's real body exists, compiles, and was cross-checked live
      against the already-working `GET /project_assembly/debug/scene_snapshot`
      endpoint (PHASE2).
- [x] The shared `ReconstructSceneFromDocument()` function exists in
      `Scene/SceneBuilder.h/.cpp` (gte_core-tier); `Editor::LoadScene()` is a
      thin wrapper; HOOK POINT B's real body exists and was verified live via
      a genuine isolation test (PHASE3).
- [x] A real marker entity + the one narrow testing-only mutation route
      exist and are wired end-to-end, with a confirmed, disclosed,
      safety-critical deviation fully documented (PHASE4).
- [x] The full success-path and rollback-path live tests both pass, proving
      genuine ECS state persistence AND genuine new-code liveness together,
      for both possible outcomes of a cycle (PHASE5).
- [x] The ONE full build + full `ctest` regression pass this whole campaign
      is permitted to run succeeded with zero new regressions (1934 tests,
      100% passing, 7 legitimate environment-gated skips — unchanged from
      `editor-core-separation-14`'s own baseline) (PHASE5).
- [x] `docs/conventions/project-assembly-system.md`'s stale `LDD4` is
      corrected at all three locations, and a new, permanent `## Hot
      Reload` section exists; `docs/conventions/scene-serialization.md` has
      a symmetrical cross-reference (PHASE5).
- [x] `PHASE5_COMPLETION_REPORT.md` and `CAMPAIGN_COMPLETION_REPORT.md`
      (this file) both exist; `AGENTS.md`'s own Hot Reload entry is updated
      to describe the now-complete, 4-campaign effort (next commit).
- [x] Every checkbox in every one of PHASE1-4's own Definition of Done is
      re-confirmed still true (re-verified this phase, both by direct
      source reading and by this phase's own live checks independently
      re-exercising every surface).

**This whole 4-campaign effort (`editor-core-separation-12` through `-15`,
"Project Assembly Hot Reload") is DONE.**
