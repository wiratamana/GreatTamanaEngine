# editor-core-separation-13 — PHASE2: EditorPanelRegistry::UnregisterPluginPanel() (Hazard 2)

Parent: `PHASE0_MASTER_STRATEGY.md` — read it first.

Depends on: nothing (no compile dependency on PHASE1; can be implemented in
either order, this campaign just runs them serially).
Blocks: PHASE3 (`ProjectAssemblyRegistrationLedger::UnregisterEverythingFor()`
calls this method).

---

## Step 1: The Goal

Add a real, working `EditorPanelRegistry::UnregisterPluginPanel(name)` that
removes a previously-registered panel from EVERY internal list this class
owns — not just the one holding the dangerous raw pointer — so that (a) the
very next ImGui frame after an unload never calls into unmapped memory, and
(b) the SAME panel name can be registered again later (a real reload) without
being silently rejected by this class's own existing collision guard. Prove
both properties live.

## Step 2: The Situation — including a real bug the external plan left open

Confirmed, current, `src/Core/EditorPanelRegistry.h`:

```cpp
class EditorPanelRegistry {
public:
    static EditorPanelRegistry& Instance();
    void RegisterBuiltinPanelName(const std::string& name);
    void RegisterPluginPanel(const std::string& name, IEditorPanelModule_v1* module);
    bool IsKnownName(const std::string& name) const noexcept;
    const std::vector<std::string>& AllNames() const noexcept { return m_allNames; }
    struct PluginPanelEntry {
        std::string name;
        IEditorPanelModule_v1* module = nullptr;
    };
    const std::vector<PluginPanelEntry>& PluginPanels() const noexcept { return m_pluginPanels; }
private:
    EditorPanelRegistry() = default;
    std::vector<std::string> m_allNames;
    std::vector<PluginPanelEntry> m_pluginPanels;
};
```

Confirmed, current, `src/Core/EditorPanelRegistry.cpp` lines 17-45:

```cpp
void EditorPanelRegistry::RegisterPluginPanel(const std::string& name, IEditorPanelModule_v1* module)
{
    if (IsKnownName(name)) {
        GTE_LOG_WARNING("EditorPanelRegistry", "Refusing to register plugin panel '" + name + "' - a panel "
            "with this exact name is already registered ...");
        return;
    }
    m_allNames.push_back(name);
    m_pluginPanels.push_back(PluginPanelEntry{ name, module });
}

bool EditorPanelRegistry::IsKnownName(const std::string& name) const noexcept
{
    for (const std::string& candidate : m_allNames) {
        if (name == candidate) return true;
    }
    return false;
}
```

**The bug this phase must not reproduce**: the external plan
(`HOTRELOAD_BIGSTEP_02...txt`, Step 2) sketches `UnregisterPluginPanel()` as
removing ONLY from `m_pluginPanels`, deliberately leaving `m_allNames`
untouched, calling the question of whether to also clean up `m_allNames` "a
real, separate design question... deferred to BIG-STEP 3/4... a
cosmetic/UX question, not a correctness one." **This is objectively wrong
once you trace `IsKnownName()`'s own real call site above**: `IsKnownName()`
reads `m_allNames`, and `RegisterPluginPanel()` calls `IsKnownName()` FIRST,
unconditionally, on every call, refusing the call if it returns `true`. If
`UnregisterPluginPanel()` leaves `name` inside `m_allNames`, then the very
next time ANY project (most concretely, this campaign's own permanent
`ProjectAssemblyProbe` fixture, which registers a real panel named
`"Probe Panel"` — confirmed, `Projects/ProjectAssemblyProbe/Assets/Editor/
HelloEditorPanel.cpp` line 66) tries to re-register that exact same panel
name — which is EXACTLY what happens on every single reload after the
first, by construction, since a Project Assembly's own source never changes
its own panel's name across a recompile — `RegisterPluginPanel()` silently
refuses it, logs a warning, and returns without registering anything. **The
panel would work on load #1, then permanently vanish from every reload
after that, forever, for that whole process's remaining lifetime.** This is
not cosmetic; it is a guaranteed, deterministic feature regression this
campaign's own future consumer (BIG-STEP 3) would hit on its very first
real reload test against the very fixture project this whole system uses to
prove itself. **This phase resolves it now**: `UnregisterPluginPanel()` must
remove the name from BOTH `m_pluginPanels` AND `m_allNames`.

