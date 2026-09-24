# PHASE3 — Editor Panel Name Collision Protection

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it first. Also read
`PHASE1_COMPLETION_REPORT.md`/`PHASE2_COMPLETION_REPORT.md` if they exist.

**Severity:** HIGH (Issue #3 of the re-analysis document).

**Source of truth for this bug:**
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\editor-core-separation\
editor-core-separation-3_REANALYSIS_bugs_and_gaps_2026-09-24.md`, section "3.
HIGH — zero collision protection for plugin panel names".

---

## Step 1: The Goal

Stop a plugin from silently hijacking a built-in panel's name (e.g. `"Log"`,
`"Inspector"`, `"Scene"`) or another already-loaded plugin's panel name. Today,
`EditorPanelRegistry::RegisterPluginPanel()` accepts ANY name unconditionally —
a duplicate causes two real, observable bugs: `DockLayout.cpp` calls
`ImGui::DockBuilderDockWindow()` twice for the identical window title, and
`ImGuiEditorLayer.cpp` calls `ImGui::Begin()` twice with the identical title in
the same frame — a well-known Dear ImGui ID-collision condition (asserts in a
debug ImGui build, or silent visual corruption in a release one).

Given this whole system's own "always all-in, zero manifest, zero toggle"
design (Locked Design Decision #1), there is no admin gate between "a `.dll`
sits in `plugins/`" and "it runs" — so this specific check (reject a colliding
name) is the ONLY defense that will ever exist here. It must exist.

## Step 2: The Situation (exact current code)

`src/Core/EditorPanelRegistry.h` already HAS an `IsKnownName()` method (used
today only by `Network/NetworkRoutes.cpp`'s `GET /activate_tab` handler — never
at registration time):

```cpp
bool IsKnownName(const std::string& name) const noexcept;
```

`src/Core/EditorPanelRegistry.cpp`, the exact, current, unguarded
implementation:

```cpp
void EditorPanelRegistry::RegisterPluginPanel(const std::string& name, IEditorPanelModule_v1* module)
{
    m_allNames.push_back(name);
    m_pluginPanels.push_back(PluginPanelEntry{ name, module });
}
```

Call site, `src/Editor/EditorHost.cpp` (inside `#if GTE_ENABLE_PLUGINS`, right
after `Core::LoadPlugins()` returns):

```cpp
for (IPluginModule* module : m_core.GetPluginHost().AllLoadedModules()) {
    if (auto* panel = static_cast<IEditorPanelModule_v1*>(
            module->QueryCapability(kIEditorPanelModule_v1_Name))) {
        EditorPanelRegistry::Instance().RegisterPluginPanel(panel->GetPanelName(), panel);
    }
}
```

`RegisterBuiltinPanelName()` (also currently unguarded — see
`EditorPanelRegistry.cpp`) is called ONLY by `EditorHost.cpp`'s own hand-
written, fixed list of exactly 10-11 real built-in panel names, BEFORE any
plugin panel is ever registered — this call site is fully first-party/trusted
code, never touched by a plugin, so it does not need the same defensive
treatment `RegisterPluginPanel()` needs (a collision here would be an engine
programmer's own bug, not a plugin author's).

## Step 3: The Plan (exact changes)

### 3.1 `EditorPanelRegistry::RegisterPluginPanel()` refuses a collision

File: `src/Core/EditorPanelRegistry.cpp`.

Replace the current unguarded body with:

```cpp
void EditorPanelRegistry::RegisterPluginPanel(const std::string& name, IEditorPanelModule_v1* module)
{
    // editor-core-separation-4 campaign, PHASE3
    // (PHASE3_EDITOR_PANEL_NAME_COLLISION_PROTECTION.md) - a plugin's own
    // IEditorPanelModule_v1::GetPanelName() can return ANY string (there is
    // no admin gate under this system's own "always all-in, zero manifest"
    // design - Locked Design Decision #1) - including, by accident or on
    // purpose, a name that already belongs to a built-in panel or an
    // earlier-loaded plugin's panel. Accepting it anyway would make
    // DockLayout.cpp call ImGui::DockBuilderDockWindow() twice for the
    // identical window title, and ImGuiEditorLayer.cpp call ImGui::Begin()
    // twice with the identical title in the same frame - a well-known Dear
    // ImGui ID-collision hazard. Refuse (log + skip) rather than silently
    // accept, mirroring this exact codebase's own established "clean skip,
    // never crash, never silently corrupt state" convention already used for
    // PluginHost's fingerprint-mismatch/missing-export/decline-to-load
    // paths (src/Core/Plugins/PluginHost.cpp).
    if (IsKnownName(name)) {
        GTE_LOG_WARNING("EditorPanelRegistry",
            "Refusing to register plugin panel '" + name + "' - a panel "
            "with this exact name is already registered (either a built-in "
            "panel or an earlier-loaded plugin's own panel). This plugin's "
            "panel will not be shown. Pick a unique panel name to fix this.");
        return;
    }

    m_allNames.push_back(name);
    m_pluginPanels.push_back(PluginPanelEntry{ name, module });
}
```

