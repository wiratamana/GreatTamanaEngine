# PHASE2 — The Actual Tab-Activation Mechanism (ImGui docking)

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it first.
**Also read:** `PHASE1_COMPLETION_REPORT.md` before starting — it may record
a deviation from `PHASE1_EDITOR_UI_COMMAND_BRIDGE.md`'s exact wording.

## Step 1: The Goal

Give `IEditorLayer` a new method, `ActivateTab(const std::string& panelName)`,
that — when called on the real `ImGuiEditorLayer` implementation, on the
main thread, at the right point in the frame — makes Dear ImGui's docking
system bring the named tab to the front (selects it as the active tab in
its dock node, exactly as if the user had clicked that tab), and reports
back whether a live window with that name actually existed to focus.
`NullEditorLayer` gets a trivial, always-"not available" implementation.

This phase does NOT touch the network layer, `Application::Run()`'s frame
loop, or `EditorUiCommandBridge` at all — it is purely about making the
ImGui mechanism itself work and proving it manually. Wiring it to the bridge
is Phase 3's job.

## Step 2: The Situation / The Problem

- `src/Editor/EditorLayer.h` (`IEditorLayer`) is the ONLY interface
  `Application` talks to for anything Editor-related — see its own class
  comment. Every existing method already follows the pattern this phase
  must extend: a pure-virtual method, a real `ImGuiEditorLayer` override
  (`src/Editor/ImGuiEditorLayer.cpp`), and a trivial no-op `NullEditorLayer`
  override (`src/Editor/NullEditorLayer.cpp`) — see e.g. `WantsExit()` for
  the simplest possible existing example of this three-part shape.
- Dear ImGui's public API already provides exactly the primitive needed:
  `IMGUI_API void SetWindowFocus(const char* name);` (declared in the
  vendored `third_party/imgui/imgui.h` at line 508 — confirmed present,
  no `imgui_internal.h` needed for this call itself). Internally (see
  `third_party/imgui/imgui.cpp` line ~13744) it does exactly
  `if (ImGuiWindow* window = FindWindowByName(name)) FocusWindow(window);`
  — i.e. it is a safe no-op if no window with that name currently exists.
- **Mechanism detail, confirmed by reading this exact vendored ImGui source
  (important for anyone debugging this feature later, so it is spelled out
  here rather than left implicit):** `FocusWindow()` itself (`imgui.cpp`
  ~line 13901) does NOT directly write a dock node's selected tab — the one
  line that would (`dock_node->TabBar->SelectedTabId = ... =
  window->TabId;`, ~line 13962) is deliberately COMMENTED OUT upstream
  (Dear ImGui's own "#2304" fix, to avoid selecting a tab before its tab bar
  is even visible yet) — do not be alarmed if you go looking at
  `FocusWindow()`'s body and find that line dead; that is expected, not a
  sign this feature is broken. What `FocusWindow()` DOES do is call
  `SetNavWindow(window)`, setting `ImGuiContext::NavWindow`. The actual tab
  selection happens SEPARATELY, inside `ImGui::DockNodeUpdateTabBar()`
  (`imgui.cpp` ~line 19731, its own "Apply NavWindow focus back to the tab
  bar" comment, ~line 19856-19858):
  `if (g.NavWindow && g.NavWindow->RootWindow->DockNode == node)
  tab_bar->SelectedTabId = g.NavWindow->RootWindow->TabId;`. End to end,
  this IS exactly the "select this docked tab" behavior a human clicking
  the tab produces — just applied one step removed from `FocusWindow()`
  itself, via `g.NavWindow`, rather than written directly inside it.
- **This is exactly WHY `ActivateTab()` must be called between `NewFrame()`
  and `BuildUI()` in the same frame (see Step 3.4 below / Phase 3's real
  wiring) — this is load-bearing correctness, not just a tidy convention:**
  the dockspace's own `ImGui::DockSpace()` call (issued by `DockLayout.cpp`'s
  `BuildDockspaceAndMenuBar()`, itself called from `BuildUI()`) is what
  drives `DockNodeUpdate()`/`DockNodeUpdateTabBar()` for every child dock
  node THIS frame (see `imgui.cpp`'s own "Typical Docking call flow" comment,
  ~line 17998 — `DockSpace()` -> `DockNodeUpdate()`). If `g.NavWindow` is not
  already pointing at the target window by the time that `DockSpace()` call
  runs, `DockNodeUpdateTabBar()` has nothing to react to this frame — the
  selection would only apply on some LATER frame's own `DockSpace()`/
  `NewFrame()` docking update instead, silently breaking the
  `GET /activate_tab` endpoint's own contract (`PHASE0_MASTER_STRATEGY.md`:
  "the named tab was found and focused this frame"). Calling
  `SetWindowFocus()`/`FindAndFocusEditorWindow()` any time after `NewFrame()`
  has run and before `BuildUI()`'s own `BuildDockspaceAndMenuBar()` call
  satisfies this.
