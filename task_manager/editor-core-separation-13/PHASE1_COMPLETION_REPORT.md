# editor-core-separation-13 — PHASE1 COMPLETION REPORT

## Status: DONE

Implements PHASE1's own "Step 3: The Plan" in full, exactly as specified — no
more, no less. No later phase's work (PHASE2-5) was touched.

---

## What was added

### 1. `src/ECS/Reflection/ComponentTypeRegistry.h`

Added `void UnregisterDescriptor(const std::string& typeName);` immediately
after the existing `RegisterDescriptor()` declaration, with the exact doc
comment specified in PHASE1's own section 3.1 (citing this campaign,
BIG-STEP 2, Hazard 1, and `ProjectAssemblyRegistrationLedger` as the future
PHASE3 caller mechanism).

### 2. `src/ECS/Reflection/ComponentTypeRegistry.cpp`

Added the `UnregisterDescriptor()` body immediately after
`RegisterDescriptor()`'s existing body, byte-for-byte as specified in section
3.2 (a `std::remove_if`/`erase` pass by `typeName`, no re-sort needed since
`erase()` preserves relative order of survivors and the vector was already
sorted).

**Confirmed `RegisterDescriptor()`'s own `assert()` line
(`assert(Find(descriptor.typeName) == nullptr && "ComponentTypeRegistry::
RegisterDescriptor() called twice for the same typeName - always a
programmer error");`) is byte-for-byte unchanged** — re-read after editing,
still at its original position, untouched.

`<algorithm>` was already `#include`d (line 2) — no new include needed, per
the phase file's own note.

### 3. `tests/ECS/Reflection/ComponentTypeRegistryTests.cpp`

Extended the existing file (no new file, so no `tests/CMakeLists.txt` change
needed, per section 3.4) with 4 new `TEST(ComponentTypeRegistryTest, ...)`
cases, following this file's own existing plain-`TEST()` style (no fixture
class exists in this file — it is not a `TEST_F`-based file — so each new
test manages its own unique, nowhere-else-used typeName(s) and cleans up any
throwaway registration it leaves behind at the end of its own body, mirroring
the file's pre-existing convention exactly):

1. **`ComponentTypeRegistryTest.UnregisterDescriptorRemovesAPreviouslyRegisteredDescriptor`**
   — registers `UnregisterableDummyComponent`, confirms `Find()` non-null,
   calls `UnregisterDescriptor()`, confirms `Find()` now null.
2. **`ComponentTypeRegistryTest.ReRegisteringSameTypeNameAfterUnregisterDoesNotAssert`**
   — registers `ReRegisterableDummyComponent`, unregisters it, then
   registers the exact same typeName again inside `EXPECT_NO_FATAL_FAILURE`,
   confirming the duplicate-registration `assert()` does NOT trip (this is
   the actual point of Hazard 1's fix). Cleans up afterward.
3. **`ComponentTypeRegistryTest.UnregisterDescriptorForAnUnknownTypeNameIsASafeNoOp`**
   — calls `UnregisterDescriptor()` on a typeName that was never registered
   anywhere in this binary, confirms `AllSortedByTypeName().size()` is
   unchanged and `Find()` still returns `nullptr`.
4. **`ComponentTypeRegistryTest.UnregisterDescriptorRemovingAMiddleElementKeepsRemainingSorted`**
   — registers `Hazard1MiddleDummyComponentA/B/C` (sorting alphabetically as
   A/B/C), unregisters B, confirms the WHOLE `AllSortedByTypeName()` table
   stays ascending-sorted and that A's index is still before C's index
   (mirroring this file's own pre-existing
   `AllSortedByTypeNameIsAlphabeticalRegardlessOfRegistrationOrder` test
   pattern exactly). Cleans up A and C afterward.

No deviation from the phase file's own enumerated 4 test cases (section
3.3, items 1-4) — all 4 implemented as specified.

---

## Compile check (incremental, exactly the two commands PHASE1 specifies — no full build, no ctest)

```
cmake --build build --target gte_core
cmake --build build --target GreatTamanaEngineTests
```

Both succeeded cleanly (no errors, no new warnings):

```
[1/5] Building CXX object CMakeFiles/gte_core.dir/src/ECS/Reflection/ComponentTypeRegistry.cpp.obj
[2/5] Building CXX object CMakeFiles/gte_core.dir/src/Scene/SceneBuilder.cpp.obj
[3/5] Building CXX object CMakeFiles/gte_core.dir/src/ECS/Reflection/BuiltinComponentReflection.cpp.obj
[4/5] Linking CXX static library libgte_core.a
```

```
[1/8] Building CXX object CMakeFiles/gte_editor.dir/src/Editor/EditorHotReloadDebugCapability.cpp.obj
[2/8] Building CXX object CMakeFiles/gte_editor.dir/src/Editor/SceneIO.cpp.obj
[3/8] Building CXX object tests/CMakeFiles/GreatTamanaEngineTests.dir/Scene/SceneRoundTripIntegrationTests.cpp.obj
[4/8] Building CXX object tests/CMakeFiles/GreatTamanaEngineTests.dir/ECS/Reflection/BuiltinComponentReflectionTests.cpp.obj
[5/8] Linking CXX static library libgte_editor.a
[6/8] Building CXX object tests/CMakeFiles/GreatTamanaEngineTests.dir/ECS/Reflection/ComponentTypeRegistryTests.cpp.obj
[7/8] Linking CXX executable tests\GreatTamanaEngineTests.exe; Copying SDL3.dll next to GreatTamanaEngineTests
```

