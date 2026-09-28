# editor-core-separation-13 — PHASE2 COMPLETION REPORT

## Status: DONE

Implements PHASE2's own "Step 3: The Plan" in full, exactly as specified — no
more, no less. No later phase's work (PHASE3-5) was touched. PHASE1's work
was not touched either (confirmed no dependency, ran serially as PHASE0
mandates).

---

## What was added

### 1. `src/Core/EditorPanelRegistry.h`

- Added `void UnregisterPluginPanel(const std::string& name);` immediately
  after the existing `RegisterPluginPanel()` declaration, with the exact doc
  comment specified in PHASE2's own section 3.1 (citing this campaign,
  BIG-STEP 2, Hazard 2, the `m_allNames` deviation, and
  `ProjectAssemblyRegistrationLedger`/`ProjectAssemblyHost::UnloadProjectAssembly()`
  as the future PHASE3/PHASE4 callers).
- Updated `RegisterPluginPanel()`'s own stale doc comment (previously: "valid
  for the engine's entire remaining lifetime ... Milestone 4/hot-reload is
  explicitly out of scope") to instead say the pointer remains valid until
  EITHER process exit OR an explicit `UnregisterPluginPanel(name)` call for
  that same name, whichever comes first — per section 3.3's instruction —
  and names the real future callers by name.

### 2. `src/Core/EditorPanelRegistry.cpp`

- Added `#include <algorithm>` (confirmed, before this change, only
  `"EditorPanelRegistry.h"` and `"Logging.h"` were included).
- Added the `UnregisterPluginPanel()` body immediately after
  `RegisterPluginPanel()`'s existing body, byte-for-byte as specified in
  section 3.2: a `std::remove_if`/`erase` pass over `m_pluginPanels` by
  `entry.name == name`, THEN the same pass over `m_allNames` by
  `candidate == name` — removing from **both** vectors, unconditionally.
- **This IS the `m_allNames` collision-guard fix this phase's own spec
  demands, shipped exactly as PHASE2's own Step 2/3.2 requires — not left
  open, not deferred.** Restated as shipped fact, with the concrete
  reasoning (this is a deliberate, documented deviation from the external
  design doc `HOTRELOAD_BIGSTEP_02_TEARDOWN_SAFETY_AND_REGISTRATION_LEDGER_2026-09-28.txt`,
  Step 2, which left "does this also clean up `m_allNames`" open, deferred,
  and mischaracterized as "cosmetic/UX, not correctness"):
  `IsKnownName()` reads `m_allNames` and `RegisterPluginPanel()` calls it
  FIRST, unconditionally, refusing any name already present. If
  `UnregisterPluginPanel()` left `name` inside `m_allNames`, the very next
  attempt to re-register that exact same panel name — which is exactly what
  every reload after the first does, by construction, since a Project
  Assembly's own panel name never changes across a recompile, including this
  campaign's own permanent `ProjectAssemblyProbe` fixture's real
  `"Probe Panel"` — would be silently refused forever, for the rest of that
  process's lifetime. That is a guaranteed, deterministic feature
  regression, not a hypothetical one, and it would hit BIG-STEP 3's very
  first real reload test against the very fixture this whole system uses to
  prove itself. This phase resolves it now by removing from BOTH vectors.

### 3. `tests/Core/EditorPanelRegistryTests.cpp`

Extended the existing file (it already existed — confirmed via
`search_in_dir` before writing any code, per section 3.4's own instruction —
and it was already listed in `tests/CMakeLists.txt` line 2106, so no CMake
change was needed) with 4 new `TEST(EditorPanelRegistryTest, ...)` cases,
matching this file's own existing plain-`TEST()` style and its own
"each test manages its own unique name(s), cleans up via
`UnregisterPluginPanel()` where meaningful" convention:

1. **`UnregisterPluginPanel_RemovesFromBothAllNamesAndPluginPanels`** —
   registers `Test_Plugin_Panel_Iota`, confirms `IsKnownName()` true and
   present in both `AllNames()`/`PluginPanels()`, calls
   `UnregisterPluginPanel()`, confirms `IsKnownName()` now false and gone
   from both vectors.
2. **`UnregisterPluginPanel_ThenReRegisteringSameNameSucceedsWithoutCollisionRefusal`**
   — THE regression test this phase exists to prevent. Registers
   `Test_Plugin_Panel_Kappa`, unregisters it, registers the exact same name
   again with a different fake module pointer, confirms this succeeds
   (not refused by `IsKnownName()`), and `PluginPanels()` contains exactly
   ONE entry for that name, pointing at the SECOND module. Cleans up
   afterward.
3. **`UnregisterPluginPanel_NeverRegisteredNameIsASafeNoOp`** — calls
   `UnregisterPluginPanel()` on a name never registered anywhere in this
   binary, confirms no fatal failure and `IsKnownName()` stays false.
4. **`UnregisterPluginPanel_DoesNotWeakenBuiltinNameCollisionProtection`** —
   registers a built-in name, attempts a colliding `RegisterPluginPanel()`
   call with the same name, confirms it is still refused (0 occurrences in
   `PluginPanels()`) and the built-in name is still known — proving only
   plugin-registered names became unregisterable, built-in protection is
   untouched.