**Confirmed safe to do, concretely** (not just asserted): `search_in_dir` for
`AllNames()` found exactly 3 real call sites —
`src/Editor/DockLayout.cpp` (twice) and `src/Network/NetworkRoutes.cpp`'s
`GET /list_tabs` handler (`BuildListTabsResponseJson()`). Reading
`DockLayout.cpp` in full (lines 15-255) confirms its OWN iteration over
`AllNames()` is used only (a) to decide whether the one-time default dock
layout needs rebuilding, and (b) to decide whether to permanently latch
`ctx.dockLayoutEnsured = true` and NEVER iterate `AllNames()` again for the
rest of that session (a deliberate one-shot latch, confirmed by that file's
own doc comment, lines 210-226, explaining why re-checking every frame would
break drag-to-undock). Removing a name from `m_allNames` therefore has ZERO
effect on `DockLayout.cpp` once that latch has fired (the overwhelmingly
common case — it latches within the first few frames of any session), and
even before it latches, a panel that no longer exists correctly SHOULD NOT
be waited on. `GET /list_tabs` reporting a torn-down panel's name as if it
still existed would itself be a (minor) lie to an HTTP caller — removing it
is strictly MORE correct, not a regression. **Decision, locked for this
campaign**: `UnregisterPluginPanel()` removes from both vectors. This is a
deliberate, documented deviation from the external plan's own Step 2 — this
phase's own completion report must say so explicitly, citing this exact
reasoning, so nobody "helpfully" reverts it later.

One remaining, genuinely honest, NOT-fixed-here caveat to flag (do not try to
fix this now — out of scope for this phase, cosmetic only): if
`ctx.dockLayoutEnsured` has ALREADY latched `true` by the time a future
BIG-STEP 3 reload re-registers a panel under the SAME name, that freshly
re-registered panel may render as an undocked floating window (nothing
re-triggers a dock-layout rebuild after the initial latch). This is a
real, minor UX gap for a FUTURE campaign (BIG-STEP 3/4) to address if it
matters — write it down in this phase's own completion report as a found-but-
deferred item, do not attempt a fix here.

## Step 3: The Plan

### 3.1 — Header change (`src/Core/EditorPanelRegistry.h`)

Add, immediately after the existing `RegisterPluginPanel()` declaration:

```cpp
// editor-core-separation-13 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 2, Hazard 2 fix) - removes a previously-registered plugin panel
// by name, if present, from BOTH m_pluginPanels (the vector holding the
// dangerous raw IEditorPanelModule_v1* - unsafe to leave dangling past a
// FreeLibrary()) AND m_allNames (the plain-string list IsKnownName()'s own
// collision guard reads - leaving a stale entry there would permanently
// block this exact same name from EVER being registered again, which is
// precisely what every reload after the first needs to do; confirmed a
// real, guaranteed regression, not a hypothetical one - see this campaign's
// PHASE2 strategy file for the full reasoning). MUST be called for every
// panel name a Project Assembly's own GTE_RegisterProject call registered,
// BEFORE that Project Assembly's .dll is FreeLibrary()'d - see
// ProjectAssemblyRegistrationLedger (src/Core/Plugins/
// ProjectAssemblyRegistrationLedger.h, this same campaign's PHASE3) for the
// mechanism that guarantees this automatically. A silent no-op if `name`
// was never registered, or was already removed.
//
// NOTE this INTENTIONALLY DEVIATES from this feature's own external design
// doc (HOTRELOAD_BIGSTEP_02_TEARDOWN_SAFETY_AND_REGISTRATION_LEDGER_2026-09-28.txt,
// Step 2), which left "does this also clean up m_allNames" as an open
// question deferred to a later phase - that deferral was incorrect (see
// this campaign's PHASE2 strategy file); this method removes from BOTH
// vectors, unconditionally.
void UnregisterPluginPanel(const std::string& name);
```

### 3.2 — Implementation (`src/Core/EditorPanelRegistry.cpp`)

```cpp
void EditorPanelRegistry::UnregisterPluginPanel(const std::string& name)
{
    m_pluginPanels.erase(
        std::remove_if(m_pluginPanels.begin(), m_pluginPanels.end(),
            [&name](const PluginPanelEntry& e) { return e.name == name; }),
        m_pluginPanels.end());
    m_allNames.erase(
        std::remove_if(m_allNames.begin(), m_allNames.end(),
            [&name](const std::string& candidate) { return candidate == name; }),
        m_allNames.end());
}
```

Needs `#include <algorithm>` added to this `.cpp` if not already present
(confirmed current includes: only `"EditorPanelRegistry.h"` and `"Logging.h"`
— add `<algorithm>`).

### 3.3 — Update the now-stale doc comment on `RegisterPluginPanel()`

