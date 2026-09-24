# PHASE4 — `IEditorPanelModule_v1`: A Plugin Gets a Real Dockable Panel

**Parent**: `PHASE0_MASTER_STRATEGY.md`. **Predecessor**:
`PHASE3_RUNTIME_RENDER_FEATURE_CAPABILITY.md`.

Maps to the source design doc's Milestone 2.

---

## Step 1: The Goal (Where are we going, this phase specifically?)

A plugin `.dll` gets its own real, dockable ImGui panel inside the Editor's
dock layout — with zero hardcoded knowledge of that specific plugin
anywhere inside `gte_editor`, and with `EditorPanelCatalog.h`'s previously
fixed, closed panel-name list evolved into a real registry every existing
built-in panel and every plugin panel both feed into identically. By the end
of this phase:

- `IEditorPanelModule_v1` (new, `gte_plugin_abi`) — the capability a plugin
  implements to own a panel.
- `IPluginPanelDrawContext` (new, `gte_plugin_abi`) — the deliberately tiny,
  curated wrapper interface a plugin uses to draw its panel's CONTENT,
  closing the ImGui-shared-global-context hazard identified in
  `PHASE0_MASTER_STRATEGY.md`, Step 2.4 — a plugin `.dll` never calls a real
  `ImGui::*` function, ever.
- `PluginPanelDrawContextAdapter` (new, `gte_editor`) — implements
  `IPluginPanelDrawContext`, forwarding each curated call to the real
  `ImGui::*` equivalent (running inside `gte_editor.a`'s own binary, which
  already correctly owns the one true `GImGui` context).
