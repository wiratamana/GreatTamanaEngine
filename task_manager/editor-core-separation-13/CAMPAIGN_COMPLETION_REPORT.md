# editor-core-separation-13 — CAMPAIGN COMPLETION REPORT

## Status: DONE

Implements the whole five-phase `editor-core-separation-13` campaign
("Project Assembly Hot Reload — BIG-STEP 2 of 4: Teardown Safety &
Registration Ledger"), per `PHASE0_MASTER_STRATEGY.md`. This is the final
phase (PHASE5) and this is the campaign's closing report — see
`PHASE1_COMPLETION_REPORT.md` through `PHASE4_COMPLETION_REPORT.md` for each
earlier phase's own detailed writeup; this report summarizes the whole
campaign, restates what PHASE5 itself added, and documents two real defects
found and fixed live during this phase's own isolation test.

---

## What was built, per phase (summary — see each phase's own report for detail)

- **PHASE1** — `ComponentTypeRegistry::UnregisterDescriptor(typeName)`
  (Hazard 1 fix): removes a previously-registered descriptor so the same
  typeName can be safely re-registered without tripping the existing
  duplicate-registration `assert()`.
- **PHASE2** — `EditorPanelRegistry::UnregisterPluginPanel(name)` (Hazard 2
  fix): removes a panel from BOTH `m_pluginPanels` and `m_allNames` — the
  `m_allNames` half is a deliberate fix beyond the external design doc's own
  sketch (which left `m_allNames` untouched, calling that a deferred,
  "cosmetic" question). Leaving it stale would have permanently blocked
  every reload after the first from ever re-registering the same panel name
  again — a guaranteed, deterministic regression, not a hypothetical one.
- **PHASE3** — new class `ProjectAssemblyRegistrationLedger`
  (`src/Core/Plugins/ProjectAssemblyRegistrationLedger.h/.cpp`): wraps
  `Core::RegisterProjectRenderPassProvider()`/`EditorPanelRegistry::
  RegisterPluginPanel()`/`ComponentTypeRegistry::RegisterDescriptor()` so
  every name a given Project Assembly's own `GTE_RegisterProject` call
  registers is recorded under that project's own ledger entry; brackets both
  `ProjectAssemblyHost::TryLoadOneAssembly()` call sites (`_Game.dll` and
  `_Editor.dll`); `UnregisterEverythingFor(projectName, core)` tears every
  recorded name back down, in reverse order. `EditorHotReloadDebugCapability::
  GetLedgerEntry()`'s placeholder body replaced with a real `PeekEntry()`
  call.
- **PHASE4** — `ProjectAssemblyHost::UnloadProjectAssembly(projectName, core,
  renderer)`: `renderer.WaitForGpuIdle()` (Hazard 4) → ledger teardown →
  `FreeLibrary()` (Editor `.dll` first, then Game), in that exact order;
  `GetLoadedAssemblyFileNames()` accessor.
  `ProjectAssemblyBuildRunner::BackupProjectAssemblyBinaries()`/
  `RestoreProjectAssemblyBinariesFromBackup()` (purely file-based,
  `.hotreload_backup/`). `EditorHotReloadDebugCapability::
  GetLoadedAssemblyFileNames()`'s placeholder body replaced with a real call.
- **PHASE5** (this phase) — extended `Projects/ProjectAssemblyProbe/` with a
  throwaway custom ECS component (`ProbeHotReloadMarker`) to exercise Hazard
  1 for real; full live isolation test of the whole unload sequence; full
  build + full `ctest` regression pass; this report; the `AGENTS.md` update;
  two real defects found-and-fixed (see below).

---

## Deliberate deviations from the external plan, found and resolved by this campaign

1. **`m_allNames` collision-guard fix (PHASE2).** The external design doc
   (`HOTRELOAD_BIGSTEP_02_TEARDOWN_SAFETY_AND_REGISTRATION_LEDGER_2026-09-28.txt`,
   Step 2) left "does unregister also clean up `m_allNames`" open, deferred,
   and mischaracterized as cosmetic/UX. This campaign resolved it in PHASE2,
   not later — `EditorPanelRegistry::IsKnownName()` reads `m_allNames` and
   `RegisterPluginPanel()` calls it first, unconditionally; leaving a name in
   `m_allNames` after unregistering it would refuse every future
   re-registration of that exact same name, forever, for the life of the
   process — a certainty for every reload after the first of any project
   with an Editor panel, including this campaign's own permanent
   `ProjectAssemblyProbe` fixture.

2. **Test mechanism for exercising `UnloadProjectAssembly()` live (PHASE5,
   Step 3.4).** Option (a) — preferred — was used: the existing
   `EditorHotReloadDebugCapability::TriggerHotReload()`/`POST
   /project_assembly/hot_reload` placeholder was TEMPORARILY repurposed to
   call `ProjectAssemblyHost::UnloadProjectAssembly()` directly, run the live
   test, then fully reverted. **Found and fixed during this phase, before the
   test could pass at all:** a first attempt at option (a) — calling
   `UnloadProjectAssembly()` (which calls `Renderer::WaitForGpuIdle()`, i.e.
   `vkDeviceWaitIdle()`) directly from the network thread while the main
   thread's own `Run()` loop kept submitting new frames concurrently — simply
   hung the network request forever (`vkDeviceWaitIdle()` from a second
   thread never observes a truly idle device while the main thread keeps
   presenting; this is a genuine Vulkan external-synchronization violation,
   not a timing fluke). Fixed by marshalling the actual unload call onto the
   MAIN thread via a small, temporary condition-variable bridge inside
   `EditorHotReloadDebugCapability`/`EditorHost::Run()` — mirroring this
   engine's own established `GET /get_texture` precedent ("GPU-touching debug
   work only ever runs on the main thread"). This whole test-hook (the
   repurposed `TriggerHotReload()` body, the `SetCoreForPhase5TestOnly()`
   setter, the `ServicePendingPhase5TestOnlyUnload()` per-frame poll, and the
   `EditorHost.cpp` wiring for both) was fully reverted before this phase
   ended — confirmed via `git diff` showing zero content difference (only a
   line-ending normalization warning, no actual hunks) against the state PHASE4
   committed for `EditorHotReloadDebugCapability.h`/`.cpp`, and `EditorHost.cpp`
   showing no diff at all. `POST /project_assembly/hot_reload` was
   re-confirmed, live, to answer its exact permanent `501`
   `{"error":"hot reload orchestrator not yet wired - see BIG-STEP 3","success":false}`
   contract after the revert.

