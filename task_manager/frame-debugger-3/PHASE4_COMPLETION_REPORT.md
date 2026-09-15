# PHASE4 — Panel wiring: real tree, real selection, and the new Frame-History UI — COMPLETION REPORT

Campaign: `task_manager/frame-debugger-3/`
Branch: `feature/frame-debugger-impl`
Phase document: `PHASE4_PANEL_REAL_TREE_AND_FRAME_HISTORY_UI.md`
Parent: `PHASE0_MASTER_STRATEGY.md`
Depends on: `PHASE1`..`PHASE3` (already landed).

## Summary

Implemented PHASE4's own "Step 3: The Plan" in full: the Frame Debugger
window now shows something REAL for the first time in this campaign.

- `FrameDebuggerPanel::Build()` no longer unconditionally calls
  `BuildPlaceholderFrameDebuggerSnapshot()` for its displayed content — it
  reads `m_history.CurrentEntry()` (PHASE3's real ring buffer) and uses that
  entry's own real `snapshot`, falling back to the exact same placeholder's
  empty-tree behavior only when no real capture has ever happened yet this
  session (`CurrentEntry() == nullptr`) — a real, honest, reachable
  "enabled, but not yet captured" state, not a bug.
- A brand-new Frame-History mini-toolbar (`BuildFrameHistoryToolbarRow()`)
  was added, with "<"/">" arrow buttons calling
  `m_history.StepCursor(-1)`/`StepCursor(+1)`, a new pure label helper
  (`FormatFrameHistoryLabel()`, `FrameDebuggerData.h/.cpp`, Tier-1-tested),
  and both buttons correctly disabled at `Count() == 0` or at either cursor
  edge.
- The RenderTarget preview box now displays the currently-viewed history
  entry's real retained preview texture via `ImGui::Image()` instead of the
  "No Texture" placeholder, whenever one exists and the selected event isn't
  a GPU-skinning leaf (see "Deviation #1" below) — backed by a NEW ImGui
  descriptor (`m_previewDescriptor`) this class now owns and manages itself.
- The pre-existing "N of M" event-stepper row (`BuildFrameStepperRow()`) now
  shows real numbers (`FormatFrameStepperLabel(m_selectedEventIndex,
  snapshot.totalEventCount)`) instead of the permanent "0 of 0" — kept as a
  completely SEPARATE control from the new Frame-History toolbar, per Locked
  Design Decision #4.
- The resolution/format caption row is now real, reading
  `snapshot.renderTarget` (already populated for real by PHASE2/PHASE3).

### This phase's own Step 2 finding — the ownership decision (documented, as required)

Per this phase's own note, `ImGui_ImplVulkan_AddTexture()`/
`ImGui_ImplVulkan_RemoveTexture()` do NOT live in `Panels/GamePanel.cpp` —
that file only ever consumes an ALREADY-WRAPPED `VkDescriptorSet`
(`EditorContext::gameViewDescriptor`); the actual wrapping call, plus the
"only re-wrap when the underlying `VkImageView` actually changed" caching
logic, lives in `ImGuiEditorLayer.cpp`'s own `BuildUI()`. Both files were
read before implementing, as instructed.

**Decision: the new preview descriptor is owned by `FrameDebuggerPanel`
itself, not by `ImGuiEditorLayer`.** Rationale (documented in
`FrameDebuggerPanel.h`'s own `EnsurePreviewDescriptor()` doc comment too):

- `FrameDebuggerPanel` is already a stateful class that owns comparable
  per-frame GPU-adjacent state (`m_captureContext`, `m_history` — which
  itself owns real `RenderTexture`s) — unlike `GamePanel`/`ScenePanel`
  (stateless free functions), it has an established precedent for owning
  this kind of thing.
- A second, closer precedent exists in this exact codebase:
  `BoneViewerWindow` (`src/Editor/BoneViewerWindow.h/.cpp`) already owns its
  own `m_descriptor`/`m_renderTexture` pair entirely self-contained,
  including its own `#include <backends/imgui_impl_vulkan.h>`, its own
  `ImGui_ImplVulkan_AddTexture()`/`RemoveTexture()` calls, and its own
  explicit "release before `ImGui_ImplVulkan_Shutdown()`" contract called
  from `ImGuiEditorLayer`'s destructor. `FrameDebuggerPanel` now mirrors this
  pattern exactly.
- The alternative (owning it in `ImGuiEditorLayer`, alongside
  `gameViewDescriptor`/`sceneViewDescriptor`) would have required a THIRD
  descriptor field on the shared `EditorContext`, plus a new getter out of
  `FrameDebuggerHistory` for `ImGuiEditorLayer::BuildUI()` to poll every
  frame just to decide whether/what to re-wrap, and a way to hand the
  resulting descriptor back into `FrameDebuggerPanel::BuildInspectorPane()`
  — strictly more moving parts, more files touched, and more indirection for
  no benefit, since nothing outside `FrameDebuggerPanel` itself ever needs
  this descriptor.

Concretely: `FrameDebuggerPanel` gained a new `EnsurePreviewDescriptor()`
(private) method — called once per `Build()` call, cheap/no-op unless the
currently-viewed history entry's own retained texture's `VkImageView`
actually changed (mirrors `ImGuiEditorLayer::BuildUI()`'s own
`gameViewDescriptor`/`sceneViewDescriptor` re-wrap-on-change logic exactly,
just against PHASE3's retained HISTORICAL copy instead of the live Game
View) — plus a new public `ReleasePreviewDescriptor()` method and an
explicit `~FrameDebuggerPanel()` destructor. `ImGuiEditorLayer`'s own
destructor now calls `m_frameDebuggerPanel.ReleasePreviewDescriptor()`
explicitly, right alongside `ReleaseGameViewDescriptor()`/
`ReleaseSceneViewDescriptor()`/`ReleaseBlurredSceneOutputDescriptor()`,
BEFORE `ImGui_ImplVulkan_Shutdown()` runs — required because
`FrameDebuggerPanel` is declared (and therefore destroyed, in reverse
member-declaration order) AFTER `m_context`, so `ImGuiEditorLayer`'s
explicit destructor BODY (which calls `ImGui_ImplVulkan_Shutdown()`) always
runs before `FrameDebuggerPanel`'s own destructor would — the exact same
requirement `AssetPreviewMesh`/`AssetPreviewTexture`/`BoneViewerWindow`
already have, and the exact same fix shape.

## Deviations from the phase document

1. **The RenderTarget preview box's "show the real texture" rule is more
   precise than a literal reading of Step 3.3 alone might suggest — it
   checks the currently-SELECTED event, not just whether a history entry
   exists.** Locked Design Decision #5 (`PHASE0_MASTER_STRATEGY.md`) is
   explicit: selecting the `"GameView"` leaf shows the real retained
   texture; selecting a GPU-skinning leaf shows "No Texture" (honest — a
   compute pass has no color image). Step 3.3's own wording ("if
   `m_history.CurrentEntry()` is non-null AND its `preview` has a value,
   display it... keep the... placeholder... for every other case (no entry
   yet, or a GPU-skinning leaf selected...)") already anticipates this, but
   phrases it as two disjoint "other cases" rather than spelling out the
   exact boolean condition. Implemented literally: `showPreviewTexture =
   (m_previewDescriptor != VK_NULL_HANDLE) && (currentEntry != nullptr) &&
   !(the selected event's own `details->passName == "GPU Skinning"`)` — the
   preview is otherwise a FRAME-level (not event-level) thing, shown
   regardless of whether anything is selected at all, or whether the
   `"GameView"` leaf specifically is selected, matching the RenderTarget
   row's own pre-existing "frame-level, not event-level" comment
   immediately above it in `BuildInspectorPane()`.
2. **A new, small piece of state-management logic was added that the phase
   document does not explicitly mention: `m_selectedEventIndex` is now reset
   to `-1` whenever the VIEWED history entry itself changes** (a
   Frame-History Prev/Next navigation) **, not just when a brand-new capture
   happens** (which `TriggerCapture()` already handled). Rationale: leaf
   `eventIndex` values are only meaningful relative to the specific snapshot
   they were assigned in (`BuildRealFrameDebuggerSnapshot()` numbers them
   `0, 1, 2, ...` per capture) — carrying a selection over to a DIFFERENT
   captured frame's tree (which may have a different shape, e.g. a
   different number of GPU-skinning leaves that frame) could otherwise
   highlight/describe a completely unrelated event purely by index
   coincidence. Detected via comparing the entry's own retained preview
   `VkImageView` before/after `EnsurePreviewDescriptor()` runs (guaranteed
   fresh on every real capture — see `FrameDebuggerHistory::CaptureFrame()`'s
   own doc comment), so this piggybacks on state PHASE4 already needed to
   track, at zero extra bookkeeping cost. This is judged a straightforward,
   clearly-correct engineering improvement squarely within this phase's own
   scope (real tree/selection wiring), not a deviation from any Locked
   Design Decision.
3. **The event-stepper row's slider itself stays cosmetic/disabled** (its
   `min`/`max` were updated to `0`/`totalEventCount - 1` for visual honesty,
   but it remains `ImGui::BeginDisabled()`-wrapped, exactly as before) —
   Step 3 only asked for the row to "show real numbers", not to become an
   interactive alternate way to change the selection; clicking a tree row
   (`RenderEventNode()`) remains the only way to select an event this
   campaign, unchanged from `frame-debugger-2`.
