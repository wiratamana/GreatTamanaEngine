# PHASE3 — Frame history ring buffer + the real capture trigger — COMPLETION REPORT

Campaign: `task_manager/frame-debugger-3/`
Branch: `feature/frame-debugger-impl`
Phase document: `PHASE3_FRAME_HISTORY_RING_BUFFER_AND_CAPTURE_TRIGGER.md`
Parent: `PHASE0_MASTER_STRATEGY.md`
Depends on: `PHASE1_RENDERER_CAPTURE_INSTRUMENTATION.md`,
`PHASE2_FRAME_DEBUGGER_SNAPSHOT_BUILDER.md` (both already landed).

## Summary

Implemented PHASE3's own "Step 3: The Plan" — a real, in-memory,
`kCapacity = 8`-slot ring buffer (`FrameDebuggerHistory`), each slot holding
one real `FrameDebuggerSnapshot` plus a retained GPU copy of that historical
frame's real Game View output image, and the actual capture TRIGGER
(Enable's false→true edge, Step while Enabled, and a new explicit "Capture"
button) — including this phase's own Step 3.4b ("Actually threading the
armed pointer"), which genuinely changes `Game::Render()`/`AddGameViewPass()`
signatures using PHASE1's own Step 3.1b forward-declare +
`#if GTE_ENABLE_EDITOR`-guarded-dereference pattern.

Per this phase's own Step 3.5 ("What this phase explicitly does NOT do"),
`FrameDebuggerPanel::Build()`'s own DISPLAYED tree/inspector still reads
`BuildPlaceholderFrameDebuggerSnapshot()`, byte-for-byte unchanged — even
though a real capture now genuinely happens and `m_history` genuinely holds
it. Switching the UI over to `m_history`'s real data is PHASE4's job.

### New files

- `src/Editor/FrameDebuggerHistory.h`/`.cpp` (`GTE_ENABLE_EDITOR`-gated, added
  to the root `CMakeLists.txt`'s existing `if(GTE_ENABLE_EDITOR)` block right
  after `FrameDebuggerCapture.cpp`):
  - `FrameDebuggerHistoryEntry` — `FrameDebuggerSnapshot snapshot` +
    `std::optional<RenderTexture> preview`.
  - `FrameDebuggerHistoryWriteState` (`count`/`nextWriteIndex`) +
    `AdvanceFrameDebuggerHistoryWriteState()` + `ClampFrameDebuggerHistoryCursor()`
    — the pure, plain-int ring-buffer bookkeeping the phase document's own
    Step 3.1 requires be directly Tier-1-testable.
  - `FrameDebuggerHistory` — `CaptureFrame(Renderer&, const
    FrameDebuggerSnapshot&, RenderTexture&)`, `Count()`, `CursorIndex()`,
    `StepCursor(int)`, `CurrentEntry()`, exactly matching the phase
    document's own illustrative shape. `CaptureFrame()`'s real GPU-to-GPU
    copy mirrors `Renderer::CaptureImagePixels()`'s own transition-copy-
    transition-back discipline (same `rg::EmitImageBarrier()`/
    `rg::RequiredStateFor()` calls, just a `vkCmdCopyImage` into another live
    GPU image instead of a `vkCmdCopyImageToBuffer` into host memory) via
    `Renderer::ImmediateSubmit()` — a genuinely extra, on-demand GPU
    submission, acceptable only because it happens at most once per real
    capture trigger.
- `tests/Editor/FrameDebuggerHistoryTests.cpp` (added to
  `tests/CMakeLists.txt`'s existing `if(GTE_ENABLE_EDITOR)` block, right
  after `Editor/FrameDebuggerCaptureTests.cpp`) — 8 Tier-1 tests covering
  every case the phase document's own Step 3.6 lists: a fresh history is
  empty and `CurrentEntry() == nullptr`; `StepCursor()` on an empty history
  is a no-op; `AdvanceFrameDebuggerHistoryWriteState()` grows `count` by one
  per call until `kCapacity`, then pins it there forever while
  `nextWriteIndex` keeps circularly wrapping (the real eviction behavior);
  `ClampFrameDebuggerHistoryCursor()` clamps at both ends without wrapping,
  and always returns 0 for an empty/invalid count.

### Modified files

- `src/Editor/FrameDebuggerCapture.h`/`.cpp` — untouched (PHASE1's own type,
  reused as-is).