3. **`Projects/ProjectAssemblyProbe/` (including this phase's own
   `ProbeHotReloadMarker` addition to `HelloGame.cpp`) is NOT part of this
   commit — confirmed, deliberately, NOT an oversight.** This phase's own
   spec (Step 3.8) says to commit "the probe project extension" alongside
   this report/AGENTS.md, but `.gitignore` line 115 (`/Projects/`) excludes
   the ENTIRE `Projects/` tree from version control, unconditionally — a
   deliberate, pre-existing, binding decision from campaign
   `editor-core-separation-11`'s own Locked Design Decision 3 ("per-developer,
   in-progress user C++/shader source living OUTSIDE the engine's own
   committed `src/` tree... same-toolchain, same-build-run scoped by
   design"). Confirmed via `git log --oneline -- Projects/` returning
   completely empty — this directory has never been committed, ever, in this
   repo's whole history, by any earlier campaign either. The concrete,
   already-established `.gitignore` rule is treated as authoritative over
   this phase file's own possibly-stale prose; `HelloGame.cpp`'s real,
   on-disk content (with `ProbeHotReloadMarker`) remains exactly as this
   report documents and as this phase's own live tests exercised it — it
   is simply never tracked by git, consistent with every other file under
   `Projects/` since that directory first existed.

