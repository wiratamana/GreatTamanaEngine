# PHASE1 — Dual-Stage Retained Capture + Composite-Aware Preview Selection — COMPLETION REPORT

Campaign: `task_manager/frame-debugger-4/`
Branch: `feature/frame-debugger-impl`
Phase document: `PHASE1_DUAL_STAGE_RETAINED_CAPTURE_AND_PREVIEW_SELECTION.md`
Parent: `PHASE0_MASTER_STRATEGY.md`
Also read: `PHASE1_DOUBLE_CHECK_NOTES.md`, `PHASE0_DOUBLE_CHECK_NOTES.md` (both already folded into the
phase document itself before implementation started — no further clarification was needed from either at
implementation time).

## Summary

Implemented PHASE1's "Step 3: The Plan" exactly as written, in full: `FrameDebuggerHistory` now retains TWO
real images per captured frame (the true pre-atmosphere-composite `"GameView"` copy, unchanged, plus a NEW
true post-atmosphere-composite `"GameViewComposited"` copy), wired end-to-end from
`ImGuiEditorLayer::BuildUI()` down to `FrameDebuggerHistory::CaptureFrame()`, and
`FrameDebuggerPanel::EnsurePreviewDescriptor()` now picks the right one to display based on which tree event
is currently selected — the literal `"GameView"` leaf shows the true pre-composite image; anything else
(including nothing selected) shows the true final, atmosphere-inclusive image. This closes this campaign's
primary bug: simply enabling the Frame Debugger and capturing a frame — with no further UI interaction —
now already shows the atmosphere-scattering/aerial-perspective effect in the preview.

## Files changed

- `src/Editor/FrameDebuggerHistory.h`
  - `FrameDebuggerHistoryEntry` gained a new field, `std::optional<RenderTexture> compositedPreview`,
    directly below `preview`, with the exact field-level doc comment the phase document specifies
    (`std::nullopt` is a real, honest "not available for this particular captured frame" state, and — unlike
    `preview` — can legitimately go back to `std::nullopt` on a later capture into the same slot).
  - The struct's own top-of-file summary comment was extended with one more sentence pointing out that
    `compositedPreview` follows a different nullability rule than `preview`, exactly as PHASE1's own
    Step 3.1 (folded in from `PHASE1_DOUBLE_CHECK_NOTES.md`) instructs.
  - `CaptureFrame()`'s declaration grew a new, non-defaulted last parameter,
    `RenderTexture* compositedGameViewSource`, with its own doc comment describing the nullable contract.
