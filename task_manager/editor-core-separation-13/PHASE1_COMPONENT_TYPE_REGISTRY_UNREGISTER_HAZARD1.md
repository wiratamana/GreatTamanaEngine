# editor-core-separation-13 — PHASE1: ComponentTypeRegistry::UnregisterDescriptor() (Hazard 1)

Parent: `PHASE0_MASTER_STRATEGY.md` — read it first.

Depends on: nothing (first phase, no compile dependency on any other phase in
this campaign).
Blocks: PHASE3 (`ProjectAssemblyRegistrationLedger::UnregisterEverythingFor()`
calls this method).

---

## Step 1: The Goal

Add a real, working `ComponentTypeRegistry::UnregisterDescriptor(typeName)`
method that removes a previously-registered descriptor by name, WITHOUT
weakening `RegisterDescriptor()`'s own existing duplicate-registration
`assert()` for the genuine-programmer-error case, and prove it live.

## Step 2: The Situation

Confirmed, current, `src/ECS/Reflection/ComponentTypeRegistry.h`:

```cpp
class ComponentTypeRegistry {
public:
    static ComponentTypeRegistry& Instance();
    void RegisterDescriptor(ComponentTypeDescriptor descriptor);
    const ComponentTypeDescriptor* Find(const std::string& typeName) const;
    const std::vector<ComponentTypeDescriptor>& AllSortedByTypeName() const;
private:
    ComponentTypeRegistry() = default;
    std::vector<ComponentTypeDescriptor> m_descriptors; // kept sorted by typeName
};
```

Confirmed, current, `src/ECS/Reflection/ComponentTypeRegistry.cpp` lines 31-39:

```cpp
void ComponentTypeRegistry::RegisterDescriptor(ComponentTypeDescriptor descriptor)
{
    assert(Find(descriptor.typeName) == nullptr && "ComponentTypeRegistry::RegisterDescriptor() called twice for the same typeName - always a programmer error");
    m_descriptors.push_back(std::move(descriptor));
    std::sort(m_descriptors.begin(), m_descriptors.end(), [](const ComponentTypeDescriptor& a, const ComponentTypeDescriptor& b) {
        return a.typeName < b.typeName;
    });
}
```

No unregister method exists. `m_descriptors` is `private` — the new method
must be a member, not a free function. Existing test file to extend:
`tests/ECS/Reflection/ComponentTypeRegistryTests.cpp` — read it in full
first to match its existing style/fixture pattern exactly (do not invent a
new test style for this one file).

BIG-STEP 1 already ships a route that makes this fix directly, mechanically
observable: `GET /project_assembly/debug/component_types` (confirmed live
today: `["Camera","DirectionalLight","Name","PrimitiveSource","Transform"]`).

## Step 3: The Plan

### 3.1 — Header change (`src/ECS/Reflection/ComponentTypeRegistry.h`)

Add, immediately after the existing `RegisterDescriptor()` declaration:

```cpp
// editor-core-separation-13 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 2, Hazard 1 fix) - removes a previously-registered descriptor by
// typeName, if present. A silent no-op if `typeName` was never registered,
// or was already removed - mirrors this class's own existing "unrecognized
// key is silently ignored" forward-compatibility philosophy (see Find()'s
// own doc comment). MUST be called for every typeName a Project Assembly's
// own GTE_RegisterProject call registered, BEFORE that Project Assembly's
// .dll is FreeLibrary()'d - see ProjectAssemblyRegistrationLedger
// (src/Core/Plugins/ProjectAssemblyRegistrationLedger.h, this same campaign's
// PHASE3) for the mechanism that guarantees this automatically. Never
// asserts on a missing typeName (unlike RegisterDescriptor()'s own assert on
// a DUPLICATE) - removing something that both existed and didn't is
// meaningfully different from registering something twice; only the latter
// is unconditionally a programmer error.
void UnregisterDescriptor(const std::string& typeName);
```

### 3.2 — Implementation (`src/ECS/Reflection/ComponentTypeRegistry.cpp`)

Add, after `RegisterDescriptor()`'s existing body:

```cpp
void ComponentTypeRegistry::UnregisterDescriptor(const std::string& typeName)
{
    m_descriptors.erase(
        std::remove_if(m_descriptors.begin(), m_descriptors.end(),
            [&typeName](const ComponentTypeDescriptor& d) { return d.typeName == typeName; }),
        m_descriptors.end());
    // No re-sort needed - std::vector::erase() preserves the relative order
    // of every remaining element, and the vector was already sorted before
    // this call (RegisterDescriptor()'s own invariant) - removing entries
    // can never un-sort what remains.
}
```