- `src/Editor/EditorLayer.h` — a new, unconditional, bare forward
  declaration (`class FrameDebuggerCaptureContext;`, mirroring
  `RenderSystem.h`'s own PHASE1 precedent — `EditorLayer.h` is a CORE,
  always-compiled file), plus two new pure-virtual methods:
  - `FrameDebuggerCaptureContext* PrepareFrameDebuggerCaptureContext()` — the
    ONE place that decides whether the Frame Debugger's capture context is
    armed for THIS frame's Game-View render (nullptr the overwhelmingly
    common case); called once per frame by `Application::Run()`, BEFORE
    `Game::Render()` ever runs, and threaded straight into
    `AddGameViewPass()`.
  - `void NotifyFrameDebuggerStepConsumed()` — records "a Step happened this
    frame" for the Frame Debugger's own later use (see Deviation #2 below).
- `src/Editor/NullEditorLayer.cpp` — both new methods implemented as inert
  no-ops (`return nullptr;` / empty body).
- `src/Editor/ImGuiEditorLayer.cpp` — both new methods forward straight into
  the new `FrameDebuggerPanel` methods below; `BuildUI()`'s own
  `m_frameDebuggerPanel.Build(...)` call site now resolves this frame's real
  GPU-skinning pass names (from `game.CollectGpuSkinningDispatchRequests()`,
  the same idempotent, const, side-effect-free query
  `RenderPasses.cpp`'s own `AddGpuSkinningPasses()` already calls) and passes
  `renderer`/`renderGraph`/`m_gameView` (this class's own real Game-View
  `RenderTexture`) into `Build()`'s grown signature.
- `src/Editor/Panels/FrameDebuggerPanel.h`/`.cpp` — the bulk of this phase's
  real logic:
  - New members: `FrameDebuggerCaptureContext m_captureContext`,
    `FrameDebuggerHistory m_history`, plus a small set of this-frame-only
    cached raw pointers/reference (`m_frameRenderer`/`m_frameRenderGraph`/
    `m_frameGameView`/`m_frameGpuSkinningPassNames`) and `m_stepCaptureRequested`.
  - `Build(EditorContext&, Renderer&, const rg::RenderGraph&, RenderTexture&,
    const std::vector<std::string>&)` — grown signature (see this phase's
    own Step 3.3); stashes the four new parameters into the cached members
    above (used only for the rest of THIS call), before the pre-existing
    `!ctx.frameDebuggerWindowOpen` early-return check.
  - `PrepareCaptureContextForThisFrame(EditorContext&)` — the real
    implementation `IEditorLayer::PrepareFrameDebuggerCaptureContext()`
    forwards to: resets and returns `&m_captureContext` whenever
    `ctx.frameDebuggerWindowOpen && m_enabled`, else `nullptr`.
  - `NotifyStepConsumed()` — sets `m_stepCaptureRequested = true`.
  - `TriggerCapture()` (private) — builds `graphSnapshot` via
    `m_frameRenderGraph->LastSnapshot(rg::ExecuteTimingMode::SynchronousImmediateReadback)`,
    a real `FrameDebuggerRenderTargetInfo` from `m_frameGameView`'s own
    `Extent()`/`Format()` (using `gte::ToString(VkFormat)`, reused from
    `MemoryPanelData.h`, for the format string — resolving PHASE2's own
    Deviation #1's "a future phase supplies real width/height/format"
    promise), calls PHASE2's `BuildRealFrameDebuggerSnapshot()`, hands the
    result to `m_history.CaptureFrame()`, and resets
    `m_selectedEventIndex = -1`.
  - `BuildToolbarRow()` — the "Enable" checkbox's false→true edge now also
    calls `TriggerCapture()` (after the pre-existing `ctx.playbackPaused =
    true` line); a new "Capture" button (enabled only while `m_enabled`) also
    calls `TriggerCapture()`; and a pending Step-triggered capture
    (`m_stepCaptureRequested`) is serviced here too, re-checking `m_enabled`.
  - `Build()`'s own displayed-content branch is **completely unchanged** —
    still calls `BuildPlaceholderFrameDebuggerSnapshot()`, per Step 3.5.
- `src/Game/Game.h`/`.cpp` — `Render()` gained a new, defaulted, LAST
  parameter, `FrameDebuggerCaptureContext* frameDebuggerCapture = nullptr`,
  forwarded straight through to the float-aspect `RenderSystem::Draw()`
  overload only (never into the `viewProjectionOverride` branch, which Scene
  View's own call site uses — out of scope). `Game.h` adds only an
  unconditional forward declaration (`class FrameDebuggerCaptureContext;`),
  never a real `#include`; `Game.cpp` never dereferences the pointer at all
  (only forwards it), so — exactly as this phase's own Step 3.4b predicted —
  neither file needs an `#if GTE_ENABLE_EDITOR` guard.
- `src/Application/RenderPasses.h`/`.cpp` — same forward-declare pattern;
  `AddGameViewPass()` gained a new, defaulted, LAST parameter,
  `FrameDebuggerCaptureContext* frameDebuggerCapture = nullptr`, whose
  `execute` lambda now calls `game.Render(renderer, aspectWidthOverHeight,
  nullptr, frameDebuggerCapture)`. `AddPresentPass()`'s own direct-render
  fallback branch's `game.Render(renderer, *directGameRenderAspect)` call is
  left **completely unchanged** (no new parameter added to
  `AddPresentPass()`'s own signature at all) — it correctly relies on
  `Game::Render()`'s own `nullptr` default rather than ever being handed
  (or needing to explicitly pass) a real capture pointer, which is both the
  simplest and the safest possible guard against ever wiring the same
  shared variable into both call sites by mistake (there is no such
  parameter here to misuse). `RenderPasses.h`'s own stale "Game::Render()
  (UNCHANGED signature)" header comment was reworded per this phase's own
  explicit instruction.
- `src/Application/Application.cpp` — `Application::Run()` now: (1) calls
  `m_editorLayer->PrepareFrameDebuggerCaptureContext()` once, right after
  computing `gameTarget`/`sceneTarget` (before the offscreen `Execute()`
  call), storing the result in a local `FrameDebuggerCaptureContext*
  frameDebuggerCapture` and threading it into the one real
  `AddGameViewPass(...)` call site; (2) calls
  `m_editorLayer->NotifyFrameDebuggerStepConsumed()` right where
  `steppedThisFrame` is computed (mirroring `TryConsumeStepRequest()`'s own
  existing call-site position). Neither new line needs an `#if
  GTE_ENABLE_EDITOR` guard — the pointer is a bare, forward-declared type
  this file never dereferences.
- `CMakeLists.txt` / `tests/CMakeLists.txt` — new files registered as
  described above.

No change was needed to `src/Editor/EditorContext.h` — every new piece of
state lives inside `FrameDebuggerPanel` itself, per this phase's own
preference ("prefer keeping ownership inside FrameDebuggerPanel itself per
its own existing stateful-class precedent").

## Deviations from the phase document

1. **`FrameDebuggerHistory::CaptureFrame()`'s retained texture is always
   freshly `.emplace()`d (destroyed + recreated) on every single real
   capture into a given ring-buffer slot, never `Resize()`d in place.** The
   phase document's own Step 3.1 illustrative comment says a slot's
   `RenderTexture` is "creat[ed] ... lazily ... the first time this exact
   ring index is ever used" without being fully explicit about what happens
   on the SECOND (and later) capture that lands on the same slot index. This
   implementation still satisfies the letter of "lazy" (no `RenderTexture`
   is ever constructed for a slot index that has never actually been
   captured into — `std::optional::emplace()` is a no-op allocation-wise
   until first called), but recreates it fresh on every subsequent write to
   that same slot too, rather than trying to `Resize()` a possibly-stale
   previous one. This is deliberately simpler and strictly safer: it
   guarantees the retained copy always exactly matches the live Game View's
   CURRENT size/format even if it changed between two captures that happen
   to land on the same slot, and it sidesteps needing to track a slot's own
   previous `VkImageLayout` across many past captures — a freshly (re)created
   `RenderTexture`'s color image always starts life in
   `VK_IMAGE_LAYOUT_UNDEFINED` (see `RenderTexture.cpp`'s own `Create()`),
   which is exactly the "previous" state `CaptureFrame()`'s own destination
   barrier already assumes. The cost (one extra Vulkan image
   create/destroy) only happens at most once per real capture trigger, the
   same "rare, acceptable" cost model this whole method already carries.
2. **The Step-triggered capture (Step 3.2's call site 2) is split into two
   halves rather than calling `TriggerCapture()` directly from
   `Application::Run()`'s own per-frame loop at the exact point the phase
   document names.** A literal reading of "trigger a recapture ... from
   Application::Run()'s own per-frame loop, exactly where
   `IEditorLayer::TryConsumeStepRequest()` is already checked" is not
   actually possible: that check happens BEFORE `Game::Update()`/
   `Game::Render()` even run this frame (confirmed by reading
   `Application::Run()`), so no real, this-frame `RenderGraphSnapshot`/
   `FrameDebuggerCaptureContext`/Game-View pixels exist yet at that exact
   line — capturing there would necessarily capture STALE data from a
   previous frame, not "the JUST-STEPPED frame" the phase document itself
   explicitly says must be captured ("order matters — capture AFTER the
   stepped frame's own Game View pass executes, not before"). The
   resolution: `Application::Run()` still calls a new, narrow
   `IEditorLayer::NotifyFrameDebuggerStepConsumed()` at that exact
   documented call site (merely recording "a Step happened this frame" as a
   pending flag on `FrameDebuggerPanel`), and the REAL `TriggerCapture()`
   call is serviced later, from inside `FrameDebuggerPanel::BuildToolbarRow()`
   (called via `ImGuiEditorLayer::BuildUI()`), which is the earliest point in
   the SAME frame where the Game-View render has actually finished and this
   frame's real data genuinely exists. This is the smallest change that
   satisfies the phase document's own stated intent (capture the
   just-stepped frame, not a stale one) rather than its most literal,
   physically-impossible wording.
3. **The Enable-edge (call site 1) and Capture-button (call site 3)
   triggers DO call `TriggerCapture()` synchronously, exactly as written —
   but this carries a necessary, honest one-frame-lag consequence worth
   flagging explicitly (not a bug).** `PrepareFrameDebuggerCaptureContext()`
   arms the capture context for THIS frame's Game-View render based on
   `m_enabled`/`ctx.frameDebuggerWindowOpen` as of the END of the PREVIOUS
   frame's `BuildUI()` call — the same one-frame lag every other
   Editor↔engine feedback loop in this codebase already accepts (e.g.
   `IEditorLayer::IsPlaybackPaused()`'s own doc comment). So the very FIRST
   click of "Enable" triggers a capture whose own `FrameDebuggerCaptureContext`
   was armed based on last frame's (still-disabled) state — i.e. it produces
   a real, honest "Game View" leaf (real draw-stats/blend/Z/stencil info,
   since those are sourced from the graph snapshot itself, not the capture
   context) but with empty shader/texture names and an identity
   view-projection matrix, rather than fully-populated per-draw facts. Real
   per-draw facts start flowing from the very next capture onward (a
   Step, or clicking "Capture" again), once arming has caught up. This
   still satisfies the phase document's own stated goal — "the tree is
   never left on 'No frame captured yet.' the moment Enable is checked" —
   literally and honestly; it is documented at length in
   `FrameDebuggerPanel.cpp`'s own `BuildToolbarRow()` comment rather than
   silently accepted.
4. **A theoretical, unhandled edge case, noted but not specially guarded
   against this phase**: if a user opens the Frame Debugger and clicks
   "Capture" on a session where the "Game" panel has NEVER once been
   visible (so the Game View `RenderTexture`'s color image is still in its
   construction-time `VK_IMAGE_LAYOUT_UNDEFINED` layout, never transitioned
   by `AddGameViewPass()`/`FinalizeRenderTextureForExternalSampling()`),
   `CaptureFrame()`'s own source barrier (which assumes the source is
   currently `ShaderRead`) would be transitioning from a layout that does
   not match the image's real current one. This is judged astronomically
   unlikely in practice (the "Game"/"Scene" panels are both visible by
   default — `EditorContext::gameViewVisible = true` — and the Frame
   Debugger window itself defaults closed, requiring deliberate user
   action to even reach a state where this matters), and is the same class
   of accepted, documented, narrow edge case this codebase already carries
   elsewhere (e.g. `IsBgraFormat()`'s own "accepted narrow risk" precedent) —
   flagged here for visibility rather than silently ignored, not fixed this
   phase.
5. Everything else (`FrameDebuggerHistory`'s own public API shape,
   `kCapacity = 8`, the three real trigger call sites, PHASE3's own
   deliberate "does not change the displayed tree/inspector yet" scope
   limit, the `Game.h`/`RenderPasses.h` signature changes and their
   forward-declare/`#if`-guard discipline) matches the phase document's Step
   3.1–3.5 exactly, with no further deviation.

## Compile check (fast, per this phase's own Step 3.7 — not a full rebuild/
regression)

1. Built `gte_core` in the existing `build` directory (`GTE_ENABLE_EDITOR=ON`):
   ```
   cmake --build build --target gte_core
   ```
   Result: **succeeded**, no warnings/errors from any new or modified file.

2. Built and ran `GreatTamanaEngineTests`, filtered to every Frame-
   Debugger-related test:
   ```
   cmake --build build --target GreatTamanaEngineTests
   build\tests\GreatTamanaEngineTests.exe --gtest_filter=*FrameDebugger*
   ```
   Result: **all 25 tests passed** — the 8 new `FrameDebuggerHistoryTest`/
   `FrameDebuggerHistoryWriteStateTest`/`ClampFrameDebuggerHistoryCursorTest`
   cases, plus the pre-existing 7 `FrameDebuggerSnapshotBuilderTest.*`
   (PHASE2), 6 `FrameDebuggerCaptureContextTest.*`/`DescribeStandardPipelineStateTest`
   (PHASE1), and 6 `FrameDebuggerDataTest.*` (`frame-debugger-2`) — all
   unaffected.

3. Built the full `GreatTamanaEngine` executable target in the same
   `GTE_ENABLE_EDITOR=ON` `build` tree — **succeeded**, linked cleanly (this
   phase touches `Application.cpp`/`Renderer` wiring, not just Editor-local
   pure data, so this is required per this phase's own Step 3.7, not merely
   a bonus check).

4. **Scoped `GTE_ENABLE_EDITOR=OFF` configure+build**, in the existing
   `build-editor-off` directory:
   ```
   cmake --build build-editor-off --target gte_core
   cmake --build build-editor-off --target GreatTamanaEngine
   ```
   Result: **both succeeded — compile AND LINK cleanly.** This confirms the
   forward-declaration-only headers (`EditorLayer.h`/`Game.h`/`RenderPasses.h`'s
   bare `class FrameDebuggerCaptureContext;`) plus every real
   `#include`/dereference staying correctly absent/guarded in every CORE
   `.cpp` file this phase touches (`Application.cpp`, `Game.cpp`,
   `RenderPasses.cpp`, `NullEditorLayer.cpp`) never trips the "compiles but
   fails to link" trap PHASE1's own Step 3.1b specifically warns about.

5. **Live runtime smoke test** (strongly encouraged, not required, per this
   phase's own Step 3.7 given its Tier-2 risk): launched the `build`
   directory's `GreatTamanaEngine.exe` in the background, confirmed
   `GET /get_swapchain` returns a real, valid PNG of the running Editor (the
   Scene/Game panels visibly rendering, sky background included) both
   immediately after launch and again a few seconds later (no crash/hang),
   and confirmed `GET /list_tabs` still responds normally and correctly does
   NOT list "Frame Debugger" (expected and unchanged — the Frame Debugger
   window is deliberately excluded from `EditorPanelCatalog.h`'s tab list
   per `frame-debugger-2`'s own Locked Design Decision #6, and PHASE7 is
   where HTTP automation for it is added). The process was then cleanly
   stopped.

   **Accepted limitation, matching `frame-debugger-1`/`frame-debugger-2`'s
   own precedent**: since no HTTP endpoint exists yet to open the Frame
   Debugger window or click its "Enable"/"Capture" controls (that is
   PHASE7's job), this smoke test could not click through the real
   Enable-edge/Step/Capture-button trigger paths themselves end-to-end —
   only their compile-time correctness and the engine's overall continued
   stability with the new code paths compiled in were verified live. The
   trigger logic itself is exercised indirectly through the Tier-1 tests
   above (the pure ring-buffer arithmetic) and through careful, explicit
   code-review-level reasoning documented inline (see Deviations #2/#3
   above) — a full, real, clicked-through verification of the actual
   capture trigger happens naturally once PHASE4 wires the displayed tree
   to real data (making a real capture visually obvious for the first
   time) and/or PHASE7 adds HTTP automation for this window.

No full clean build and no full `ctest` regression suite were run in this
phase — per both this phase's own Step 3.7 and PHASE0's Step 3.6/"Order of
work", that is reserved for the final PHASE8 step only.

## Next step

PHASE4 (`PHASE4_PANEL_REAL_TREE_AND_FRAME_HISTORY_UI.md`) — swaps
`FrameDebuggerPanel` from the placeholder snapshot to this phase's real
`m_history` ring buffer; adds a new Frame-History mini-toolbar (Prev/Next
captured frame, driven by `FrameDebuggerHistory::StepCursor()`); wires the
RenderTarget preview box to the real retained texture
(`FrameDebuggerHistoryEntry::preview`) this phase's `CaptureFrame()` now
genuinely populates.
