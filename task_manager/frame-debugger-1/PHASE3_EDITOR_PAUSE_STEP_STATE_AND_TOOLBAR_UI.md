# PHASE3 — Editor Pause/Resume + Step state and toolbar UI

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on `PHASE1`/`PHASE2` already
landed (not strictly required for this phase to compile, since it never
touches `EngineContext`/`Time` directly — but work in order regardless).

## Step 1: The Goal

Add the actual **Pause/Resume** and **Step** buttons to the Editor UI
(matching the reference screenshot's toolbar), plus the plumbing that
makes their state readable from `Application`:

- Two new `EditorContext` fields (the toggle intent).
- A new, small, always-visible (never dockable/closable) toolbar strip,
  rendered directly inside the Editor's existing dockspace host window,
  right under the menu bar.
- Two new `IEditorLayer` pure-virtual accessor methods, implemented for
  real in `ImGuiEditorLayer` and as constant `false` in `NullEditorLayer`.

**This phase does not yet change engine loop behavior.** `Application::Run()`
is not touched here at all — the new accessors exist and are correct, but
nothing calls them yet (that's PHASE4). This keeps the "add the button" and
"wire the button to actually freeze the game" changes independently
reviewable/bisectable.

## Step 2: The Situation

- `EditorContext` (`src/Editor/EditorContext.h`) is a plain struct with no
  behavior — every Editor panel/dock-layout function reads/writes it by
  reference. This is the established, correct place for new UI-owned
  toggle state (see e.g. `showBlurredSceneOutput`, an existing precedent
  for exactly this kind of "a checkbox/button toggles a plain bool here"
  pattern).
- `DockLayout.cpp`'s `BuildDockspaceAndMenuBar(EditorContext&, Game&, Renderer&)`
  builds the full-viewport dockspace host window (`"EditorDockSpaceHost"`)
  and, inside it, a menu bar (`File > Save Scene/Open Scene/Exit`) via
  `ImGui::BeginMenuBar()`/`EndMenuBar()`, followed later in the same
  function by `ImGui::DockSpace(...)`. This is the exact right place to add
  a new toolbar row: after the menu bar, before the dock space itself, so
  it always renders as a fixed strip at the top of the whole Editor,
  never as a separate dockable panel.
- `IEditorLayer` (`src/Editor/EditorLayer.h`) already has several simple
  `bool ... const` accessor methods with an identical shape to what's
  needed here — `WantsExit()`, `WantsCaptureMouse()`, `WantsCaptureKeyboard()`
  — each backed by a plain `m_ctx` field read in `ImGuiEditorLayer`, and a
  constant `false` in `NullEditorLayer`. Copy this exact pattern.
- `src/Editor/NullEditorLayer.cpp` is the release-build (`GTE_ENABLE_EDITOR=OFF`)
  stand-in — every method is a trivial no-op/constant-`false` — implement
  the two new interface methods there the same way.
- `CMakeLists.txt`'s `GTE_ENABLE_EDITOR` branch of `target_sources(gte_core ...)`
  is where `DockLayout.h/.cpp` and every `Panels/*.h/.cpp` file is listed —
  the new `PlaybackControls.h/.cpp` files go in that same list (they are
  not under `Panels/` — see 3.2 below for why).

## Step 3: The Plan

### 3.1 `src/Editor/EditorContext.h` — two new fields

Add at the very END of the struct (right after the existing
`blurredSceneOutputDescriptor` field, the current last member) — NOT
immediately after `showBlurredSceneOutput` itself, which would otherwise
wedge these two new, functionally-unrelated `bool`s between that toggle
and its own paired `VkDescriptorSet` field:

```cpp
// frame-debugger-1 campaign (task_manager/frame-debugger-1/
// PHASE3_EDITOR_PAUSE_STEP_STATE_AND_TOOLBAR_UI.md) - true whenever the
// user has toggled gameplay simulation paused via the toolbar's
// Pause/Resume button (see PlaybackControls.h). False by default - a
// brand-new session behaves EXACTLY like before this campaign (nothing
// paused, Game::Update() simulates normally every frame) until the user
// explicitly pauses. Read once per frame by
// Application::Run()/IEditorLayer::IsPlaybackPaused() - see PHASE4.
bool playbackPaused = false;

// True for exactly the one frame after the user clicks "Step" while
// playbackPaused is already true - set by PlaybackControls.cpp's own
// button handler, consumed (read-and-cleared) by
// IEditorLayer::TryConsumeStepRequest() - see PHASE4. Clicking "Step"
// while NOT already paused is a no-op (the button is rendered disabled
// while playbackPaused is false - see PlaybackControls.cpp).
bool stepOneFrameRequested = false;
```

### 3.2 New file: `src/Editor/PlaybackControls.h`

```cpp
#pragma once

namespace gte {

struct EditorContext;

// frame-debugger-1 campaign (task_manager/frame-debugger-1/
// PHASE3_EDITOR_PAUSE_STEP_STATE_AND_TOOLBAR_UI.md) - renders the small,
// ALWAYS-visible Pause/Resume + Step control strip, directly inside the
// Editor's own full-viewport dockspace host window
// ("EditorDockSpaceHost" - see DockLayout.cpp) - deliberately NOT a
// dockable/closable panel under Panels/ (unlike Hierarchy/Inspector/Scene/
// Game/...), since a Unity-style playback toolbar must always stay
// visible in a fixed place, never accidentally closed/dragged away by the
// user. Called from DockLayout.cpp's BuildDockspaceAndMenuBar(), right
// after the menu bar block (ImGui::EndMenuBar()), BEFORE the
// ImGui::DockSpace(...) call itself.
void BuildPlaybackToolbar(EditorContext& ctx);

} // namespace gte
```

Why not a `Panels/` file: every file under `Panels/` builds a real,
independent `ImGui::Begin()`/`ImGui::End()` window the user can drag,
resize, undock, or hide as a dockspace tab (see `AGENTS.md`, "Editor
Module Structure" — "a fixed set of `Panels/*.cpp` builder functions").
The playback toolbar is explicitly NOT that kind of thing — it must always
render as a fixed strip at a fixed place, the same way the menu bar itself
is not a `Panels/` file either. `PlaybackControls.h/.cpp` sits directly
under `src/Editor/`, alongside other non-panel Editor-support files like
`TransformGizmo.h`/`SceneGridRenderer.h`.

### 3.3 New file: `src/Editor/PlaybackControls.cpp`

```cpp
#include "PlaybackControls.h"

#include "EditorContext.h"

#include <imgui.h>

namespace gte {

void BuildPlaybackToolbar(EditorContext& ctx)
{
    // A single toggle button whose label reflects the CURRENT state -
    // Unity's own Play button also visually toggles rather than using two
    // separate buttons for the two states.
    if (ImGui::Button(ctx.playbackPaused ? "Resume" : "Pause")) {
        ctx.playbackPaused = !ctx.playbackPaused;
    }

    ImGui::SameLine();

    // "Step" only ever makes sense while already paused - rendered
    // visibly disabled otherwise (grayed out, un-clickable), rather than
    // silently doing nothing on click, so the control's own affordance
    // matches its actual behavior.
    ImGui::BeginDisabled(!ctx.playbackPaused);
    if (ImGui::Button("Step")) {
        ctx.stepOneFrameRequested = true;
    }
    ImGui::EndDisabled();

    if (ctx.playbackPaused) {
        ImGui::SameLine();
        ImGui::TextDisabled("(Paused)");
    }

    ImGui::Separator();
}

} // namespace gte
```

(Exact button labels/layout are a starting point — cosmetic details like
spacing/color are free to be adjusted during implementation as long as the
two buttons and their enabled/disabled/toggle behavior described above are
preserved.)

### 3.4 `src/Editor/DockLayout.cpp` wiring

Add `#include "PlaybackControls.h"` near the other includes at the top.

Inside `BuildDockspaceAndMenuBar()`, right after the existing:

```cpp
    if (ImGui::BeginMenuBar()) {
        ...
        ImGui::EndMenuBar();
    }
```

and BEFORE the existing:

```cpp
    {
        ImGuiIO& io = ImGui::GetIO();
        ...
    }

    const ImGuiID dockspaceId = ImGui::GetID("EditorDockSpace");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
```

insert:

```cpp
    // frame-debugger-1 campaign (task_manager/frame-debugger-1/
    // PHASE3_EDITOR_PAUSE_STEP_STATE_AND_TOOLBAR_UI.md) - the Pause/Resume +
    // Step playback toolbar, rendered as a fixed strip right under the menu
    // bar, before the dockspace itself - see PlaybackControls.h.
    BuildPlaybackToolbar(ctx);
```

(Exact insertion point relative to the Ctrl+S/Ctrl+O keyboard-shortcut
block right above `DockSpace()` doesn't matter functionally — either just
before or just after that block is fine, since neither touches the
toolbar's own state — but placing it immediately after `EndMenuBar()`, as
shown, keeps it visually right under the menu bar as intended.)

### 3.5 `src/Editor/EditorLayer.h` — two new pure-virtual methods

Add, near `WantsCaptureMouse()`/`WantsCaptureKeyboard()`:

```cpp
// frame-debugger-1 campaign (task_manager/frame-debugger-1/
// PHASE3_EDITOR_PAUSE_STEP_STATE_AND_TOOLBAR_UI.md) - true whenever the
// user currently has gameplay simulation paused via the toolbar (see
// PlaybackControls.h). Read once per frame by Application::Run(), BEFORE
// NewFrame(), to decide this frame's EngineContext::Time::Advance() call
// (see PHASE4) - this necessarily reflects whatever the user last clicked
// as of the END of the PREVIOUS frame's BuildUI() call, exactly one frame
// of lag, the same acceptable lag every other Editor<->engine feedback
// loop in this codebase already has (see e.g. GameViewTarget()'s own doc
// comment on resize lag). Always false for NullEditorLayer (a release
// build has no toolbar to pause with at all).
virtual bool IsPlaybackPaused() const = 0;

// True, and CLEARS the pending request (read-and-clear, exactly once), if
// the user clicked "Step" since the last time this was called - false on
// every other call, including every call after the first one following a
// given click. Called unconditionally once per frame by
// Application::Run() (see PHASE4) so a stray/stale request set while NOT
// actually paused (should never happen - the button is rendered disabled
// then - but this is defensive) is still drained rather than left
// dangling forever. Always false for NullEditorLayer.
virtual bool TryConsumeStepRequest() = 0;
```

### 3.6 `src/Editor/ImGuiEditorLayer.cpp` — implementations

Add, near `WantsCaptureMouse()`/`WantsCaptureKeyboard()`:

```cpp
bool IsPlaybackPaused() const override { return m_ctx.playbackPaused; }

bool TryConsumeStepRequest() override
{
    if (!m_ctx.stepOneFrameRequested) {
        return false;
    }
    m_ctx.stepOneFrameRequested = false;
    return true;
}
```

### 3.7 `src/Editor/NullEditorLayer.cpp` — implementations

Add, alongside the other trivial `false`-returning overrides:

```cpp
bool IsPlaybackPaused() const override { return false; }
bool TryConsumeStepRequest() override { return false; }
```

(Check the exact existing style of this file first — it may implement
everything inline in a class body, or as free `override` definitions;
match whichever convention the file already uses for
`WantsCaptureMouse()`/`WantsCaptureKeyboard()`.)

### 3.8 `CMakeLists.txt` edit

Add `src/Editor/PlaybackControls.h` and `src/Editor/PlaybackControls.cpp`
to the `if(GTE_ENABLE_EDITOR)` block's `target_sources(gte_core PRIVATE ...)`
list, right next to `src/Editor/DockLayout.h`/`src/Editor/DockLayout.cpp`.

### 3.9 Tests

No new Tier-1 test file is needed for this phase — `PlaybackControls.cpp`
is pure ImGui rendering code with no logic to unit-test independently
(mirrors the existing precedent: `DockLayout.cpp` itself has no
`DockLayoutTests.cpp` either, for the same reason). `NullEditorLayer`'s two
new trivial `false`-returning methods likewise need no dedicated test
(mirrors the existing precedent: none of its other trivial overrides have
one either). Manual/visual verification of the toolbar's actual look and
click behavior happens in PHASE5's runtime smoke test, once PHASE4 has
made it functionally meaningful.

### 3.10 Compile check for this phase

1. Build with `GTE_ENABLE_EDITOR=ON` (the default) — must succeed.
2. Build with `-DGTE_ENABLE_EDITOR=OFF` too (a second, separate configure/
   build, e.g. into a fresh build directory, or reuse one of the existing
   `build-editor-off`/`build-project-panel-off` directories already present
   in the repo root if still configured for this) — must ALSO succeed
   (confirms `NullEditorLayer.cpp`'s two new overrides compile correctly
   and the interface is satisfied by both implementations).
3. Run the full existing test suite once — must show zero new failures
   (this phase adds no new test-observable logic).

### 3.11 Report

Write `PHASE3_COMPLETION_REPORT.md` covering both the `GTE_ENABLE_EDITOR=ON`
and `=OFF` build confirmations, plus (if convenient) a screenshot via
`gte_send_request`'s `/get_swapchain` endpoint (or `/get_game_view` if more
appropriate) showing the new toolbar strip actually rendering under the
menu bar with the Editor running. `git commit`
`src/Editor/EditorContext.h`, `src/Editor/PlaybackControls.h`,
`src/Editor/PlaybackControls.cpp`, `src/Editor/DockLayout.cpp`,
`src/Editor/EditorLayer.h`, `src/Editor/ImGuiEditorLayer.cpp`,
`src/Editor/NullEditorLayer.cpp`, `CMakeLists.txt`, and the report.