`EditorPanelRegistry.h` lines 36-42 currently say, verbatim: *"a non-owning
pointer into PluginHost's own registry, valid for the engine's entire
remaining lifetime (PluginHost never unloads a plugin before process exit -
Milestone 4/hot-reload is explicitly out of scope, see
PHASE0_MASTER_STRATEGY.md's Non-Goals)."* This is no longer true the moment
`UnregisterPluginPanel()` exists and is actually called by something
(PHASE3/PHASE4 of this campaign). Update this comment to say the pointer
remains valid until either process exit OR an explicit
`UnregisterPluginPanel(name)` call for that same name, whichever comes
first, and note the caller (`ProjectAssemblyRegistrationLedger`,
`ProjectAssemblyHost::UnloadProjectAssembly()`) by name — mirrors this
whole repo's own "every real caller is named in the doc comment"
convention, exactly like PHASE4 of this campaign must do for
`Renderer::WaitForGpuIdle()`'s own comment.

### 3.4 — New Tier-1 test coverage

Find (or create, if none exists yet — `search_in_dir` for
`EditorPanelRegistry` inside `tests/Core/` first to confirm either way)
`tests/Core/EditorPanelRegistryTests.cpp`. Add test cases, in complete
isolation (a throwaway fake `IEditorPanelModule_v1` implementation local to
the test file — do not depend on any real panel class):

1. Register a panel under a unique name, confirm `IsKnownName(name)` is
   `true` and it appears in both `AllNames()` and `PluginPanels()`. Call
   `UnregisterPluginPanel(name)`. Confirm `IsKnownName(name)` is now
   `false`, and the name is gone from BOTH `AllNames()` and
   `PluginPanels()`.
2. **The regression this phase exists to prevent**: after step 1's
   unregister, register the SAME name again with a (possibly different)
   fake module pointer. Confirm this SUCCEEDS (does not hit the
   `IsKnownName()` collision-guard refusal) and the panel is registered
   again, with `PluginPanels()` containing exactly one entry for that name
   (not two, not zero).
3. `UnregisterPluginPanel("NeverRegistered")` is a safe no-op.
4. Registering a built-in panel name (via `RegisterBuiltinPanelName()`)
   still correctly blocks a plugin panel from claiming that same name
   afterward (confirms this phase did not weaken the existing built-in
   name protection at all — only plugin-registered names become
   unregisterable).

Remember: `EditorPanelRegistry::Instance()` is a process-wide singleton — if
this is a NEW test file, add its exact path to `tests/CMakeLists.txt`'s
explicit file list (mirrors PHASE1's own reminder); if extending an existing
file, no CMake change is needed. Make sure each test tears down (via
`UnregisterPluginPanel()`) whatever throwaway name it registered, so no test
leaks state into another test running later in the same binary.

### 3.5 — Compile check (incremental)

```
cmake --build build --target gte_core
cmake --build build --target GreatTamanaEngineTests
```

Fix any error. Run the new/extended test cases via
`--gtest_filter=EditorPanelRegistry*` (confirm the real suite name from the
test file's own `TEST`/`TEST_F` macros first).

### 3.6 — Live verification

`run_app_background` the built `GreatTamanaEditor.exe`, then, via
`gte_send_request`:

1. `GET /list_tabs` → confirm `"Probe Panel"` still appears (this phase adds
   a capability, nothing calls `UnregisterPluginPanel()` yet in production
   code — this phase must not change what's visible today).
2. `GET /get_swapchain` after `GET /activate_tab?name=Probe Panel` → visually
   confirm the panel still renders exactly as before (unchanged behavior).
3. Confirm no new warning/error appears in `GET /get_logs` at startup.

`stop_app_background` once confirmed.

### 3.7 — Completion report

Write `PHASE2_COMPLETION_REPORT.md`: what was added, the exact deviation
from the external plan (the `m_allNames` fix) restated as shipped fact with
the concrete reasoning, the deferred DockLayout re-latch caveat (Step 2,
last paragraph), compile-check and live-verification results. Commit.

## Definition of Done

- [ ] `EditorPanelRegistry::UnregisterPluginPanel()` exists, removes from
      BOTH `m_pluginPanels` and `m_allNames`.
- [ ] `RegisterPluginPanel()`'s own stale doc comment is updated.
- [ ] New Tier-1 tests (all 4 cases above) exist and pass, especially case 2
      (register-unregister-register-same-name succeeds).
- [ ] `GET /list_tabs`/`GET /get_swapchain` show zero behavior change versus
      before this phase.
- [ ] `PHASE2_COMPLETION_REPORT.md` exists; changes committed to git.