- The one thing `SetWindowFocus()` does NOT give the caller is a return
  value saying whether it actually found a window — so to report
  `ActivateTabOutcome::tabExists` accurately, this code ALSO needs
  `ImGui::FindWindowByName()` — which is declared in `imgui_internal.h`,
  NOT the public `imgui.h`. `src/Editor/DockLayout.cpp` already includes
  `imgui_internal.h` and already calls `ImGui::FindWindowByName()` (see its
  own `DefaultDockLayoutIsNeeded()`/`BuildDockspaceAndMenuBar()`'s one-shot
  layout logic) — this is the ONE file in this codebase that already pays
  the cost of depending on ImGui's internal header, specifically because
  building a default dock layout has no supported alternative in Dear
  ImGui's public API.
- `src/Editor/ImGuiEditorLayer.cpp` does NOT currently include
  `imgui_internal.h` — and per this codebase's own existing discipline (keep
  the internal-header dependency isolated to the ONE file that already
  needs it), it should NOT gain that include just for this feature either.
  Instead, this phase adds ONE new small helper function to
  `DockLayout.h`/`.cpp` (the file that already owns this dependency) that
  `ImGuiEditorLayer.cpp` calls into — mirroring how `ImGuiEditorLayer.cpp`
  already calls out to `BuildDockspaceAndMenuBar()` in that same file for
  everything else dock-layout-related.

## Step 3: The Plan

### 3.1 — `src/Editor/DockLayout.cpp`/`.h`: use the shared catalog + add the focus helper

First, replace `DockLayout.cpp`'s own private `kAllPanelNames` array with
Phase 1's shared `gte::kKnownEditorPanelNames` (from
`EditorPanelCatalog.h`), so there is exactly ONE list of panel names in the
whole codebase from this point on:

- Add `#include "EditorPanelCatalog.h"` to `DockLayout.cpp`'s includes.
- Delete the existing `constexpr const char* kAllPanelNames[] = { ... }`
  array (with its `#if GTE_ENABLE_PROJECT_PANEL` block) from the anonymous
  namespace in `DockLayout.cpp`.
- Every existing use of `kAllPanelNames` in that file
  (`DefaultDockLayoutIsNeeded()`'s `for` loop, `BuildDockspaceAndMenuBar()`'s
  own `allPanelsAccountedFor` loop) now iterates
  `gte::kKnownEditorPanelNames` instead — a pure rename, zero behavior
  change (this is intentionally a byte-for-byte-equivalent list at this
  point in the campaign — the PROJECT_PANEL-conditional entry is preserved
  exactly, just sourced from the shared header now).
- **Verify by inspection** that `BuildDefaultDockLayout()`'s own
  `ImGui::DockBuilderDockWindow(...)` call sequence (the literal string
  arguments to each call) still matches every name in
  `kKnownEditorPanelNames` exactly, one-for-one — this was already true
  before this change and must remain true; do not let the two silently
  diverge (this is exactly the class of bug this refactor exists to make
  structurally impossible going forward, so double-check it is actually
  true right now).

Then add the new helper — declared in `DockLayout.h`:

```cpp
// network-impl-7 campaign - the ONE place ImGui::FindWindowByName()
// (imgui_internal.h) is called from for the GET /activate_tab feature,
// mirroring this file's own pre-existing "the ONE file with an
// imgui_internal.h dependency" role (see DefaultDockLayoutIsNeeded() above).
// Looks up `panelName` by EXACT name; if a live ImGuiWindow with that exact
// name currently exists, calls ImGui::SetWindowFocus(panelName) (bringing
// it to the front - for a docked window, this selects it as that dock
// node's active tab, exactly like a user clicking the tab) and returns
// true. Returns false, and does nothing else, if no such window exists yet
// this session (e.g. requested before this panel's own first Begin() call
// this session - an accepted, narrow race - see IEditorLayer::ActivateTab()'s
// own doc comment, EditorLayer.h). Does NOT itself validate `panelName`
// against EditorPanelCatalog.h's known list - that validation already
// happened one layer up, in NetworkRoutes.cpp (Phase 4), before this call
// was ever reached; this function is a dumb, generic "does a window with
// this exact name exist right now" primitive, reusable for any future
// need, not specific to the known-panel-catalog restriction.
bool FindAndFocusEditorWindow(const char* panelName);
```

Implementation in `DockLayout.cpp` (add near the bottom, after
`BuildDockspaceAndMenuBar()`):

```cpp
bool FindAndFocusEditorWindow(const char* panelName)
{
    const ImGuiWindow* window = ImGui::FindWindowByName(panelName);
    if (window == nullptr) {
        return false;
    }
    ImGui::SetWindowFocus(panelName);
    return true;
}
```