`<algorithm>` is already `#include`d in this `.cpp` (confirmed, line 2) —
`std::remove_if` needs no new include.

**Do not touch `RegisterDescriptor()`'s own `assert()` line at all.** Confirm,
after this change, by re-reading the file, that line is byte-for-byte
unchanged — this phase only ADDS a method, it never weakens the existing
duplicate-detection safety net for the ordinary, non-hot-reload registration
case (a real second `RegisterDescriptor()` call for a typeName that was never
unregistered in between must still `assert()`, exactly as today).

### 3.3 — New Tier-1 test coverage

Extend `tests/ECS/Reflection/ComponentTypeRegistryTests.cpp` (read its
existing fixture/registration-cleanup pattern first — this class is a
process-wide Meyers singleton, so existing tests almost certainly already
have to deal with "don't leak a registration into the next test"; follow
whatever convention is already there rather than inventing a new one). Add
test cases proving, in complete isolation (no real Project Assembly
involved):

1. Register a throwaway descriptor with a unique typeName, confirm
   `Find(typeName)` returns non-null, call `UnregisterDescriptor(typeName)`,
   confirm `Find(typeName)` now returns `nullptr`.
2. Register the SAME typeName again after unregistering it — confirm this
   does NOT trigger the duplicate-registration `assert()` (this is the
   whole point of Hazard 1's fix — a register-unregister-register-same-name
   sequence must succeed cleanly).
3. Call `UnregisterDescriptor("SomeNameNeverRegistered")` — confirm it is a
   safe no-op (no crash, no assert, `AllSortedByTypeName()`'s size
   unchanged).
4. Confirm `AllSortedByTypeName()` stays correctly sorted after an
   unregister that removes a middle element (register 3 descriptors whose
   names sort as A/B/C, unregister B, confirm the remaining two are still
   `[A, C]` in that order).

If this test file's own gtest fixture class does not already tear down
`ComponentTypeRegistry::Instance()`'s state between tests, add
`UnregisterDescriptor()` calls in the new tests' own cleanup so they never
leak a throwaway typeName into an unrelated, later-running test (this
matters more than usual here — this singleton persists for the whole test
binary's lifetime).

### 3.4 — Add the new/modified files to the build

`ComponentTypeRegistry.h/.cpp` are already compiled as part of `gte_core` —
no `CMakeLists.txt` change needed for the production code. The test file
already exists in `tests/CMakeLists.txt`'s own list — no line needs to be
added for it (only a genuinely NEW test .cpp file would need one; this phase
only extends an existing file).

### 3.5 — Compile check (incremental, NOT a full build)

Build only the affected targets:

```
cmake --build build --target gte_core
cmake --build build --target GreatTamanaEngineTests
```//
(Working directory: repository root.) Fix any error before proceeding. Then
run just the new/extended test file's own cases:

```
build\GreatTamanaEngineTests.exe --gtest_filter=ComponentTypeRegistryTest*
```

(confirm the real, current gtest binary path/test-suite name by reading the
existing test file's `TEST`/`TEST_F` macro invocations first — do not guess
the filter string).

### 3.6 — Live verification

`run_app_background` the built `GreatTamanaEditor.exe`, then, via
`gte_send_request`:

1. `GET /project_assembly/debug/component_types` → confirm the exact same
   real list as before this phase (`Camera`, `DirectionalLight`, `Name`,
   `PrimitiveSource`, `Transform`) — this phase adds a CAPABILITY, it must
   not change what's registered at normal startup (nothing in this phase
   calls the new method from any production code path yet — that only
   happens in PHASE3/PHASE4).
2. Confirm no crash/warning appears in `GET /get_logs` related to
   `ComponentTypeRegistry` during normal startup and a few seconds of
   continued operation.

`stop_app_background` once confirmed.

### 3.7 — Completion report

Write `PHASE1_COMPLETION_REPORT.md` in this same
`task_manager/editor-core-separation-13/` folder: what was actually added,
the exact new test names, the compile-check output, the live-verification
result, and any real deviation found versus this file's own instructions
(with citation). Commit (`git_add`, `git_commit`) this phase's changes plus
the report.

## Definition of Done

- [ ] `ComponentTypeRegistry::UnregisterDescriptor()` exists exactly as
      specified, `RegisterDescriptor()`'s own `assert()` line is unchanged.
- [ ] New Tier-1 tests (all 4 cases above) exist and pass.
- [ ] `GET /project_assembly/debug/component_types` still reports the exact
      same 5 built-in type names, live, after this phase.
- [ ] `PHASE1_COMPLETION_REPORT.md` exists; changes committed to git.