---

## Two genuine, previously-latent defects found and fixed live during PHASE5's own isolation test

These were not introduced by careless coding in this campaign — they are
real, pre-existing architectural gaps that simply could never be exercised
before this campaign, because nothing in this engine's history ever actually
`FreeLibrary()`'d a module whose own static data had been captured elsewhere,
until PHASE4 shipped `UnloadProjectAssembly()` and PHASE5 became the first
phase to actually call it against a real, fully-hazard-populated fixture.

### 1. `ComponentTypeRegistry`'s lazy bootstrap could fire inside a Project Assembly's own ledger bracket

`ComponentTypeRegistry::Instance()` self-bootstraps
`RegisterBuiltinComponentReflections()` lazily, on its own first call, from
anywhere in the process. `ProjectAssemblyProbe`'s own `_Game.dll`
`GTE_RegisterProject` call (adding `ProbeHotReloadMarker` via
`RegisterComponentType<T>()`) turned out to be the very first call into that
registry in the whole process — meaning the built-in bootstrap fired WHILE
still inside that project's own `BeginRecordingFor()`/`EndRecording()`
bracket, misattributing all five built-in component types
(`Transform`/`Name`/`Camera`/`DirectionalLight`/`PrimitiveSource`) to
`ProjectAssemblyProbe`'s own ledger entry. Confirmed live:
`GET /project_assembly/debug/ledger?name=ProjectAssemblyProbe` listed all
five built-ins alongside the real `ProbeHotReloadMarker` before the fix.
**Fixed** by forcing the bootstrap unconditionally, at the very top of
`ProjectAssemblyHost::LoadProjectAssemblies()`, before the directory-iteration
loop that calls `TryLoadOneAssembly()` for any project ever runs — this
guarantees the bootstrap always happens outside any project's own bracket,
regardless of load order or which project's own code happens to be the first
to touch `ComponentTypeRegistry`. Confirmed live, after the fix:
`ledger?name=ProjectAssemblyProbe` correctly lists ONLY
`["ProbeHotReloadMarker"]`.

### 2. `RenderGraphNameSlotTable`/`RenderGraph::NamedStats` held dangling `const char*` pointers across an unload

The actual process-crashing defect. `RenderGraphNameSlotTable`
(`src/Renderer/RenderGraph/RenderGraphNameSlotTable.h`) — the GPU
timing-slot table `RenderGraph` owns two of, one per `ExecuteTimingMode`
regime — used to store a raw, non-owning `const char*` per assigned slot,
on the documented assumption that every pass debugName is a
process-lifetime string literal (true for every built-in pass, e.g.
`"RenderOpaque"`). But a Project Assembly's own `RenderPassDesc::debugName`
(`"ProjectAssemblyProbe.FillTexture"`) is a string literal living INSIDE
that Project Assembly's own `.dll` image, which
`ProjectAssemblyHost::UnloadProjectAssembly()` now genuinely
`FreeLibrary()`'s. Since this table (and `RenderGraph::NamedStats`, and its
own overflow-report vectors) deliberately keeps a name alive PAST any one
pass's own single-frame declaration lifetime (`FinalizeSynchronousGpuTiming()`
loops over EVERY EVER-ASSIGNED slot, unconditionally, every single frame,
regardless of whether that pass ran this frame), the very first frame after
`UnloadProjectAssembly()` returned called `NameAtSlot()` on the now-dangling
pointer, and `RenderGraph::UpdateTimingFor()`'s own `std::strcmp()` against it
immediately crashed the WHOLE process with an access violation — confirmed
live, reproducibly, on the very first attempt at the 3.4 isolation test.
**Fixed** by making `RenderGraphNameSlotTable::m_names` (and
`RenderGraph::NamedStats::name`, and `m_reportedSynchronousOverflows`/
`m_reportedPipelinedOverflows`) OWN a real `std::string` copy of every name
instead of ever storing a borrowed pointer whose lifetime it does not
control — the common "same pass re-declares the same string literal every
frame" fast path is preserved via `std::string::operator==(const char*)`;
this never trusts a caller's own pointer to remain valid past the call.
Confirmed: the pre-existing `RenderGraphNameSlotTableTests.cpp` (18 tests, an
existing, this-campaign-untouched test file) still passes 100%, byte-for-byte
unchanged behavior, after this fix.

