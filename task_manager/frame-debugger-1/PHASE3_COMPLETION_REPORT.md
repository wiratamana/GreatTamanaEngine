# PHASE3 — Completion Report: Editor Pause/Resume + Step state and toolbar UI

Phase file: `PHASE3_EDITOR_PAUSE_STEP_STATE_AND_TOOLBAR_UI.md`
Parent: `PHASE0_MASTER_STRATEGY.md`
Branch: `feature/frame-debugger-impl`

## Summary

Implemented PHASE3 exactly per its "Step 3: The Plan" — added the actual
Pause/Resume + Step toolbar to the Editor UI, plus the plumbing that makes
its state readable from `Application` (not yet consumed — that is PHASE4).
No engine-loop behavior changed in this phase: `Application::Run()` was not
touched at all.

## What was added

- **`src/Editor/PlaybackControls.h`** — declares
  `void BuildPlaybackToolbar(EditorContext& ctx);`, copied verbatim from the
  phase document's own header (forward-declares `EditorContext`, no other
  dependency).
- **`src/Editor/PlaybackControls.cpp`** — copied verbatim from the phase
  document:
  - A single toggle button whose label flips between `"Pause"` and
    `"Resume"` depending on `ctx.playbackPaused`.
  - A `"Step"` button, wrapped in `ImGui::BeginDisabled(!ctx.playbackPaused)`/
    `EndDisabled()` so it renders visibly grayed-out and un-clickable unless
    already paused.
  - A `"(Paused)"` text-disabled indicator shown only while paused.
  - A trailing `ImGui::Separator()` to visually close off the toolbar strip.

## What was modified

- **`src/Editor/EditorContext.h`** — added two new `bool` fields at the very
  END of the struct (after the existing `blurredSceneOutputDescriptor`, per
  the phase document's explicit placement instruction, not immediately
  after `showBlurredSceneOutput`):
  - `bool playbackPaused = false;`
  - `bool stepOneFrameRequested = false;`

  Both copied verbatim (including doc comments) from the phase document.

- **`src/Editor/DockLayout.cpp`**
  - Added `#include "PlaybackControls.h"` alongside the other includes.
  - Inserted `BuildPlaybackToolbar(ctx);` (with its doc comment) directly
    after the existing `ImGui::EndMenuBar()` block's closing brace and
    BEFORE the Ctrl+S/Ctrl+O keyboard-shortcut block and the
    `ImGui::DockSpace(...)` call — exactly the placement the phase document
    specifies, so the toolbar renders as a fixed strip immediately under the
    menu bar, always before the dockspace itself.

- **`src/Editor/EditorLayer.h`** — added two new pure-virtual methods to
  `IEditorLayer`, placed right after the existing
  `WantsCaptureKeyboard()` and before `ActivateTab()`, with the phase
  document's own doc comments verbatim:
  - `virtual bool IsPlaybackPaused() const = 0;`
  - `virtual bool TryConsumeStepRequest() = 0;`

- **`src/Editor/ImGuiEditorLayer.cpp`** — added the two real implementations
  right after `WantsCaptureKeyboard()`:
  - `bool IsPlaybackPaused() const override { return m_ctx.playbackPaused; }`
  - `bool TryConsumeStepRequest() override` — reads and clears
    `m_ctx.stepOneFrameRequested` exactly once (returns `false` on every
    call after the first following a click, per the phase document's
    read-and-clear contract).

  Neither of these needs `ImGui::SetCurrentContext(m_context)` (unlike
  `WantsCaptureMouse()`/`WantsCaptureKeyboard()` immediately above them) —
  they only read/write a plain `EditorContext` field, never touch ImGui
  state directly.

- **`src/Editor/NullEditorLayer.cpp`** — added the two trivial
  constant-`false` overrides, matching this file's existing single-line
  inline-body convention used by every other trivial override
  (`WantsCaptureMouse()`, `WantsCaptureKeyboard()`, etc.):
  - `bool IsPlaybackPaused() const override { return false; }`
  - `bool TryConsumeStepRequest() override { return false; }`

- **`CMakeLists.txt`** — added `src/Editor/PlaybackControls.h` and
  `src/Editor/PlaybackControls.cpp` to the `GTE_ENABLE_EDITOR` branch's
  `target_sources(gte_core PRIVATE ...)` list, immediately after
  `src/Editor/DockLayout.h`/`src/Editor/DockLayout.cpp`, per the phase
  document's exact instruction.

## Tests

