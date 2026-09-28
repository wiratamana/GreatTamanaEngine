# PHASE5 COMPLETION REPORT — Full Live Verification, Regression, Documentation Fix, and Campaign Closeout

Campaign: `editor-core-separation-15` (Project Assembly Hot Reload plan, BIG-STEP 4).
Phase file: `PHASE5_FULL_LIVE_VERIFICATION_REGRESSION_AND_CAMPAIGN_CLOSEOUT.md`.

## Pre-flight: re-confirmation of PHASE1-4's own Definition of Done

Before starting, every one of PHASE1-4's own Definition of Done checkboxes was
re-confirmed true by DIRECT SOURCE READING (not by trusting the prior reports
alone), per this phase file's own explicit instruction:

- **PHASE1** — `Core::GetAssetDatabase()`/`m_assetDatabase` confirmed present
  in `Core.h`; `PerformProjectAssemblyHotReload()`'s 7-parameter signature
  (including `projectRootDirectory`) confirmed in both
  `ProjectAssemblyHotReload.h` and `.cpp`.
- **PHASE2** — `HotReloadStateSnapshot::document` confirmed a real
  `SceneDocument`; `CaptureProjectAssemblyHotReloadState()`'s real body
  (refresh `AssetDatabase` once, call `BuildSceneDocumentFromRegistry()`)
  confirmed in `ProjectAssemblyHotReload.cpp`.
- **PHASE3** — `ReconstructSceneFromDocument()` confirmed present in
  `Scene/SceneBuilder.h/.cpp` (gte_core-tier); `Editor::LoadScene()`
  confirmed to be a thin wrapper; `RestoreProjectAssemblyHotReloadState()`'s
  real body confirmed.
- **PHASE4** — `Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp` confirmed
  to spawn a real, permanent `ProbeHotReloadMarkerEntity` carrying
  `ProbeHotReloadMarker{value=7}`; `POST
  /project_assembly/debug/set_probe_marker_value` confirmed wired end-to-end.

All confirmed true. PHASE4's own report flagged two "New gaps found" this
phase needed to watch for — both resurfaced here, in a more serious form than
PHASE4 believed, and both are now fixed (see below).

---

## STEP 1 — Success-path live test

Full sequence executed against a real, background-launched
`GreatTamanaEditor.exe` (`run_app_background` → `gte_send_request` →
`stop_app_background`):

1. Baseline confirmed: `GET /project_assembly/debug/loaded_assemblies` (both
   probe `.dll`s present), `GET /project_assembly/debug/scene_snapshot`
   (`ProbeHotReloadMarkerEntity`/`value: 7`), `GET
   /get_texture?texture_name=ProjectAssemblyProbe.Output` (solid orange).
2. `POST /project_assembly/debug/set_probe_marker_value?value=42` →
   `{"success":true}`.
3. `ProbeCompute.comp`'s fill color changed orange → blue (mirrors
   `editor-core-separation-14`'s own precedent).
4. `POST /project_assembly/hot_reload?name=ProjectAssemblyProbe` →
   `"last_outcome":"Success"`.