4. Everything else — the snapshot-source swap (Step 3.1), the new
   Frame-History toolbar's shape/placement (Step 3.2, placed directly after
   the existing toolbar row and before the event-stepper row, per the phase
   document's own suggested placement), the preview-descriptor ownership
   choice and its documentation (Step 2's own explicit ask), and this
   phase's explicit non-goals (Step 3.4 — Channels/Levels processing,
   `BuildEventDetailsSection()` polish, HTTP endpoints, all untouched) —
   matches the phase document exactly, with no further deviation.

## File-change inventory

Modified:
- `src/Editor/Panels/FrameDebuggerPanel.h` — new `~FrameDebuggerPanel()`,
  `ReleasePreviewDescriptor()` (public), `EnsurePreviewDescriptor()`,
  `BuildFrameHistoryToolbarRow()` (private); `BuildFrameStepperRow()`/
  `BuildInspectorPane()` signatures grew a `const FrameDebuggerSnapshot&`/
  `const FrameDebuggerHistoryEntry*` parameter respectively; new members
  `m_previewDescriptor`, `m_lastKnownPreviewView`, `m_device`; explicit
  `#include <volk.h>` (already transitively visible, added for clarity,
  mirroring `BoneViewerWindow.h`'s own explicit include); copy
  constructor/assignment explicitly deleted.
- `src/Editor/Panels/FrameDebuggerPanel.cpp` — the bulk of this phase's
  logic (see Summary above); new `#include <backends/imgui_impl_vulkan.h>`
  and `#include "../../Renderer/Renderer.h"` (for `GetVulkanContextInfo()`/
  `vkDeviceWaitIdle`).
- `src/Editor/FrameDebuggerData.h`/`.cpp` — new `FormatFrameHistoryLabel()`
  pure helper, alongside `FormatFrameStepperLabel()`.
- `src/Editor/ImGuiEditorLayer.cpp` — destructor now calls
  `m_frameDebuggerPanel.ReleasePreviewDescriptor()` explicitly, right
  alongside the existing `Release*Descriptor()` calls, before
  `ImGui_ImplVulkan_Shutdown()`; `m_frameDebuggerPanel`'s own member comment
  updated to note the new ownership.
- `tests/Editor/FrameDebuggerDataTests.cpp` — new
  `FormatFrameHistoryLabelTest` case.

No new files were needed this phase (no `CMakeLists.txt`/
`tests/CMakeLists.txt` changes required).

## Compile check + live smoke test (per this phase's own Step 3.5)

1. Fast, scoped compile check, `GTE_ENABLE_EDITOR=ON`, existing `build`
   directory:
   ```
   cmake --build build --target gte_core
   ```
   Result: **succeeded**, no warnings/errors from any new or modified file.

2. Built and ran every Frame-Debugger-related test:
   ```
   cmake --build build --target GreatTamanaEngineTests
   build\tests\GreatTamanaEngineTests.exe --gtest_filter=*FrameDebugger*
   ```
   Result: **all 26 tests passed** — the new `FormatFrameHistoryLabelTest`
   plus every pre-existing `FrameDebuggerDataTest`/
   `FrameDebuggerSnapshotBuilderTest`/`FrameDebuggerHistoryTest`/
   `FrameDebuggerHistoryWriteStateTest`/`ClampFrameDebuggerHistoryCursorTest`/
   `FrameDebuggerCaptureContextTest` case, all unaffected.

3. Built the full `GreatTamanaEngine` executable target in the same
   `GTE_ENABLE_EDITOR=ON` `build` tree — **succeeded**, linked cleanly (this
   phase touches `ImGuiEditorLayer.cpp`, not just Editor-local pure data).

4. **Scoped `GTE_ENABLE_EDITOR=OFF` build**, existing `build-editor-off`
   directory:
   ```
   cmake --build build-editor-off --target gte_core
   cmake --build build-editor-off --target GreatTamanaEngine
   ```
   Result: **"ninja: no work to do"** for both — confirms every file this
   phase touched (`FrameDebuggerPanel.h/.cpp`, `FrameDebuggerData.h/.cpp`,
   `ImGuiEditorLayer.cpp`) is entirely `GTE_ENABLE_EDITOR`-gated and never
   participates in that configuration's build at all, exactly as expected.

5. **Live runtime smoke test**: launched `build\GreatTamanaEngine.exe` in
   the background.
   - `GET /get_swapchain` returned a real, valid PNG of the running Editor
     (Scene/Game panels visibly rendering, sky background included),
     confirmed both immediately after launch and again a few seconds later
     (no crash/hang, no visible regression to any existing panel).
   - `GET /list_tabs` returned the expected, unchanged panel list (still
     correctly excluding "Frame Debugger" — unchanged from PHASE3, expected,
     since HTTP automation for this window is PHASE7's job).
   - The process was then cleanly stopped.

   **Accepted limitation, matching PHASE1/PHASE2/PHASE3's own identical
   precedent**: this environment has no mouse-driven UI automation
   available, and no HTTP endpoint yet exists (that's PHASE7) to open the
   Frame Debugger window, check "Enable"/click "Capture", or drag the
   floating window onto the main viewport so `GET /get_swapchain` could see
   it. This phase's own Step 3.5 explicitly anticipates and permits this
   ("If this manual step is impractical in this environment, defer full
   visual confirmation to PHASE8's automation-driven pass, but still confirm
   a clean, crash-free build/launch here.") — done above. The real
   correctness of the new tree/selection/preview-texture wiring is instead
   backed by: the passing Tier-1 test suite (unchanged from PHASE3, since
   this phase's own new logic — `FormatFrameHistoryLabel()` — is itself
   Tier-1-tested); careful, explicit code-review-level tracing of
   `Build()`'s new control flow against every one of PHASE0's Locked Design
   Decisions (documented inline in `FrameDebuggerPanel.cpp`'s own comments);
   and the four clean compiles above (`ON`/`OFF`, `gte_core`, full
   executable, test binary) with zero warnings. A full, real, clicked-through
   (or HTTP-automation-driven) visual verification — actually seeing a
   non-empty tree and a real preview image on screen — happens naturally
   once PHASE7 adds `/frame_debugger/*` HTTP automation and the
   main-viewport pin, and/or PHASE8's own final integration pass.

No full clean build and no full `ctest` regression suite were run in this
phase — per both this phase's own Step 3.5 and `PHASE0_MASTER_STRATEGY.md`'s
Step 3.6/"Order of work", that is reserved for the final PHASE8 step only.

## Next step

PHASE5 (`PHASE5_EVENT_DETAILS_REAL_DATA_WIRING.md`) — verifies/finishes the
event-details section against real, non-null data now that
`BuildEventDetailsSection()` is reachable in practice for the first time
(a real capture now genuinely populates `FrameDebuggerEventNode::details`,
and this phase's own real tree/selection wiring makes it possible to
actually select one).