`GTE_LOG_WARNING` needs `#include "Logging.h"` in this file — check whether
it's already included (it likely is not, since this file previously never
logged anything); add `#include "Logging.h"` near the top of
`EditorPanelRegistry.cpp` if missing.

### 3.2 Leave `RegisterBuiltinPanelName()` unguarded, on purpose

Do not add the same check there — it is first-party, hand-written, trusted
code (see Step 2's reasoning above). Adding a defensive check there too is
harmless but not required by this issue; skip it to keep this phase's diff
minimal and focused, unless a genuine built-in duplicate is found during your
own read of `EditorHost.cpp`'s registration list (there should not be one).

### 3.3 Add/extend a Tier-1 test in
`tests/Core/EditorPanelRegistryTests.cpp`

This file already exists and already has a `FakeEditorPanelModule` test
double (see its own existing tests, e.g.
`RegisterPluginPanel_MakesIsKnownNameTrueAndAppearsInAllNamesAndPluginPanels`).
Add TWO new `TEST()` cases, following that file's own existing style/naming
convention exactly (each test registers its own uniquely-named,
never-reused-elsewhere test panel name, per this file's own top-of-file
comment about why):

```cpp
TEST(EditorPanelRegistryTest, RegisterPluginPanel_RefusesACollisionWithAnAlreadyRegisteredBuiltinName)
{
    EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Test_Builtin_For_Collision_Eta");

    FakeEditorPanelModule collidingModule("Test_Builtin_For_Collision_Eta");
    EditorPanelRegistry::Instance().RegisterPluginPanel("Test_Builtin_For_Collision_Eta", &collidingModule);

    // The name must still resolve to the ORIGINAL (built-in) registration,
    // never the plugin's - i.e. it must NOT appear a second time in
    // PluginPanels(), proving the plugin's own registration was refused.
    int occurrencesInPluginPanels = 0;
    for (const auto& entry : EditorPanelRegistry::Instance().PluginPanels()) {
        if (entry.name == "Test_Builtin_For_Collision_Eta") {
            ++occurrencesInPluginPanels;
        }
    }
    EXPECT_EQ(occurrencesInPluginPanels, 0);
}

TEST(EditorPanelRegistryTest, RegisterPluginPanel_RefusesACollisionWithAnAlreadyRegisteredPluginName)
{
    FakeEditorPanelModule firstModule("Test_Plugin_For_Collision_Theta");
    EditorPanelRegistry::Instance().RegisterPluginPanel("Test_Plugin_For_Collision_Theta", &firstModule);

    FakeEditorPanelModule secondModule("Test_Plugin_For_Collision_Theta");
    EditorPanelRegistry::Instance().RegisterPluginPanel("Test_Plugin_For_Collision_Theta", &secondModule);

    // Exactly ONE entry must exist for this name, and it must still point at
    // the FIRST module (first-registered wins, second is refused) - never
    // two entries for the same name.
    int occurrences = 0;
    const IEditorPanelModule_v1* resolvedModule = nullptr;
    for (const auto& entry : EditorPanelRegistry::Instance().PluginPanels()) {
        if (entry.name == "Test_Plugin_For_Collision_Theta") {
            ++occurrences;
            resolvedModule = entry.module;
        }
    }
    EXPECT_EQ(occurrences, 1);
    EXPECT_EQ(resolvedModule, &firstModule);
}
```

This is genuinely Tier-1: no GPU/SDL/live plugin `.dll` involved, just the
existing `FakeEditorPanelModule` double already defined in this same test
file (`class FakeEditorPanelModule final : public IEditorPanelModule_v1`).

### 3.4 Update `docs/conventions/plugin-architecture.md` if it describes
`RegisterPluginPanel()`'s behavior

Search for any prose there claiming/implying panel registration is
unconditional; if found, add one sentence noting the new collision refusal.
If the file doesn't mention this level of detail at all today, skip this —
do not add net-new documentation depth beyond what this phase's own code
change needs described.

## Verification (fast, incremental — no full build)

1. `cmake --build build --target GreatTamanaEngineTests` (incremental).
2. Run just the new tests, narrowly:
   `build\GreatTamanaEngineTests.exe --gtest_filter=EditorPanelRegistryTest.*`
   — confirm all `EditorPanelRegistryTest.*` cases pass, including the two new
   ones.
3. Live check (optional but recommended, cheap): `run_app_background` the
   rebuilt `GreatTamanaEditor.exe`, `gte_send_request` `GET /list_tabs` and
   confirm the existing "Demo Plugin Panel" entry (from `demo_editor_panel`)
   is still present exactly once (proves this change didn't accidentally
   start refusing the ALREADY-working, non-colliding demo plugin panel).
   `stop_app_background` when done.

## Completion

Write `PHASE3_COMPLETION_REPORT.md`: what changed, the exact test run
output (pass count), the live `/list_tabs` check result. Then `git_add` +
`git_commit` (message referencing PHASE3).