Filtered test run (`build\tests\GreatTamanaEngineTests.exe
--gtest_filter=ComponentTypeRegistryTest*`, exact gtest binary path confirmed
as `build\tests\GreatTamanaEngineTests.exe`, not `build\GreatTamanaEngineTests.exe`
— see "Deviation" note below):

```
[==========] Running 13 tests from 1 test suite.
[ RUN      ] ComponentTypeRegistryTest.FindReturnsNullForATypeNameNeverRegisteredAnywhere        [OK]
[ RUN      ] ComponentTypeRegistryTest.RegisterThenFindReturnsAMatchingDescriptor                [OK]
[ RUN      ] ComponentTypeRegistryTest.HasComponentTryGetAndEnsureDefaultComponentWrapARealRegistry [OK]
[ RUN      ] ComponentTypeRegistryTest.RoundTripsEveryFieldThroughWriteJsonThenReadJson           [OK]
[ RUN      ] ComponentTypeRegistryTest.MissingKeyKeepsPreExistingValueAndStillReturnsTrue          [OK]
[ RUN      ] ComponentTypeRegistryTest.ExtraUnrecognizedKeyIsSilentlyIgnored                       [OK]
[ RUN      ] ComponentTypeRegistryTest.WrongJsonTypeForAPlainFieldFailsCleanlyWithoutThrowing       [OK]
[ RUN      ] ComponentTypeRegistryTest.MalformedVec3ArrayFailsCleanlyWithoutThrowing                [OK]
[ RUN      ] ComponentTypeRegistryTest.AllSortedByTypeNameIsAlphabeticalRegardlessOfRegistrationOrder [OK]
[ RUN      ] ComponentTypeRegistryTest.UnregisterDescriptorRemovesAPreviouslyRegisteredDescriptor   [OK]  (NEW)
[ RUN      ] ComponentTypeRegistryTest.ReRegisteringSameTypeNameAfterUnregisterDoesNotAssert        [OK]  (NEW)
[ RUN      ] ComponentTypeRegistryTest.UnregisterDescriptorForAnUnknownTypeNameIsASafeNoOp          [OK]  (NEW)
[ RUN      ] ComponentTypeRegistryTest.UnregisterDescriptorRemovingAMiddleElementKeepsRemainingSorted [OK]  (NEW)
[==========] 13 tests from ComponentTypeRegistryTest ran. (3 ms total)
[  PASSED  ] 13 tests.
```

All 4 new tests pass; all 9 pre-existing tests in this file still pass
(no regression).

---

## Live verification

Since `GreatTamanaEditor.exe`'s own on-disk binary statically links
`libgte_core.a`, and PHASE1's section 3.5 only mandates rebuilding
`gte_core`/`GreatTamanaEngineTests`, an additional incremental
`cmake --build build --target GreatTamanaEditor` (relink-only — 1/2 steps
were pure file-staging, the only real work was re-linking the already-built
`gte_core.a` into the exe; this is NOT a full `cmake --build build` and
touches no other target) was run so the live-verification step below
actually reflects the current code, not a stale pre-change binary.

Launched via `run_app_background` (PID 12056), then via `gte_send_request`:

1. `GET /project_assembly/debug/component_types` →
   `{"type_names":["Camera","DirectionalLight","Name","PrimitiveSource","Transform"]}`
   — identical to the pre-phase baseline PHASE1 itself cites. Confirmed
   immediately after startup AND again several seconds later (log id count
   unchanged: 36 both times) — the built-in registration set has not moved
   at all, exactly as expected (nothing in this phase calls
   `UnregisterDescriptor()` from any production code path yet — that only
   happens in PHASE3/PHASE4, per this phase's own DoD and PHASE0's plan).
2. `GET /get_logs` (full, and filtered `?category=ComponentTypeRegistry`,
   which returned 0 entries) — no crash, no warning, no error anywhere
   related to `ComponentTypeRegistry` during startup or the several seconds
   of continued operation observed. Only pre-existing, unrelated `PluginHost`
   /`RenderFeatureCompositor` informational/warning entries were present
   (multiple render-feature plugins competing for the same stage — a known,
   pre-existing condition, unrelated to this phase).

`stop_app_background(pid: 12056)` cleanly terminated the process afterward.

---

## Deviations found versus this phase file's own instructions

1. **Test binary path.** Section 3.5 says to run
   `build\GreatTamanaEngineTests.exe --gtest_filter=...`. The actual built
   path (confirmed from the linker's own build output:
   `Linking CXX executable tests\GreatTamanaEngineTests.exe`) is
   `build\tests\GreatTamanaEngineTests.exe`, one directory level deeper. Not
   a code issue — purely a path in this phase file's own section 3.5 that
   has drifted from the current CMake target-output-directory layout. Used
   the correct, actual path; no other tool/behavior was affected.

No other deviation. `ComponentTypeRegistry.h/.cpp` needed no other change;
`tests/CMakeLists.txt` needed no edit (this phase only extended an existing,
already-listed test file, per section 3.4).

---

## Definition of Done — checked off

- [x] `ComponentTypeRegistry::UnregisterDescriptor()` exists exactly as
      specified, `RegisterDescriptor()`'s own `assert()` line is unchanged.
- [x] New Tier-1 tests (all 4 cases above) exist and pass.
- [x] `GET /project_assembly/debug/component_types` still reports the exact
      same 5 built-in type names, live, after this phase.
- [x] `PHASE1_COMPLETION_REPORT.md` exists (this file); changes committed to
      git (see commit following this report).