Per the phase document's own "3.9 Tests" section: no new Tier-1 test file
was added. `PlaybackControls.cpp` is pure ImGui rendering code with no
independently-testable logic (mirrors the existing `DockLayout.cpp`/no
`DockLayoutTests.cpp` precedent), and `NullEditorLayer`'s two new trivial
`false`-returning overrides need no dedicated test either (mirrors every
other trivial override in that file). Manual/visual verification of the
toolbar happened via a live runtime smoke test (below), and functional
Pause/Step behavior itself is reserved for PHASE4/PHASE5 once
`Application::Run()` actually consumes these new accessors.

## Compile check performed

1. **`GTE_ENABLE_EDITOR=ON` (default `build/` directory)**:
   `cmake --build build --target GreatTamanaEngineTests` — succeeded
   (rebuilt `gte_core` including the new `PlaybackControls.cpp`,
   `DockLayout.cpp`, `ImGuiEditorLayer.cpp`; relinked
   `tests\GreatTamanaEngineTests.exe`). Then, separately,
   `cmake --build build --target GreatTamanaEngine` — succeeded (relinked
   the main app exe against the updated `gte_core` — needed for the live
   runtime smoke test below, since the earlier `GreatTamanaEngineTests`-only
   build target does not relink the main app).
2. **`GTE_ENABLE_EDITOR=OFF` (existing `build-editor-off/` directory)**:
   `cmake --build build-editor-off` (default `all` target, so both the
   test binary and the main app relinked) — succeeded with zero errors,
   confirming `NullEditorLayer.cpp`'s two new overrides compile correctly
   and fully satisfy `IEditorLayer` with no ImGui/toolbar code linked in at
   all.
3. **Full existing test suite** (`build/tests\GreatTamanaEngineTests.exe`,
   no filter): **`[  PASSED  ] 1338 tests.` / `[  SKIPPED ] 1 test`**
   (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`, the
   same pre-existing machine-gated skip this repo has always had). **Zero
   new failures** — this phase adds no new test-observable logic (per the
   phase document's own expectation), and the full pass count exactly
   matches PHASE2's own reported total, confirming no regression.

## Live runtime smoke test

Launched `build\GreatTamanaEngine.exe` in the background and captured
`GET /get_swapchain` twice:

- **First capture** (before rebuilding the main app exe against the
  freshly-updated `gte_core`): the toolbar was NOT visible — this was a
  genuine build-hygiene gotcha, not a code bug: the earlier
  `GreatTamanaEngineTests`-only build target relinks `gte_core`/the test
  binary but does **not** relink the separate `GreatTamanaEngine` app
  target, so the running exe was still the stale pre-PHASE3 build. Rebuilt
  with `cmake --build build --target GreatTamanaEngine` (see above) and
  relaunched.
- **Second capture** (after the targeted rebuild): the "Pause"/"Step"
  toolbar strip is now clearly visible directly under the "File" menu bar,
  right where the phase document's diagram places it — "Pause" rendered as
  a normal, clickable button; "Step" rendered visibly grayed-out/disabled
  (since playback is not currently paused), exactly matching
  `PlaybackControls.cpp`'s documented enabled/disabled contract. Confirmed
  visually via `gte_send_request`'s `/get_swapchain` capture. The app was
  then cleanly stopped via `stop_app_background`.

## Notes for the next phase (PHASE4)

- `EditorContext::playbackPaused`/`stepOneFrameRequested`,
  `IEditorLayer::IsPlaybackPaused()`/`TryConsumeStepRequest()`, and the
  visible toolbar are all in place and correct. `Application::Run()` still
  hardcodes `isPaused = false`/`isSteppedThisFrame = false` (from PHASE2) —
  PHASE4 is what actually reads these new accessors and feeds them into
  `EngineContext::Time::Advance()`, making Pause/Step/Resume real.
- One implementation-time gotcha worth flagging explicitly for future
  phases doing a runtime smoke test after touching only `gte_core`/`Editor`
  files: always also rebuild the `GreatTamanaEngine` app target explicitly
  (not just `GreatTamanaEngineTests`) before launching it for a visual
  check — a `GreatTamanaEngineTests`-only incremental build does not
  relink the separate main-app executable, so it's easy to accidentally
  screenshot a stale build. Not a bug in this campaign's own code — purely
  a build-invocation reminder for whoever runs the next phase's smoke test.
- No other surprises or deviations from the phase document's plan.

## Git

All new/modified files for this phase
(`src/Editor/EditorContext.h`, `src/Editor/PlaybackControls.h`,
`src/Editor/PlaybackControls.cpp`, `src/Editor/DockLayout.cpp`,
`src/Editor/EditorLayer.h`, `src/Editor/ImGuiEditorLayer.cpp`,
`src/Editor/NullEditorLayer.cpp`, `CMakeLists.txt`), plus this report, were
staged and committed together with a commit message referencing PHASE3.