**Both fixes are permanent, apply to ANY future Project Assembly (not just
`ProjectAssemblyProbe`), and are the direct reason the final live isolation
test (13/13 checks) and full `ctest` pass (1925/1925 excluding legitimate
skips) both succeed cleanly.**

## One additional genuine regression found and fixed via the full `ctest` pass itself

`EditorHotReloadDebugCapability::GetLoadedAssemblyFileNames()`'s PHASE4 body
unconditionally dereferenced `m_projectAssemblyHost`, on the documented
assumption ("guaranteed non-null by the time any real HTTP request can reach
this method") that only the real production wiring path
(`EditorHost`'s constructor) ever constructs this class. A pre-existing
BIG-STEP 1 test file
(`tests/Network/ProjectAssemblyHotReloadEndpointsEndToEndTests.cpp`,
`LoadedAssembliesReturnsHonestEmptyPlaceholderList`) constructs a bare
`EditorHotReloadDebugCapability` directly and legitimately never calls
`SetProjectAssemblyHost()` — this test SEGFAULTED the moment the full
`ctest` suite ran it against PHASE4's real (non-placeholder) body; PHASE1-4's
own targeted/incremental compiles never happened to include this specific,
older test file, and per this whole campaign's own workflow rule, PHASE5 is
the only phase allowed to run the full suite at all — so this was the first
point in the whole campaign this regression could even be observed. **Fixed**
with a defensive null check (`if (m_projectAssemblyHost == nullptr) { return
{}; }`), matching this whole engine's own established "unknown/unset state
degrades to an honest empty result, never a crash" philosophy used
throughout this exact class and its siblings. Confirmed: all 9 tests in that
file now pass, and the fix has zero effect on real production behavior
(`m_projectAssemblyHost` is always set by `EditorHost`'s constructor in a real
running engine).

---

## Full build and full `ctest` regression pass (Step 3.3/3.6 — the ONLY phase in this campaign permitted to run either)

```
cmake --build build
```
Succeeded cleanly (zero errors, zero warnings) on the final attempt, after
all fixes above.

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```
**Final result: 1925 tests total, 100% passing, 5 legitimate,
environment-gated skips** (up from `editor-core-separation-12`'s own
documented 1903-test baseline — the growth is every new Tier-1 test PHASE1-4
of this campaign added):

- `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine` —
  pre-existing, conditional on a real MMD model file being present on this
  machine; unrelated to this campaign.
- `ProjectAssemblyHostTest.UnloadProjectAssemblyOnANeverLoadedHostIsASafeNoOp`,
  `ProjectAssemblyRegistrationLedgerTest.
  UnregisterEverythingForANeverLoadedProjectIsASafeNoOp`,
  `ProjectAssemblyRegistrationLedgerTest.
  FullRoundTripThroughARealComponentTypeRegistrationProvesTheWholeWiring`,
  `CoreHeadlessConstructionTest.
  ConstructsWithoutCrashingAndExposesUsableAccessorsWithNoEditorLayerHookSet`
  — all four skip identically, confirmed, on this development machine's own
  Vulkan driver/loader not reporting `VK_EXT_headless_surface`
  (`vkCreateInstance failed (VkResult=-7)`) — a pre-existing, unrelated,
  environment-only gap, already documented identically by PHASE3/PHASE4's own
  completion reports; not a code defect anywhere in this campaign.

One genuine regression was found and fixed via this exact pass — see "One
additional genuine regression found and fixed via the full ctest pass
itself" above (`GetLoadedAssemblyFileNames()`'s null-pointer crash). After
the fix, a second full run confirmed 100% passing with the same 5 legitimate
skips and zero new failures.

---

## Live isolation test — all 13 checks, Step 3.4

Run against a real, freshly-built, background-launched `GreatTamanaEditor.exe`:

**Baseline (before unload):**
1. `GET /project_assembly/debug/loaded_assemblies` →
   `{"dll_file_names":["ProjectAssemblyProbe_Editor.dll","ProjectAssemblyProbe_Game.dll"]}` ✅
2. `GET /project_assembly/debug/ledger?name=ProjectAssemblyProbe` →
   `{"component_type_names":["ProbeHotReloadMarker"],"panel_names":["Probe Panel"],"project_name":"ProjectAssemblyProbe","render_pass_names":["ProjectAssemblyProbe.FillTexture"]}`
   — all three non-empty, for the first time in this whole campaign. ✅
3. `GET /project_assembly/debug/component_types` → `"ProbeHotReloadMarker"`
   present alongside the 5 built-ins. ✅
4. `GET /list_tabs` → `"Probe Panel"` present. ✅
5. `GET /render_graph` → `"ProjectAssemblyProbe.FillTexture"` present. ✅
6. `GET /get_swapchain` → normal rendering, "Probe Panel" tab active. ✅

**Unload trigger** (option (a), see "Deliberate deviations" above):
`POST /project_assembly/hot_reload?name=ProjectAssemblyProbe` → `500`
`{"error":"unexpected: TriggerHotReload() reported success in a build with no
real orchestrator","success":false}` — this is the exact, expected, harmless
stale-500 artifact this phase's own spec predicted for option (a); the unload
itself completed synchronously, server-side, before this response was ever
written (confirmed via `GET /get_logs`: `"UnloadProjectAssembly('ProjectAssemblyProbe')
complete."` was the last log line before this response was received).

**Post-unload verification:**
7. `GET /project_assembly/debug/loaded_assemblies` → `{"dll_file_names":[]}`. ✅
8. `GET /project_assembly/debug/ledger?name=ProjectAssemblyProbe` → all three
   lists empty again. ✅
9. `GET /project_assembly/debug/component_types` → back to exactly the 5
   built-ins, `ProbeHotReloadMarker` gone. ✅
10. `GET /list_tabs` → `"Probe Panel"` gone (fell back to `"Demo Plugin Panel"`
    as the active tab). ✅
11. `GET /render_graph` → `"ProjectAssemblyProbe.FillTexture"` no longer
    present. ✅
12. `GET /get_swapchain` → Editor still renders normally, no crash, no
    corrupted frame. ✅
13. **Process survives several real seconds of continued normal operation** —
    confirmed via repeated `GET /get_swapchain`/`GET /get_logs` calls across a
    multi-second span, all succeeding, no crash. **This is the single most
    important check in this whole campaign** — confirmed passing only after
    finding and fixing the two dangling-pointer defects documented above; the
    FIRST attempt at this exact test crashed the whole process on this check. ✅

All 13 checks pass. Test option (a)'s placeholder was confirmed byte-for-byte
reverted afterward (see "Deliberate deviations", item 2).

---

## Honest restatement: BIG-STEP 3 and BIG-STEP 4 remain FULLY UNIMPLEMENTED

Per this campaign's own non-goals (`PHASE0_MASTER_STRATEGY.md`) and PHASE5's
own spec:

- **BIG-STEP 3 (synchronous compile/atomic swap orchestrator) does not
  exist.** `POST /project_assembly/hot_reload` still answers a permanent
  `501 Not Implemented` — `TriggerHotReload()`'s real, permanent body is
  still `return false;`. No compile-triggered reload of any kind exists.
- **BIG-STEP 4 (ECS world-state snapshot/restore) does not exist.**
  `ProbeHotReloadMarker` deliberately carries no meaningful runtime value and
  is never attached to a live entity — this campaign proves the
  register/unregister MECHANISM only, never state survival across a reload.
- This campaign's own, exact, narrow scope: **an already-loaded Project
  Assembly can now be safely, cleanly UNLOADED, on command, in isolation** —
  nothing more.

---

## Definition of Done — this phase, and this whole campaign

- [x] `Projects/ProjectAssemblyProbe/` registers a real, throwaway custom ECS
      component type (`ProbeHotReloadMarker`).
- [x] All 13 checks in Step 3.4 pass against a real running engine, including
      the critical "process survives several seconds afterward" check (#13).
- [x] Test option (a) was used; the `TriggerHotReload()`/`POST
      /project_assembly/hot_reload` placeholder is confirmed byte-for-byte
      reverted to its permanent `501` contract afterward (`git diff` shows no
      content difference for `EditorHotReloadDebugCapability.h`/`.cpp`
      versus PHASE4's committed state, and zero diff at all for
      `EditorHost.cpp`).
- [x] Full build succeeds; full `ctest` shows zero new regressions (1925
      tests, 100% passing, 5 legitimate environment-gated skips).
- [x] `CAMPAIGN_COMPLETION_REPORT.md` exists in this folder (this file).
- [x] `AGENTS.md` has a new, accurate entry for this campaign's shipped
      surface, explicitly stating BIG-STEP 3/4 remain unimplemented.
- [x] Every checkbox in every one of PHASE1-4's own Definition of Done is
      confirmed still true:
  - **PHASE1**: `UnregisterDescriptor()` exists exactly as specified,
    `RegisterDescriptor()`'s own `assert()` unchanged; new Tier-1 tests all
    pass (re-confirmed, 13/13 `ComponentTypeRegistryTest*`, this session).
  - **PHASE2**: `UnregisterPluginPanel()` exists, removes from both vectors;
    `RegisterPluginPanel()`'s doc comment updated; new Tier-1 tests pass
    (re-confirmed, 12/12 `EditorPanelRegistryTest*`, this session);
    `GET /list_tabs`/`GET /get_swapchain` show zero behavior change (still
    true — confirmed live this phase's own baseline checks).
  - **PHASE3**: `ProjectAssemblyRegistrationLedger` exists exactly as
    specified including `PeekEntry()`; all three registration entry points
    call the matching `RecordX()` (confirmed live this phase, ledger
    non-empty for all three categories for the first time);
    `TryLoadOneAssembly()` brackets both `entry(...)` calls;
    `Core::UnregisterProjectRenderPassProvider()` exists;
    `EditorHotReloadDebugCapability::GetLedgerEntry()`'s body is real
    (confirmed live this phase).
  - **PHASE4**: `Renderer::WaitForGpuIdle()`'s doc comment lists this
    feature as a second sanctioned caller;
    `ProjectAssemblyHost::UnloadProjectAssembly()` exists, calls
    `WaitForGpuIdle()` → ledger teardown → `FreeLibrary()` in that exact
    order (confirmed live this phase, working end-to-end);
    `GetLoadedAssemblyFileNames()` exists (confirmed live, now with a
    defensive null check added this phase, see above);
    `BackupProjectAssemblyBinaries()`/`RestoreProjectAssemblyBinariesFromBackup()`
    exist, proven by isolated tests (re-confirmed passing this session);
    `EditorHotReloadDebugCapability::GetLoadedAssemblyFileNames()`'s body is
    real, `IHotReloadDebugCapability`'s own interface signature unchanged
    (confirmed by re-reading `Core/EditorCapabilities.h` this session — no
    edits made to it anywhere in this whole campaign).

**This whole campaign (`editor-core-separation-13`, BIG-STEP 2 of 4) is
DONE.**