- `EditorPanelRegistry` (new, `gte_core`, replacing
  `EditorPanelCatalog.h`'s fixed array) — a real, runtime-populated registry
  seeded with every existing built-in panel name at startup, PLUS every
  loaded plugin's own `IEditorPanelModule_v1::GetPanelName()`, queried once
  after `Core::LoadPlugins()` returns.
- `DockLayout.cpp`/`NetworkRoutes.cpp`/every existing call site that reads
  `kKnownEditorPanelNames`/`IsKnownEditorPanelName()` today keeps working —
  behavior-preserving for every existing built-in panel, additive only for
  plugin panels.
- One throwaway demo plugin, `plugins/demo_editor_panel/`, whose panel shows
  one trivial `ImGui::Text()` line — exactly the source design doc's own
  Milestone 2 example ("one trivial ImGui control").
- Confirmed, live: `GET /list_tabs` includes the plugin's panel name; the
  plugin's panel is genuinely visible, docked, in a live screenshot.

---

## Step 2: The Situation

- `src/Core/EditorPanelCatalog.h` (read in full, PHASE0's own investigation)
  is exactly `kKnownEditorPanelNames[]` (a fixed `constexpr` array) +
  `kKnownEditorPanelNameCount` + `IsKnownEditorPanelName()`. Real call sites,
  confirmed via `search_in_dir`/`read_file`: `src/Editor/DockLayout.cpp` has
  THREE real consumers — `DefaultDockLayoutIsNeeded()`'s own "has every panel
  ever been docked" loop over `kKnownEditorPanelNames`; a SEPARATE, second
  real loop over `kKnownEditorPanelNames` physically inside
  `BuildDockspaceAndMenuBar()` itself (its own `allPanelsAccountedFor` check,
  right before it decides whether to call `BuildDefaultDockLayout()`) — BOTH
  of these are real loops over the array and BOTH need converting (Step 3.5);
  and `BuildDefaultDockLayout()`'s own literal, hand-written
  `DockBuilderDockWindow("Hierarchy", left)` etc. calls, ONE PER LITERAL PANEL
  NAME, which never loop over the array at all and stay UNCHANGED (Step 3.5).
  `src/Network/NetworkRoutes.h`/`.cpp` (`GET /activate_tab`/`GET /list_tabs`
  — `ParseActivateTabQuery()`'s `IsKnownEditorPanelName(nameParam)` call and
  `BuildListTabsResponseJson()`'s own
  `for (const char* name : kKnownEditorPanelNames)` loop, both confirmed via
  `read_file` on `src/Network/NetworkRoutes.cpp`).
- **The docking-position problem this phase must solve concretely**:
  `BuildDefaultDockLayout()` docks every BUILT-IN panel into an explicit,
  hand-chosen position (`left`/`right`/`center`/`bottom`). A plugin panel
  has no such hand-chosen position — the correct, safe default (mirrors
  "Profiler"/"Render Graph"/"Jobs"/"Atmosphere"/"Log" all being tabbed
  together into the SAME `bottom` node, per that function's own existing
  comments) is: **every plugin panel gets docked into the same `bottom`
  node, tabbed alongside Memory/Profiler/Render Graph/Jobs/Atmosphere/Log**,
  looped generically over the registry's own plugin-panel subset, appended
  AFTER the existing fixed sequence of `DockBuilderDockWindow()` calls (so
  existing panels' own tab order among themselves never changes).
- `ImGuiEditorLayer.cpp`'s `BuildUI()` (confirm its exact real structure via
  `read_file` before editing) calls one `BuildXPanel(ctx, ...)` free function
  per built-in panel, in a fixed sequence — this phase adds ONE new,
  final, generic loop after every existing call, iterating the plugin-panel
  subset of the registry and calling `ImGui::Begin(name)` /
  `module->BuildPanel(drawContext)` / `ImGui::End()` for each — **`Begin`/
  `End` themselves are called HOST-SIDE, by `gte_editor`'s own code, never
  by the plugin** — only the CONTENT between them goes through the curated
  `IPluginPanelDrawContext`. This keeps window-level concerns (docking,
  open/closed state, title bar) entirely a host concern, and keeps the
  plugin's own curated surface limited to "draw content," mirroring
  `Panels/*.cpp`'s own existing "one function, does its own Begin/End
  internally" shape as closely as this split allows.

---

## Step 3: The Plan

### 3.1 — `plugins/gte_plugin_abi/IEditorPanelModule.h`

```cpp
#pragma once

namespace gte {

class IPluginPanelDrawContext;

// A plugin implements this to own one dockable Editor panel - source design
// doc, Section 2/6. Queried via
// IPluginModule::QueryCapability("IEditorPanelModule_v1"). Never queried by
// a Player-shaped host process at all (source design doc, Section 7) -
// this is exactly what makes an editor-tier plugin sitting in a Player's
// own plugins/ folder harmless: nothing ever calls QueryCapability() with
// this exact string outside gte_editor's own code.
class IEditorPanelModule_v1 {
public:
    virtual ~IEditorPanelModule_v1() = default;

    // A short, stable, static-duration string literal (never a
    // freshly-allocated buffer - Locked Design Decision #3,
    // PHASE0_MASTER_STRATEGY.md) - becomes this panel's own ImGui window
    // title/dock-registry key. Called once, right after this plugin loads
    // (EditorPanelRegistry population, gte_editor) - the returned pointer
    // must remain valid for the plugin's entire loaded lifetime (i.e. it
    // must not be a value computed fresh per call and freed afterward).
    virtual const char* GetPanelName() const = 0;

    // Called once per frame, ONLY while this panel is genuinely visible
    // (the same "only do real work while actually visible/docked-open"
    // discipline every existing Panels/*.cpp builder already follows) -
    // gte_editor's own code has already called ImGui::Begin(GetPanelName())
    // before this, and will call ImGui::End() immediately after - this
    // method draws ONLY this panel's own content, via `ctx`, never calling
    // a real ImGui::* function directly (PHASE0_MASTER_STRATEGY.md, Step
    // 2.4 - the ImGui-shared-global-context hazard).
    virtual void BuildPanel(IPluginPanelDrawContext& ctx) = 0;
};

inline constexpr const char* kIEditorPanelModule_v1_Name = "IEditorPanelModule_v1";

} // namespace gte
```

### 3.2 — `plugins/gte_plugin_abi/IPluginPanelDrawContext.h`

```cpp
#pragma once

namespace gte {

// The ONLY way a plugin ever draws ImGui content - source design doc
// Section 6 never actually addresses this (see
// PHASE0_MASTER_STRATEGY.md, Step 2.4, for the full reasoning this
// campaign adds on top of the source doc). Dear ImGui keeps exactly one
// live, mutable global context per process (GImGui) - a plugin .dll
// calling a real ImGui::* function directly would operate on its OWN,
// separate, never-initialized context and crash immediately, completely
// independent of the fingerprint gate (Section 8), which only proves
// layout compatibility, never "these two separately-linked copies of
// ImGui share one live context." Every method here is implemented
// host-side, inside gte_editor.a (PluginPanelDrawContextAdapter), which
// already correctly shares the one true GImGui context - the plugin never
// touches ImGui directly, at all, ever.
//
// Deliberately minimal for this campaign's own Milestone 2 scope - exactly
// enough for "one trivial ImGui control" (source design doc, Section 11).
// A future _v2 (additive, never redefining this one) is where a genuinely
// richer widget surface (sliders, tables, tree nodes, ...) would be
// designed, once a real future capability actually needs it.
class IPluginPanelDrawContext {
public:
    virtual ~IPluginPanelDrawContext() = default;

    // Mirrors ImGui::TextUnformatted()'s own behavior (no printf-style
    // format string parsing on this side of the boundary - a plugin must
    // format its own string fully before calling this, since a va_list/
    // format string is exactly the kind of "not a plain built-in type"
    // surface Locked Design Decision #3 forbids).
    virtual void Text(const char* text) = 0;

    // Returns true exactly once, on the frame the button is clicked -
    // mirrors ImGui::Button()'s own real return-value contract.
    virtual bool Button(const char* label) = 0;

    virtual void Separator() = 0;
};

} // namespace gte

```

### 3.3 — `src/Editor/Plugins/PluginPanelDrawContextAdapter.h/.cpp` (new, `gte_editor`)

```cpp
// PluginPanelDrawContextAdapter.h
#pragma once
#include "../../../plugins/gte_plugin_abi/IPluginPanelDrawContext.h"

namespace gte {

// gte_editor's own implementation - constructed FRESH, once per visible
// plugin panel, per frame, immediately after the host's own
// ImGui::Begin(panelName) call (ImGuiEditorLayer.cpp) and destroyed before
// the matching ImGui::End() - stateless, holds nothing, simply forwards
// each call into the real ImGui:: API, which is always safe to call here
// (this code compiles into gte_editor.a itself, sharing the one true
// GImGui context - PHASE0_MASTER_STRATEGY.md, Step 2.4).
class PluginPanelDrawContextAdapter final : public IPluginPanelDrawContext {
public:
    void Text(const char* text) override;
    bool Button(const char* label) override;
    void Separator() override;
};

} // namespace gte
```
`.cpp`: each method is a one-line forward (`ImGui::TextUnformatted(text);`,
`return ImGui::Button(label);`, `ImGui::Separator();`).

### 3.4 — `src/Core/EditorPanelRegistry.h/.cpp` (new, `gte_core`, replaces `EditorPanelCatalog.h`'s fixed array)

```cpp
// EditorPanelRegistry.h
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace gte {

class IEditorPanelModule_v1;

// Replaces EditorPanelCatalog.h's fixed kKnownEditorPanelNames[] array
// (editor-core-separation-3 campaign, PHASE4) - source design doc Section
// 6: "DockLayout/EditorHost hold a registry populated at startup by (a) the
// engine's own small set of always-present, built-in panels registering
// themselves the exact same way a plugin would, plus (b) whatever
// IEditorPanelModule_v1 capabilities every loaded plugin .dll exposes."
//
// A process-wide singleton (Meyers-singleton, mirrors LoggerLogSink::
// Instance()'s own established precedent in this codebase) - deliberately
// NOT a Core-owned member, since NetworkRoutes.h/.cpp (gte_core-tier, but
// with NO reference to a live Core instance at its own call sites - see
// NetworkServer's own existing bridge-pointer-based design) needs to query
// it too, exactly like EditorPanelCatalog.h's own free functions did
// before this phase.
class EditorPanelRegistry {
public:
    static EditorPanelRegistry& Instance();

    // Called exactly once per built-in panel, at process startup, BEFORE
    // any plugin is ever loaded (see EditorHost.cpp's own new
    // registration call, Step 3.6 below) - mirrors
    // kKnownEditorPanelNames[]'s own former fixed content exactly, so
    // every existing built-in panel's own name is registered identically
    // to before this phase.
    void RegisterBuiltinPanelName(const std::string& name);

    // Called once per loaded plugin exposing IEditorPanelModule_v1,
    // immediately after Core::LoadPlugins() returns (gte_editor's own new
    // post-load step, Step 3.6 below) - `module` is a non-owning pointer
    // into PluginHost's own registry, valid for the engine's entire
    // remaining lifetime (PluginHost never unloads a plugin before process
    // exit - Milestone 4/hot-reload is explicitly out of scope, see
    // PHASE0_MASTER_STRATEGY.md's Non-Goals).
    void RegisterPluginPanel(const std::string& name, IEditorPanelModule_v1* module);

    bool IsKnownName(const std::string& name) const noexcept;

    // Every registered name, built-in AND plugin, in registration order -
    // replaces kKnownEditorPanelNames[]'s own former iteration role for
    // DockLayout.cpp's "has every panel been docked" check and
    // NetworkRoutes.cpp's GET /list_tabs.
    const std::vector<std::string>& AllNames() const noexcept { return m_allNames; }

    // Just the plugin-registered subset, in registration order - what
    // DockLayout.cpp's new generic bottom-dock loop and
    // ImGuiEditorLayer.cpp's new generic BuildUI() loop both iterate (Step
    // 3.5/3.6 below). Empty whenever GTE_ENABLE_PLUGINS is OFF or no
    // loaded plugin implements this capability - a fully safe, empty-by-
    // default state.
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

} // namespace gte
```

Delete `src/Core/EditorPanelCatalog.h` outright (not left behind as dead
code, mirroring `editor-core-separation-2` PHASE2's own established
"delete the old mechanism, don't leave a parallel one" precedent) — every
real call site converts to `EditorPanelRegistry::Instance()`.
`GTE_ENABLE_PROJECT_PANEL`'s own existing `#if` guard around registering
`"Project"` moves to the ONE call site that calls
`RegisterBuiltinPanelName("Project")` (Step 3.6 below), preserving identical
runtime behavior to today.

### 3.5 — `DockLayout.cpp` changes

`DefaultDockLayoutIsNeeded()`: replace the `for (const char* panelName :
kKnownEditorPanelNames)` loop with
`for (const std::string& panelName : EditorPanelRegistry::Instance().AllNames())`
(and `ImGui::FindWindowByName(panelName.c_str())`).

`BuildDockspaceAndMenuBar()`'s own SEPARATE `allPanelsAccountedFor` check
(confirmed real via `read_file` — a second, distinct loop over
`kKnownEditorPanelNames`, physically INSIDE `BuildDockspaceAndMenuBar()`
itself, not inside `DefaultDockLayoutIsNeeded()`, that this phase's own Step 2
investigation originally missed) needs the IDENTICAL conversion: replace its
own `for (const char* panelName : kKnownEditorPanelNames)` loop (and the
`ImGui::FindWindowByName(panelName)` call inside it) with
`for (const std::string& panelName : EditorPanelRegistry::Instance().AllNames())`
/ `ImGui::FindWindowByName(panelName.c_str())` — missing this second real call
site would leave a dangling reference to the deleted `kKnownEditorPanelNames`
array (Step 3.4) and fail to compile.

`BuildDefaultDockLayout()`: keep every existing, literal, hand-written
`DockBuilderDockWindow("Hierarchy", left)`-style call UNCHANGED, for every
built-in panel — append, at the very end of the function, immediately
before `ImGui::DockBuilderFinish(dockspaceId)`:
```cpp
// editor-core-separation-3 campaign, PHASE4 - every plugin panel gets
// tabbed into the SAME bottom node as Memory/Profiler/Render
// Graph/Jobs/Atmosphere/Log, generically, with zero hardcoded knowledge of
// any specific plugin (source design doc, Section 6).
for (const auto& entry : EditorPanelRegistry::Instance().PluginPanels()) {
    ImGui::DockBuilderDockWindow(entry.name.c_str(), bottom);
}
```
Confirmed via `read_file` on the real, current `BuildDefaultDockLayout()`
(`src/Editor/DockLayout.cpp`): `const ImGuiID bottom` is declared at the top
of the function (`ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.25f,
nullptr, &center)`) and stays in scope, unshadowed, all the way through to
`ImGui::DockBuilderFinish(dockspaceId)` at the function's end — the insertion
point described above (immediately before that `DockBuilderFinish()` call) is
correct exactly as sketched; `bottom` is genuinely still reachable there.

### 3.6 — `EditorHost.cpp` changes

Immediately after the `#if GTE_ENABLE_PLUGINS m_core.LoadPlugins(...) #endif`
block PHASE2 added, add the built-in-panel registration (BEFORE any plugin
panel is registered, so `AllNames()`'s own registration order always lists
every built-in panel first, plugins after — matching this codebase's own
"built-in seed data first" framing, source design doc Section 6) plus the
plugin-panel post-load query:
```cpp
EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Hierarchy");
EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Inspector");
EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Scene");
EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Game");
EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Memory");
EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Profiler");
EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Render Graph");
EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Jobs");
EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Atmosphere");
EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Log");
#if GTE_ENABLE_PROJECT_PANEL
EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Project");
#endif

#if GTE_ENABLE_PLUGINS
for (IPluginModule* module : m_core.GetPluginHost().AllLoadedModules()) {
    if (auto* panel = static_cast<IEditorPanelModule_v1*>(
            module->QueryCapability(kIEditorPanelModule_v1_Name))) {
        EditorPanelRegistry::Instance().RegisterPluginPanel(panel->GetPanelName(), panel);
    }
}
#endif
```
Confirm this exact literal panel-name list matches `EditorPanelCatalog.h`'s
own real, current `kKnownEditorPanelNames[]` content byte-for-byte before
deleting that file (Step 3.4) — this is the ONE place regression risk lives
in this whole phase; a typo here silently drops a real built-in panel from
`GET /list_tabs`.

### 3.7 — `ImGuiEditorLayer.cpp`'s `BuildUI()` — the new generic panel loop

Confirmed via `read_file` on the real, current `BuildUI()` body
(`src/Editor/ImGuiEditorLayer.cpp`): the LAST call inside the function, of any
kind, is `m_boneViewer.Build(registry, renderer, m_ctx, m_modelRigCache,
game.GetPhysicsSystem());`, but it sits INSIDE an `#if GTE_ENABLE_PROJECT_PANEL`
block (alongside `m_projectPanel.Build(m_ctx);` right before it) — the last
UNCONDITIONAL call is `m_frameDebuggerPanel.Build(m_ctx, renderer, renderGraph,
m_gameView, m_gameViewComposited);`, immediately followed by that
`#if GTE_ENABLE_PROJECT_PANEL ... #endif` block, then the function's closing
`}`. The new generic plugin-panel loop must be appended AFTER that
`#if GTE_ENABLE_PROJECT_PANEL ... #endif` block's own `#endif` (never inside
it), immediately before `BuildUI()`'s closing `}`, so it always runs
regardless of `GTE_ENABLE_PROJECT_PANEL`'s own value, append:
```cpp
#if GTE_ENABLE_PLUGINS
for (const auto& entry : EditorPanelRegistry::Instance().PluginPanels()) {
    if (ImGui::Begin(entry.name.c_str())) {
        PluginPanelDrawContextAdapter drawContext;
        entry.module->BuildPanel(drawContext);
    }
    ImGui::End(); // ImGui::End() must ALWAYS be called, matching ImGui::Begin()'s own documented contract, even when Begin() returned false (collapsed/not visible).
}
#endif
```

### 3.8 — `NetworkRoutes.cpp`/`.h` changes

Replace every `kKnownEditorPanelNames`/`IsKnownEditorPanelName(...)` call
site (confirm exact real locations via `search_in_dir` first) with
`EditorPanelRegistry::Instance().AllNames()`/`.IsKnownName(...)`. `GET
/list_tabs`'s own JSON array now genuinely includes plugin panel names too —
this is the correct, intended, additive behavior change (a plugin panel IS
now a real, activatable tab, exactly as real as any built-in one).

### 3.9 — Demo plugin: `plugins/demo_editor_panel/`

Mirrors PHASE2/PHASE3's own demo-plugin CMake shape exactly. `.cpp`
implements `IEditorPanelModule_v1` (`GetPanelName()` returns
`"Demo Plugin Panel"`; `BuildPanel(ctx)` calls
`ctx.Text("Hello from a plugin!");`) plus `IPluginModule::QueryCapability`
returning it for `kIEditorPanelModule_v1_Name`, mirroring
`DemoRenderFeaturePluginModule`'s own shape from PHASE3 exactly (a plugin
`.dll` MAY implement more than one capability — this one deliberately
implements only the editor-tier one, proving a plugin need not implement
both halves of a "feature" to be valid).