**One self-correction made while writing test 4, worth recording**: my first
draft of test 4 additionally called `UnregisterPluginPanel()` on the
built-in name afterward "to clean up", expecting `IsKnownName()` to still
return `true` since nothing was ever pushed into `m_pluginPanels` for it.
That assumption was wrong and the test failed on first run — per section
3.2's own exact, unconditional implementation, `m_allNames.erase()` matches
by name alone, with no regard for whether that name got there via
`RegisterBuiltinPanelName()` or `RegisterPluginPanel()`. Calling
`UnregisterPluginPanel()` on a built-in name really does remove it from
`m_allNames` too. This is correct, spec-exact behavior (the method's own
doc comment says "removes ... unconditionally", not "only plugin-origin
names") — the bug was in my test's own assumption, not in the production
code. Fixed by removing that stray cleanup call from the test (this test
file's own established convention already tolerates built-in test names
persisting for the rest of the binary's lifetime — see e.g.
`RegisterBuiltinPanelName_MakesIsKnownNameTrueAndAppearsInAllNames`, which
never cleans up either). Re-ran; all 12 tests in this suite passed.

---

## Compile check (incremental, exactly the two commands PHASE2 specifies — no full build, no ctest)

```
cmake --build build --target gte_core
cmake --build build --target GreatTamanaEngineTests
```

Both succeeded cleanly (no errors, no new warnings):

```
[1/6] Building CXX object CMakeFiles/gte_core.dir/src/Core/Plugins/EditorPanelCapabilityOrchestrator.cpp.obj
[2/6] Building CXX object CMakeFiles/gte_core.dir/src/Core/EditorPanelRegistry.cpp.obj
[3/6] Building CXX object CMakeFiles/gte_core.dir/src/Network/NetworkRoutes.cpp.obj
[4/6] Building CXX object CMakeFiles/gte_core.dir/src/Network/NetworkServer.cpp.obj
[5/6] Linking CXX static library libgte_core.a
```

```
[1/11] Building CXX object tests/CMakeFiles/GreatTamanaEngineTests.dir/Core/EditorPanelRegistryTests.cpp.obj
[2/11] Building CXX object CMakeFiles/gte_editor.dir/src/Editor/DockLayout.cpp.obj
...
[7/11] Linking CXX static library libgte_editor.a
...
[10/11] Linking CXX executable tests\GreatTamanaEngineTests.exe; Copying SDL3.dll next to GreatTamanaEngineTests
```

Filtered test run (`build\tests\GreatTamanaEngineTests.exe
--gtest_filter=EditorPanelRegistryTest*`, confirmed exact real suite name
from the test file's own `TEST()` macros first):

```
[==========] Running 12 tests from 1 test suite.
[ RUN      ] EditorPanelRegistryTest.RegisterBuiltinPanelName_MakesIsKnownNameTrueAndAppearsInAllNames        [OK]
[ RUN      ] EditorPanelRegistryTest.RegisterBuiltinPanelName_DoesNotAppearInPluginPanels                     [OK]
[ RUN      ] EditorPanelRegistryTest.RegisterPluginPanel_MakesIsKnownNameTrueAndAppearsInAllNamesAndPluginPanels [OK]
[ RUN      ] EditorPanelRegistryTest.RegisterPluginPanel_ModulePointerIsUsableAndDrawsThroughTheCuratedContext [OK]
[ RUN      ] EditorPanelRegistryTest.RegisterPluginPanel_RefusesACollisionWithAnAlreadyRegisteredBuiltinName   [OK]
[ RUN      ] EditorPanelRegistryTest.RegisterPluginPanel_RefusesACollisionWithAnAlreadyRegisteredPluginName    [OK]
[ RUN      ] EditorPanelRegistryTest.IsKnownName_UnregisteredNameReturnsFalse                                  [OK]
[ RUN      ] EditorPanelRegistryTest.IsKnownName_IsCaseSensitive                                                [OK]
[ RUN      ] EditorPanelRegistryTest.UnregisterPluginPanel_RemovesFromBothAllNamesAndPluginPanels              [OK]  (NEW)
[ RUN      ] EditorPanelRegistryTest.UnregisterPluginPanel_ThenReRegisteringSameNameSucceedsWithoutCollisionRefusal [OK]  (NEW)
[ RUN      ] EditorPanelRegistryTest.UnregisterPluginPanel_NeverRegisteredNameIsASafeNoOp                       [OK]  (NEW)
[ RUN      ] EditorPanelRegistryTest.UnregisterPluginPanel_DoesNotWeakenBuiltinNameCollisionProtection          [OK]  (NEW)
[==========] 12 tests from EditorPanelRegistryTest ran.
[  PASSED  ] 12 tests.
```

All 4 new tests pass; all 8 pre-existing tests in this file still pass (no
regression). (First run of the 4 new tests actually showed 1 failure — see
the self-correction note above; fixed and re-run before this report was
written.)

---

## Live verification

`GreatTamanaEditor.exe` statically links `libgte_core.a`/`libgte_editor.a`;
since PHASE2's section 3.5 only mandates rebuilding
`gte_core`/`GreatTamanaEngineTests`, an additional incremental
`cmake --build build --target GreatTamanaEditor` (relink-only — confirmed by
its own ninja output: a single "Linking CXX executable" step plus asset
staging, no other target rebuilt) was run first so live verification
actually reflects the current code, mirroring PHASE1's own identical
approach.

Launched via `run_app_background` (PID 1748), then via `gte_send_request`:

1. `GET /list_tabs` →
   `{"tabs":["Hierarchy","Inspector","Scene","Game","Memory","Profiler","Render Graph","Jobs","Atmosphere","Log","Project","Demo Plugin Panel","Probe Panel"]}`
   — `"Probe Panel"` still present, exactly as before this phase (nothing in
   this phase's production code calls `UnregisterPluginPanel()` yet — that
   only happens in PHASE3/PHASE4, per this phase's own DoD and PHASE0's
   plan).
2. `GET /activate_tab?name=Probe Panel` → `{"activated_tab":"Probe Panel","success":true}`.
3. `GET /get_swapchain` → rendered screenshot confirmed, visually, the
   "Probe Panel" tab active and showing its real content unchanged:
   "Hello from a real Project Assembly Editor panel." + a "Click me" button
   + "Clicked 0 time(s)" label — identical to its known-good pre-phase
   appearance.
4. `GET /get_logs` (full, 36 entries at startup) and
   `GET /get_logs?category=EditorPanelRegistry` (0 entries) — no new
   warning/error anywhere related to `EditorPanelRegistry` at startup. Only
   pre-existing, unrelated `PluginHost`/`RenderFeatureCompositor`
   informational/warning entries were present (multiple render-feature
   plugins competing for the same stage — a known, pre-existing condition,
   unrelated to this phase, identical to what PHASE1's own report already
   documented).

`stop_app_background(pid: 1748)` cleanly terminated the process afterward.

---

## Deviations found versus this phase file's own instructions

1. **Self-inflicted test-writing mistake, corrected before commit** — see
   the "one self-correction made while writing test 4" note above under
   "What was added, item 3". Not a production-code deviation; the shipped
   `UnregisterPluginPanel()` body is byte-for-byte the exact implementation
   PHASE2's section 3.2 specifies.
2. No deviation in `EditorPanelRegistry.h`/`.cpp` from sections 3.1-3.3 —
   implemented exactly as specified, including the doc comment updates.
3. No `tests/CMakeLists.txt` change was needed — `EditorPanelRegistryTests.cpp`
   already existed and was already listed (line 2106), per section 3.4's own
   "if extending an existing file, no CMake change is needed" note.
4. Test binary path used: `build\tests\GreatTamanaEngineTests.exe` (one
   directory level deeper than section 3.5's own literal text says) — same,
   already-known drift PHASE1's own completion report documented; not a new
   finding, just re-confirmed.

**The one substantive deviation this phase's own spec explicitly calls out
in advance and requires restating as shipped fact**: `UnregisterPluginPanel()`
removes the name from BOTH `m_pluginPanels` AND `m_allNames`, deliberately
diverging from the external design doc's own sketch (which left `m_allNames`
untouched, mischaracterizing that as a deferred "cosmetic/UX" question). See
"What was added, item 2" above for the full, concrete reasoning (already
also fully written out in this phase's own spec file, Step 2, and in the
new doc comments on both `UnregisterPluginPanel()` and `RegisterPluginPanel()`
in `EditorPanelRegistry.h`). This is locked for the whole campaign — do not
revert.

---

## Deferred DockLayout re-latch caveat (not fixed here — intentionally out of scope)

Per this phase's own spec (Step 2, last paragraph): if
`ctx.dockLayoutEnsured` has ALREADY latched `true` (which happens within the
first few frames of any real session — confirmed by reading
`src/Editor/DockLayout.cpp` lines 210-226's own doc comment) by the time a
future BIG-STEP 3 reload re-registers a panel under the same name, that
freshly re-registered panel may render as an undocked floating window
instead of returning to its original dock position — nothing re-triggers a
dock-layout rebuild after the initial latch. This is a real, minor UX gap,
confirmed genuine (not a hypothetical), left for a FUTURE campaign
(BIG-STEP 3/4) to address if/when it actually matters in practice. No
attempt was made to fix it in this phase — doing so is explicitly out of
this phase's own scope, per its own Step 2.

---

## Definition of Done — checked off

- [x] `EditorPanelRegistry::UnregisterPluginPanel()` exists, removes from
      BOTH `m_pluginPanels` and `m_allNames`.
- [x] `RegisterPluginPanel()`'s own stale doc comment is updated.
- [x] New Tier-1 tests (all 4 cases above) exist and pass, especially case 2
      (register-unregister-register-same-name succeeds).
- [x] `GET /list_tabs`/`GET /get_swapchain` show zero behavior change versus
      before this phase.
- [x] `PHASE2_COMPLETION_REPORT.md` exists (this file); changes committed to
      git (see commit following this report).