5. **Hit the documented shader-staging gap immediately** (see "Pre-existing
   gaps re-encountered" below) — worked around exactly as
   `editor-core-separation-14` did (manually copied the freshly-compiled
   `build/shaders/ProbeCompute.comp.spv` over the stale
   `build/project_assemblies/shaders/ProbeCompute.comp.spv`), then triggered
   ONE MORE hot-reload cycle (no marker mutation needed) to force the
   already-loaded `ComputePipeline` to rebuild against the corrected `.spv`.
6. **Combined proof, confirmed together**: `GET
   /project_assembly/debug/scene_snapshot` — `value: 42` (survived, not
   reset to 7). `GET /project_assembly/debug/component_types` —
   `"ProbeHotReloadMarker"` appears exactly once (Hazard 1 still correctly
   closed). `GET /get_texture?texture_name=ProjectAssemblyProbe.Output` —
   solid BLUE.
7. `ProbeCompute.comp` reverted to orange; one more hot-reload cycle (hit the
   staging gap again, worked around identically) restored the solid-orange
   baseline appearance.
8. `POST .../set_probe_marker_value?value=7` — confirmed via a follow-up
   snapshot read.

**Honest deviation from the phase file's own literal Step 6 wording** ("All
three checks pass from the SAME single reload cycle"): because the
pre-existing shader-staging gap forced one extra "no-op" reload cycle to pick
up the corrected `.spv`, the three checks were confirmed true after TWO
cycles in a row (marker survives both; texture only turns blue after the
second, since the first cycle's own compute pipeline was still built against
the stale `.spv`). This exactly mirrors `editor-core-separation-14`'s own
PHASE5 experience with the same gap and is not a new defect.

## STEP 2 — Rollback-path live test

1. `POST /project_assembly/debug/set_probe_marker_value?value=99` →
   `{"success":true}`.
2. A deliberate compile error (`this is a deliberate compile error injected
   by editor-core-separation-15 PHASE5 Step 2;`) was inserted as the first
   line of `RegisterProbeGame()` in `HelloGame.cpp`.
3. Confirmed real via `POST /project_assembly/debug/compile_only` + `GET
   /get_logs?category=ProjectAssemblyBuild&keyword=error` — a genuine GCC
   parse error (`invalid use of 'this' in non-member function`).
4. `POST /project_assembly/hot_reload?name=ProjectAssemblyProbe` →
   `"last_outcome":"RolledBack"`.
5. **Combined proof**: `GET /project_assembly/debug/scene_snapshot` —
   `value: 99` (survived the rollback, not reset to 7). `GET /list_tabs` —
   `"Probe Panel"` still present. `GET
   /get_texture?texture_name=ProjectAssemblyProbe.Output` — unchanged
   (orange).
6. `GET /get_swapchain` issued twice, several seconds apart — process alive,
   rendering normally, Hierarchy panel showing both live entities.
7. The deliberate compile error was reverted; the resulting file was
   re-read in FULL and confirmed byte-for-byte identical to its pre-test
   content.
8. `POST .../set_probe_marker_value?value=7` — fixture back at documented
   baseline (marker=7, solid orange, both `.dll`s loaded) before Step 3.

## Two genuine, previously-latent bugs found and fixed live during this phase

Both were found DURING Step 1 (the very first time this whole campaign's own
hot-reload cycle was ever exercised against a probe fixture that both (a)
carries a custom component AND (b) has actually gone through a real
unload→reload cycle) — PHASE4's own "New gaps found" section had already
spotted symptom evidence of the first one but mis-classified it as a
harmless cosmetic JSON artifact. Both required a real code fix, made
directly in this phase per this campaign's own top-level instruction ("Any
NEW failure must be diagnosed and fixed by you directly — you must NOT
delegate this").

### Bug 1 — `gte::detail::ComponentTypeId<T>()` cross-image collision actively corrupted live engine memory

**Confirmed, live, via `gdb -batch -ex run -ex bt`**: the very first attempt
at Step 1's hot-reload cycle crashed the whole engine with a SIGSEGV inside
`ComponentStorage<Transform>::Has()` (an out-of-bounds `std::vector`
assertion failure), called from `GetChildren()`'s own sort comparator,
called from `ClearEntireScene()`, called from
`RestoreProjectAssemblyHotReloadState()` (HOOK POINT B) during the reload
cycle's own "RestoringState" phase.

**Root cause**: `detail::ComponentTypeId<T>()` (`src/ECS/Registry.h`) used to
assign each component type a numeric slot via a per-BINARY-IMAGE
`static std::size_t next = 0;` counter, function-local to an `inline`
header-only function — correct only when every caller of a given `T` is
compiled into the SAME image. `ProjectAssemblyProbe_Game.dll`'s own first
call touching `Registry::Storage<ProbeHotReloadMarker>()` (from
`registry.AddComponent<ProbeHotReloadMarker>()`, PHASE4) is the FIRST AND
ONLY template instantiation of `ComponentTypeId<T>()` compiled inside that
DLL's own image, so its local counter starts at 0 — the exact same numeric
slot `GreatTamanaEditor.exe`'s own counter had already assigned to the REAL,
built-in `Transform` component. Since `Registry::m_pools` is one single,
process-wide `std::vector`, this collision meant
`registry.AddComponent<ProbeHotReloadMarker>()` silently REINTERPRETED and
corrupted the real, live `ComponentStorage<Transform>` object — active
memory corruption from the very first moment the probe project ever loaded,
not merely a cosmetic display quirk as PHASE4 believed (PHASE4's own
observed "spurious `ProbeHotReloadMarker: {value:0}` on the Camera entity"
symptom was this exact same collision, seen from the opposite direction).

**Fix** (`src/ECS/Registry.h` + new `src/ECS/Registry.cpp`,
`ECS/Reflection/ComponentTypeDescriptor.h`/`ComponentTypeRegistry.inl`
untouched otherwise): `ComponentTypeId<T>()` now resolves its slot through a
single, shared, out-of-line, process-wide authority,
`detail::ResolveComponentTypeIdByName(typeid(T).name())`, defined in a new
ordinary (non-template, non-inline) `.cpp` file compiled ONLY into
`gte_core` — mirroring `ComponentTypeRegistry::Instance()`'s own
already-proven-correct "real singleton in a real `.cpp` file" shape (per
this same file's own header comment history). `typeid(T).name()` is used
purely as a stable per-type STRING KEY into a shared
`std::unordered_map<std::string, std::size_t>`, looked up exactly once per
`(T, image)` pair (cached in `ComponentTypeId<T>()`'s own local static
afterward, so the ordinary per-call hot path pays zero extra cost) — never
for RTTI-based dynamic dispatch. This guarantees the SAME C++ type resolves
to the SAME numeric slot regardless of which binary image (the `.exe` or any
Project Assembly `.dll`) asks first.

**Confirmed fixed, live**: re-ran the exact reproduction sequence after the
fix — `GET /project_assembly/debug/scene_snapshot` no longer shows the
spurious `ProbeHotReloadMarker` on the Camera entity (a direct side-effect
confirmation the collision is gone), and the crash no longer reproduces.

### Bug 2 — a Project-Assembly-registered custom component's pool object kept a dangling vtable across `.dll` unload

**Confirmed, live, via `gdb`**, on the SECOND consecutive hot-reload cycle in
one process session (the first cycle succeeded — non-deterministically, per
the root cause below): a real SIGSEGV inside `Registry::DestroyEntity()`,
called from `DestroyEntityAndDescendants()`, called from
`ClearEntireScene()`, called from `RestoreProjectAssemblyHotReloadState()`.

**Root cause**: `ComponentStorage<ProbeHotReloadMarker>`'s pool object
(`Registry::m_pools[slot]`, a `std::unique_ptr<IComponentPool>`) is
heap-allocated once, the first time the probe project ever calls
`registry.AddComponent<ProbeHotReloadMarker>()` — but `IComponentPool`'s own
virtual function table for that concrete type is compiled INTO the Project
Assembly `.dll`'s own image. Once `ProjectAssemblyHost::UnloadProjectAssembly()`
calls `FreeLibrary()` on that `.dll` (BIG-STEP 2's own teardown), the pool
object itself still physically sits in `Registry::m_pools` (an ordinary
`Registry` object has no idea a `.dll` unload just happened), but its vtable
pointer now dangles into unmapped memory. Any LATER virtual call through
that same `IComponentPool*` — e.g. `Registry::DestroyEntity()`'s own
`pool->Remove(entity)` loop over EVERY pool, exercised by ANY future
`ClearEntireScene()`/entity-destroy call, not merely a hot-reload one — is
undefined behavior. This explains why the FIRST cycle in a session
sometimes appeared to succeed: immediately after `FreeLibrary()` returns,
the OS had not yet necessarily reused that address range, so the stale
vtable pointer sometimes still happened to be readable; a SECOND unload
made the corruption reliably observable.

**Fix**: `Registry::ResetStoragePool<T>()` (new, `src/ECS/Registry.h`)
destroys (resets to `nullptr`) a specific type's pool entirely.
`ComponentTypeDescriptor` (`ECS/Reflection/ComponentTypeDescriptor.h`)
gained a new type-erased `destroyPool` callback, populated generically by
`RegisterComponentType<T>()` (`ComponentTypeRegistry.inl`).
`ProjectAssemblyRegistrationLedger::UnregisterEverythingFor()`
(`src/Core/Plugins/ProjectAssemblyRegistrationLedger.cpp`) now calls
`descriptor->destroyPool(core.GetGame().GetRegistry())` for every CUSTOM
component typeName that specific project's own ledger entry recorded,
immediately BEFORE removing its descriptor and BEFORE the caller's own
subsequent `FreeLibrary()` — so the next time the same (or a freshly
reloaded) `.dll` calls `Storage<T>()` again, it constructs a BRAND NEW pool
object with a valid, freshly-loaded vtable instead of reusing/reinterpreting
a stale one. **Never applied to a built-in component type** — built-in
registrations run outside any project's `BeginRecordingFor()`/
`EndRecording()` bracket, so their typeNames are never recorded in any
project's ledger entry to begin with; this fix is scoped exactly to the
hazard it closes.

**Confirmed fixed, live**: Step 1's own sequence above ran FOUR consecutive
hot-reload cycles against the same probe project in one process session
(mutate→reload→copy-shader→reload→revert→reload→copy-shader→reload) with
zero crashes; Step 2 added a fifth (the rollback cycle). All five completed
cleanly.

## Pre-existing gaps re-encountered (not newly caused)

- **The documented shader-staging gap** (`editor-core-separation-14`'s own
  PHASE4/PHASE5 completion reports, and `docs/conventions/
  project-assembly-system.md`'s own "Known CMake gap, not yet fixed"
  section) — a shader-only source change during a hot-reload's own
  incremental build does not reliably re-stage the compiled `.spv` next to
  the `.dll` (the `.spv` lands in `build/shaders/` but the STALE copy stays
  in `build/project_assemblies/shaders/`, since no `.cpp` changed to force
  a relink). Hit twice this phase (once per shader-color-flip direction),
  worked around identically both times: manually copied the freshly
  compiled `build/shaders/ProbeCompute.comp.spv` over the stale one, then
  triggered one more hot-reload cycle so the DLL's own lazily-constructed
  `ComputePipeline` rebuilds against the corrected `.spv`. No shipped CMake
  file was ever edited.

## STEP 3 — Full build + full `ctest` regression pass

```
cmake --build build
```
Succeeded cleanly (only the files touched by the two bug fixes above, plus
their direct dependents, rebuilt/relinked). Zero new warnings from any
touched file.

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```
**Final result: 1934 tests total, 100% passing, 7 legitimate,
environment-gated skips** — byte-for-byte matching
`editor-core-separation-14`'s own documented 1934/7 baseline. **Zero new
failures, zero new skips.** The 7 skips are the same ones documented by
every prior campaign in this whole effort (all conditional on this
development machine's Vulkan driver/loader not reporting
`VK_EXT_headless_surface`).

## STEP 4 — Documentation fix (LDD-HR9)

`docs/conventions/project-assembly-system.md`'s stale `LDD4` ("No hot
reload, anywhere, ever") corrected at all three locations (the Locked
Design Decisions list, the runtime-loader section's "keeps every `HMODULE`
alive forever" bullet, and the "What this system does NOT do" Non-Goals
list) — each via a dated strikethrough + "SUPERSEDED" correction, never a
silent deletion of the historical record. A new, permanent `## Hot Reload`
section was added (between the "On-screen Game View compositing" subsection
and "What this system does NOT do"), covering: what the feature does end to
end, the honest ECS-vs-non-ECS-state boundary (with the concrete
`m_clickCount`-style example this whole effort's own external plan cites),
the two remaining out-of-scope limitations (LDD-HR7's separate
`AssetDatabase` instances, LDD-HR8's cross-project entity-ID churn), and a
cross-reference to `docs/conventions/scene-serialization.md`. A short,
symmetrical "## Project Assembly Hot Reload's own use of this system"
section was added to `scene-serialization.md` itself, pointing back.

## Definition of Done — checked against the phase file's own list

- [x] Step 1's full success-path test passes (with the one honest,
      documented deviation: two cycles instead of one, due to the
      pre-existing shader-staging gap, not a new defect).
- [x] Step 2's full rollback-path test passes.
- [x] The full existing regression suite (`ctest`) passes, zero new
      failures versus `editor-core-separation-14`'s own documented baseline
      (1934 tests, 100%, 7 skips — unchanged).
- [x] `docs/conventions/project-assembly-system.md`'s own stale `LDD4` is
      corrected at all three locations, and a new, permanent, honest `##
      Hot Reload` section exists in that same file.
- [x] `PHASE5_COMPLETION_REPORT.md` (this file) exists;
      `CAMPAIGN_COMPLETION_REPORT.md` and the `agents.md` update follow
      immediately after this report.
- [x] Every checkbox in every one of PHASE1-4's own Definition of Done is
      re-confirmed still true (pre-flight section above).

## What this phase does NOT do

- Does NOT implement BIG-STEP 4's own named future option (b) (opt-in
  non-ECS state serialize/restore export hooks) — named in the new docs
  section as real future work, not built here.
- Does NOT attempt any process close/relaunch persistence beyond what
  already existed — unchanged, permanent non-goal.
- Does NOT run any build/test pass beyond the ONE full pass in Step 3.
- **Went beyond its own originally-planned "fill in two hook-point bodies
  only" scope in ONE deliberate, disclosed way**: fixing the two
  cross-`.dll` Registry safety bugs above was NOT part of any earlier
  phase's plan, but was required for Step 1/2's own mandatory live tests to
  pass at all, and directly closes the exact gap PHASE4's own report had
  already flagged as "a real, non-trivial, future undertaking... out of
  scope" — this phase judged, and confirmed live, that leaving it unfixed
  would make the whole campaign's own headline claim (custom component
  state survives byte-for-byte) false the moment any custom component was
  actually involved in a real reload cycle, which is precisely this
  campaign's own required proof case.

PHASE5 is complete. The entire, 4-BIG-STEP "Project Assembly Hot Reload"
effort (`editor-core-separation-12` through `-15`) is DONE.