---

## Step 4: Verification (this phase's own mandatory checkpoint)

1. Incremental compile: `gte_plugin_abi` → `demo_editor_panel` → `gte_core`
   → `gte_editor` → `GreatTamanaEditor`.
2. `run_app_background`; `gte_send_request("/list_tabs")` — confirm
   `"Demo Plugin Panel"` appears in the returned array, alongside every
   pre-existing built-in name (compare against
   `editor-core-separation-2`'s own documented `/list_tabs` baseline —
   every one of those names must still be present, unchanged, plus exactly
   one new one).
3. `gte_send_request("/activate_tab?name=Demo Plugin Panel")` then
   `gte_send_request("/get_swapchain")` — confirm, via visual inspection of
   the returned image, the panel is genuinely visible, docked in the bottom
   strip alongside Memory/Profiler/etc., showing "Hello from a plugin!".
4. Re-run PHASE3's own Game View magenta-clear check once more here too —
   confirm this phase's own `EditorPanelCatalog.h` deletion did not
   regress anything PHASE3 already proved working.
5. `stop_app_background`.

---

## Universal Rules

Follow `PHASE0_MASTER_STRATEGY.md`'s Universal Rules exactly.

## This phase's own specific ambiguity to flag via `ask_questions` if hit

- If `DockLayout.cpp`'s real, current `bottom` dock-node variable is out of
  scope at the exact point this phase wants to append the plugin-panel
  loop (e.g. it was already consumed/shadowed before
  `DockBuilderFinish()`), do not silently restructure the function's own
  control flow beyond what is strictly needed — ask first.

## Non-Goals for this phase specifically

- No richer `IPluginPanelDrawContext` widget surface (sliders, tables,
  tree nodes) — deliberately deferred to a future `_v2`.
- No per-plugin-panel custom dock position — every plugin panel is tabbed
  into the same fixed `bottom` node for v1, matching the source design
  doc's own explicitly modest Milestone 2 scope.