**CORRECTION (double-checked against the real, vendored ImGui source during
this campaign's double-check pass):** this "read with `FindWindowByName()`
first, THEN call `SetWindowFocus()` by name a second time" shape is NOT an
existing pattern copied from `DefaultDockLayoutIsNeeded()`'s own loop above
— that loop only ever calls `FindWindowByName()` (to check `window->DockId
== 0`) and never calls `SetWindowFocus()` at all. Do not cite it as an
existing precedent for this shape. The real, and only, reason for the
double lookup here is that `SetWindowFocus()`'s own public-API signature
only takes a name, never a resolved `ImGuiWindow*`, so there is no way to
avoid a second name-based lookup internally without reaching for a
DIFFERENT, more internal API than this codebase already uses elsewhere
(`ImGui::FocusWindow(ImGuiWindow*, ImGuiFocusRequestFlags = 0)`, also
declared in `imgui_internal.h`, line ~3611). Do not try to "optimize" this
into a single lookup by calling that overload directly — that would be a
THIRD internal-API surface this file depends on for no measurable benefit.

### 3.2 — `src/Editor/EditorLayer.h`: add `ActivateTab()` to the interface

Add a small, ImGui-free result struct right in this header (mirroring how
`Mat4`/`Vec3` are already used directly here without needing their own
forward-declare dance) — this type must NOT be `EditorUiCommandBridge`'s
`ActivateTabOutcome` (Phase 1) reused directly, since `EditorLayer.h` (under
`src/Editor/`) must never depend on anything under `src/Application/` — that
would be a backwards layering dependency (`Application` already depends on
`Editor`, never the other way around — see `Application.h`'s own
`#include "../Editor/EditorLayer.h"`). This is the exact same "a struct
crossing a layer boundary is never accepted directly, only copied field-
by-field at the ONE place that legitimately depends on both layers" rule
`NetworkRoutes.h`'s own `TextureListEntryView`/`TransformSnapshotView`
already establish (see that file's own header comments) — applied here to
the `Editor` -> `Application` boundary instead of the `Network` ->
`Application`/`RenderGraph` boundary those two structs guard.

```cpp
// Result of ActivateTab() below - deliberately a SEPARATE, tiny,
// dependency-free type from Application/EditorUiCommandBridge.h's own
// ActivateTabOutcome (network-impl-7 campaign) - EditorLayer.h must never
// depend on anything under src/Application/ (Application depends on
// Editor, never the reverse - see this file's own class comment). Only
// Application::Run() (Phase 3) ever converts one of these into the OTHER
// type, one field at a time, at the one call site that legitimately
// depends on both.
struct TabActivationResult {
    bool tabExists = false;
};
```

Add the new pure-virtual method to `IEditorLayer` (place it near
`WantsExit()`/`WantsCaptureMouse()` at the bottom of the interface, since it
is a similarly small, standalone query/action rather than part of the
per-frame render sequence):

```cpp
    // network-impl-7 campaign - brings the named Editor panel/tab to the
    // front (Dear ImGui's own SetWindowFocus(), which for a DOCKED window
    // selects it as its dock node's active tab - exactly like a user
    // clicking the tab). `panelName` is expected to already be validated
    // against EditorPanelCatalog.h's known panel list by the CALLER
    // (Application::Run(), fed from EditorUiCommandBridge - see Phase 3) -
    // this method itself does no such validation; it just tries to find and
    // focus whatever exact name it's given. Returns tabExists == false, and
    // does nothing else, if no live ImGui window with that exact name
    // exists THIS FRAME (e.g. called before this panel's own first Begin()
    // call ever ran this session). Must only ever be called between
    // NewFrame() and BuildUI() in the SAME frame (see Application::Run(),
    // Phase 3) - calling it before NewFrame() or after Render() is
    // undefined with respect to which frame's tab-selection state it
    // affects. Always returns tabExists == false for NullEditorLayer (a
    // release build has no Editor UI/tabs to activate at all).
    virtual TabActivationResult ActivateTab(const std::string& panelName) = 0;
```

**CONFIRMED by direct inspection (this campaign's double-check pass, done
BEFORE Phase 2 implementation starts):** `EditorLayer.h` does NOT include
`<string>` transitively today. Its five local includes were each opened and
checked directly: `Math/Mat4.h` (includes `MathTypes.h`/`Vec3.h`/`Vec4.h`
only), `Math/Vec3.h` (includes `MathTypes.h` only),
`Renderer/Atmosphere/AtmosphereTypes.h` (includes `Mat4.h`/`Vec3.h`/
`<cstdint>` only), `Renderer/RenderTexture.h` (includes `DepthBuffer.h`/
`Memory/GpuMemoryTracker.h`/`RenderTarget.h`/`Vulkan/VulkanAllocator.h`/
`<memory>` only), and `Renderer/RenderGraph/RenderGraphTypes.h` (includes
`<volk.h>`/`<array>`/`<cstdint>`/`<functional>`/`<optional>`/`<vector>`
only) — none of these, nor anything they themselves pull in, ever includes
`<string>`. So `EditorLayer.h` MUST gain an explicit
`#include <string>` (alongside its existing `<memory>`/`<optional>`) for
`TabActivationResult`'s `ActivateTab(const std::string&)` parameter to even
compile — this is not a "maybe, verify first" item, it is a required change.

### 3.3 — `src/Editor/NullEditorLayer.cpp`: trivial implementation

Add, alongside the other one-line overrides:

```cpp
    TabActivationResult ActivateTab(const std::string& /*panelName*/) override { return TabActivationResult{}; }
```

(`TabActivationResult{}` default-constructs to `tabExists = false` — a
release build has nothing to activate, ever.)

### 3.4 — `src/Editor/ImGuiEditorLayer.cpp`: real implementation

Add, alongside `WantsExit()`/`WantsCaptureMouse()`/`WantsCaptureKeyboard()`:

```cpp
    // network-impl-7 campaign - see IEditorLayer::ActivateTab()'s own doc
    // comment (EditorLayer.h) for the full contract. Must set the current
    // ImGui context first, same as every other method in this class that
    // touches ImGui state outside a BuildUI()/Render() call that already
    // did so earlier this same frame (see WantsCaptureMouse() immediately
    // above for the identical pattern) - ActivateTab() is called from
    // Application::Run() at a point where NewFrame() already ran this
    // frame (see Phase 3), but nothing guarantees this is the LAST thing to
    // touch the context before it, so setting it explicitly here is cheap
    // insurance, not redundant.
    TabActivationResult ActivateTab(const std::string& panelName) override
    {
        ImGui::SetCurrentContext(m_context);
        TabActivationResult result;
        result.tabExists = FindAndFocusEditorWindow(panelName.c_str());
        return result;
    }
```

Place this method in the same `public:` section as the other simple
query/action overrides, and confirm `ImGuiEditorLayer.cpp` already has
visibility into `DockLayout.h`'s declarations (it already
`#include "DockLayout.h"` — see the file's existing includes list, Step 2
above) — no new include is needed for this call.

### 3.5 — Manual verification (no automated test possible for this one piece)

`ActivateTab()`'s real behavior needs a live ImGui context/docked window
tree to mean anything at all — this is genuinely Tier 2 (GPU/windowing-
dependent), the same accepted bucket `Renderer::Vulkan/*` already falls
into (see `AGENTS.md`, "Testability & Regression Safety") — do not attempt
to force a headless/mocked ImGui context just for this. Instead, verify
manually, BEFORE Phase 3 wires this to the network at all:

1. Add a **temporary** call to `m_editorLayer->ActivateTab("Profiler")`
   directly inside `Application::Run()` (e.g. right after
   `m_editorLayer->NewFrame();`), purely for this phase's own manual
   smoke-test — this line MUST be removed again before this phase is
   considered done; it exists only to prove Section 3.1-3.4 actually work
   before Phase 3 builds real plumbing on top of it.
2. `cmake --build build`, then `run_app_background` the built
   `GreatTamanaEngine.exe`.
3. Confirm (via `gte_send_request` against `/get_swapchain`, or
   `load_image` against a manually-taken screenshot) that the "Profiler"
   tab is the frontmost/active tab among the bottom-docked group
   ("Memory"/"Profiler"/"Render Graph"/"Jobs"/"Atmosphere"/"Project") on
   every frame, even though the DEFAULT dock layout does not
   particularly favor it.
4. `stop_app_background` the process.
5. **Remove** the temporary `ActivateTab("Profiler")` call from
   `Application::Run()` before committing — Phase 3 is what adds the real,
   permanent call site, driven by the bridge, not a hardcoded literal.

## Step 4: Verification for this phase

- `cmake --build build` succeeds (`GTE_ENABLE_EDITOR=ON`, the default).
- The manual smoke test in Section 3.5 passes, then its temporary call site
  is removed again.
- No behavior change for any EXISTING panel/menu/shortcut — this phase only
  adds new code paths, never modifies an existing one apart from the
  `kAllPanelNames` -> `gte::kKnownEditorPanelNames` rename in Section 3.1,
  which must be a pure, behavior-preserving rename (re-verify
  `DefaultDockLayoutIsNeeded()`/`BuildDockspaceAndMenuBar()`'s one-shot
  dock-layout-repair logic still works exactly as before — e.g. delete/
  rename `imgui.ini` and confirm the default Unity-style layout still
  rebuilds itself correctly on next launch).
- Write `PHASE2_COMPLETION_REPORT.md` in this same folder, `git add`/
  `git commit` the changes + report together.