- `src/Editor/FrameDebuggerHistory.cpp`
  - `CaptureFrame()`'s body now does the pre-composite copy exactly as before (byte-for-byte unchanged
    behavior for `entry.preview`), plus — only when `compositedGameViewSource != nullptr` — a second,
    freshly `.emplace()`d `entry.compositedPreview` created from the COMPOSITED source's own
    extent/format (never assumed to match `gameViewSource`'s own extent), with a distinct
    `"...Composited"` debug-name suffix. Both the pre-composite and (when present) post-composite
    barrier/copy sequences run inside the SAME `renderer.ImmediateSubmit()` lambda call (Locked Design
    Decision #8) — one GPU submission + fence wait for the whole `CaptureFrame()` call, not two. When
    `compositedGameViewSource == nullptr`, `entry.compositedPreview.reset()` is called explicitly so a
    slot that previously had a composited copy correctly reverts to `std::nullopt` on a capture that
    doesn't have one (the "not a one-way ratchet" rule the header comment documents).
- `src/Editor/Panels/FrameDebuggerPanel.h`
  - New member `RenderTexture* m_frameGameViewComposited = nullptr;` directly below `m_frameGameView`.
  - `Build()`'s declaration grew a new parameter, `RenderTexture* compositedGameView`, inserted between
    `gameView` and `gpuSkinningPassNamesThisFrame`, with the doc comment the phase document specifies.
- `src/Editor/Panels/FrameDebuggerPanel.cpp`
  - `Build()`'s body now also sets `m_frameGameViewComposited = compositedGameView;` alongside the
    existing `m_frameGameView = &gameView;` line.
  - `TriggerCapture()`'s one call site now reads
    `m_history.CaptureFrame(*m_frameRenderer, snapshot, *m_frameGameView, m_frameGameViewComposited);`
    — `renderTargetInfo` continues to be derived from `m_frameGameView` only, exactly as the phase
    document requires (left completely untouched).
  - `EnsurePreviewDescriptor()` was rewritten per the phase document's own Step 3.5: it now first resolves
    a `const RenderTexture* selectedSource` by checking whether the currently-selected event's
    `details->passName == "GameView"` (via the existing `FindEventDetailsByIndex()`) — if so, uses
    `entry->preview`; otherwise (including nothing selected) prefers `entry->compositedPreview`, falling
    back to `entry->preview` only when `compositedPreview` is `std::nullopt` for that captured frame. The
    rest of the function (the Channels/Levels neutral-check/dirty-check/`ImGui_ImplVulkan_AddTexture()`
    dispatch logic) is otherwise byte-for-byte the same as before, just reading from `*selectedSource`
    instead of unconditionally from `entry->preview`. `Build()`'s own "did the viewed history entry change
    underneath us" reset block (comparing `m_lastKnownRawPreviewView` against `currentEntry->preview`) and
    `BuildInspectorPane()`'s own `selectedEventIsGpuSkinning`/`showPreviewTexture` logic were both left
    completely untouched, exactly as instructed.
- `src/Editor/ImGuiEditorLayer.cpp`
  - The one `m_frameDebuggerPanel.Build(...)` call site (inside `BuildUI()`) now passes
    `m_gameViewComposited` (the class's own existing, already-populated-elsewhere `RenderTexture*` member)
    as the new second-to-last argument, alongside a new explanatory comment mirroring the phase document's
    own Step 3.6 text.
- `src/Editor/NullEditorLayer.cpp` — confirmed unchanged, as the phase document predicts:
  `NullEditorLayer::BuildUI()` is still a complete no-op and never references `FrameDebuggerPanel` at all
  (that class doesn't exist under `GTE_ENABLE_EDITOR=OFF`).

No new files were added or removed this phase — every change is a surgical extension of existing
`frame-debugger-3` files, exactly as `PHASE0_MASTER_STRATEGY.md`'s own file-change inventory predicts.

## Deviations from the phase document

None. Every function signature, code sketch, barrier/copy sequence, and picking-rule detail in the phase
document (already reviewed twice — `PHASE1_DOUBLE_CHECK_NOTES.md` and the campaign-wide
`PHASE0_DOUBLE_CHECK_NOTES.md`) matched the live source tree exactly at implementation time, and was
implemented as written with no functional changes needed. The only genuinely new content added beyond a
literal copy of the phase document's own code sketches was routine, mechanical (comment text/parameter
ordering already fully specified by the document itself).

One small process note, not a deviation from the plan's substance: while editing
`src/Editor/Panels/FrameDebuggerPanel.h`, an early `edit_line` call for the `Build()` signature change
accidentally over-specified its `length` parameter and deleted a much larger block of the file than
intended (everything from `PrepareCaptureContextForThisFrame()` down through `ApplyEnabledEdge()`'s
declaration, including every PHASE7 HTTP-automation method declaration and the `private:` section header).
This was caught immediately by re-reading the file in full right after the edit, and fully restored,
verbatim, via a follow-up `edit_line` call before any further edits were made — the final file (confirmed by
a full `read_file` afterward, and by the clean compile below) contains no lost content and no duplication.
This is flagged here for transparency since it happened mid-implementation, even though the end state is
identical to what a clean, single-shot edit would have produced.

## Compile check (fast, per this phase's own Step 3.8 — not a full rebuild/regression)

```
cmake --build build --target GreatTamanaEngine
cmake --build build --target GreatTamanaEngineTests
```

Both succeeded with zero errors/warnings from any new or modified file:

- `GreatTamanaEngine`: rebuilt `FrameDebuggerHistory.cpp.obj`, `Panels/FrameDebuggerPanel.cpp.obj`,
  `ImGuiEditorLayer.cpp.obj`, relinked `libgte_core.a` and `GreatTamanaEngine.exe` cleanly.
- `GreatTamanaEngineTests`: rebuilt `Editor/FrameDebuggerHistoryTests.cpp.obj` (the signature change to
  `FrameDebuggerHistory::CaptureFrame()` does not affect this test file at all — it only exercises the pure
  `AdvanceFrameDebuggerHistoryWriteState()`/`ClampFrameDebuggerHistoryCursor()` free functions, confirmed by
  both `PHASE1_DOUBLE_CHECK_NOTES.md` and this phase's own live compile) and relinked
  `GreatTamanaEngineTests.exe` cleanly.

Per this phase's own Step 3.8, the full test suite (`ctest`) and a full clean build were deliberately NOT
run — that is reserved for PHASE3.

## Live manual smoke check (strongly recommended, performed this phase given PHASE1's flagged highest-risk
status)

Launched `build\GreatTamanaEngine.exe` in the background and drove it entirely over the embedded HTTP
server (`GET /frame_debugger/*`, `GET /get_swapchain`, `GET /get_game_view`, `POST /instantiate_primitive`):

1. `GET /frame_debugger/open` → window opened.
2. `GET /frame_debugger/enable?value=true` → `historyCount` went from 0 to 1 (the Enable-edge auto-capture
   already present since `frame-debugger-3`).
3. Compared `GET /get_swapchain` (the Frame Debugger's own preview box, nothing selected) against
   `GET /get_game_view` (the real final Game View) on the default, geometry-free scene — both showed the
   identical sky gradient, confirming the DEFAULT preview is now genuinely sourced from the same final,
   composited output the "Game" panel/`GET /get_game_view` show (Root Cause A from
   `PHASE0_MASTER_STRATEGY.md`'s Step 2.2 is fixed).
4. `POST /instantiate_primitive` spawned a grey test cube 80 units in front of the camera (never touching
   Scene-View capture, per this campaign's own Non-Goal), then `GET /frame_debugger/capture` took a fresh
   capture (`historyCount` → 2). `GET /get_swapchain` (default view) and `GET /get_game_view` again matched
   pixel-for-pixel in composition (same cube, same position, same coloring) — confirming the dual-copy wiring
   also works correctly once real, non-sky geometry exists in the frame, not just on an empty scene.
5. `GET /frame_debugger/select_event?index=0` selected the `"GameView"` leaf explicitly. The Inspector's own
   "Pass" field correctly read `"GameView"`, `"Shader"` correctly read the real
   `"Triangle.vert/Triangle.frag (PositionColor)"` value for the test cube's own draw call, and the preview
   box continued to render without error — confirming the picking rule's `"GameView"`-leaf branch is
   reachable and does not crash/misbehave when explicitly selected (directly exercising Step 3.8's second
   suggested edge case). Given this scene's modest 80-unit distance and default atmosphere tuning, the
   pre-/post-composite visual difference for this specific cube was subtle to the eye at the panel's small
   preview resolution — the WIRING correctness (both textures populated, selection correctly switches which
   one the descriptor wraps, no crash on switching) was the property actually being verified here, exactly
   as the phase document's own Step 3.8 frames this recommended check (a full, dramatic visual "before vs.
   after" comparison of the whole feature is PHASE3's own live-verification job, once PHASE2's new
   `"Aerial Perspective Composite"` leaf and PHASE3's doc updates have also landed).
6. `GET /frame_debugger/select_event?index=-1` and further capture/selection round-trips produced no crash,
   hang, or error response at any point across the whole session.
7. The very-first-capture-before-any-composite-exists path (`compositedGameViewSource == nullptr`) was, as
   the phase document itself predicts, not independently reproduced live (the composited texture already
   existed from essentially the first rendered frame in this session) — this path was instead verified by
   code review: `hasCompositedSource` is checked before every dereference of `compositedGameViewSource` in
   `CaptureFrame()`, and `EnsurePreviewDescriptor()`'s own `else if (entry->compositedPreview.has_value())`
   /`else if (entry->preview.has_value())` chain never dereferences a `std::nullopt` optional.

The resize-mid-session edge case (Step 3.8's first suggested check) was not separately exercised live this
pass (no convenient way to resize the "Game" panel's own content region purely over HTTP) — `CaptureFrame()`
independently re-reads `compositedGameViewSource->Extent()` both when creating `entry.compositedPreview` and
again inside the `ImmediateSubmit()` lambda (never assuming it matches `gameViewSource`'s own extent), which
was confirmed correct by both `PHASE1_DOUBLE_CHECK_NOTES.md`'s own review and a direct re-read of the final
code during this phase's own implementation.

The background engine process was cleanly stopped after this smoke check.

## Next step

PHASE2 (`PHASE2_AERIAL_PERSPECTIVE_COMPOSITE_EVENT_TREE_LEAF.md`) — makes the already-real
`"AtmosphereAerialPerspectiveCompositePass"` render-graph pass visible in the event tree as a new leaf
sibling of `"GameView"`, requiring zero further changes to this phase's own picking logic in
`EnsurePreviewDescriptor()` (already generalized to treat "anything other than the literal `"GameView"`
leaf" as the composited-preview case).
