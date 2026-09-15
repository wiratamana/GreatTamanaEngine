# PHASE3 — Frame history ring buffer + the real capture trigger

## Parent -> `PHASE0_MASTER_STRATEGY.md` (READ THIS FIRST — Locked Design Decisions #2, #3, #5).
## Depends on: `PHASE1_RENDERER_CAPTURE_INSTRUMENTATION.md`,
`PHASE2_FRAME_DEBUGGER_SNAPSHOT_BUILDER.md` (already landed).

## Step 1: The Goal

Give the Frame Debugger a real, in-memory, multi-slot history of past captured frames — each slot
holding one real `FrameDebuggerSnapshot` (PHASE2) plus a retained GPU copy of that historical
frame's real Game View output image — and wire the actual TRIGGER that decides exactly when a new
capture happens: the "Enable" checkbox's false->true edge, the existing "Step" action (while
Enabled), and a new explicit "Capture" button.

## Step 2: The Situation

- `FrameDebuggerPanel::m_enabled` (`src/Editor/Panels/FrameDebuggerPanel.h`) already exists and
  already engages `ctx.playbackPaused = true` on its own false->true edge (Locked Design
  Decision #3 from `frame-debugger-2`, unchanged) — this phase adds a SECOND effect to that exact
  same edge: trigger the first real capture.
- `EditorContext::stepOneFrameRequested`/`IEditorLayer::TryConsumeStepRequest()`
  (`docs/conventions/time-and-playback-pause.md`) is how "Step" already works — this phase does
  not change Step's own simulation-advancing behavior at all; it only ADDS a second effect
  (auto-recapture) observed from `Application::Run()`'s own per-frame loop, exactly the same place
  `Time::Advance()`/`Game::Update()`'s freeze-gating already lives.
- **Where does the retained copy texture actually get made?** After `AddGameViewPass()`'s real
  execute callback finishes recording (i.e. right where `RenderPasses.cpp`'s
  `AddGameViewPass()`'s own lambda body ends, OR immediately after, from
  `Application::Run()`, once that frame's whole graph `Execute()` call returns and the Game View
  `RenderTexture` is confirmed to be back in `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`) — copy
  its CURRENT contents into a small, persistent, dedicated `RenderTexture` this phase's ring
  buffer slot owns. Model this GPU-to-GPU image copy on the EXACT SAME
  transition-copy-transition-back discipline `Renderer::CaptureImagePixels()` already uses for
  GPU-to-CPU readback (`src/Renderer/Renderer.cpp`) — same barriers, just a
  `vkCmdCopyImage`/`vkCmdBlitImage` into another live GPU image instead of a
  `vkCmdCopyImageToBuffer` into host memory. This must be a genuinely EXTRA, explicit, on-demand
  GPU copy — acceptable ONLY because it happens at most once per real capture trigger (never every
  frame), mirroring `Renderer::CaptureRenderTexturePixels()`'s own accepted "extra GPU
  submission/wait, but rare" cost model (see that method's own doc comment) — never folded into
  the always-running per-frame Game View recording path itself.
- `Renderer::CreateRenderTexture()` already exists as the exact factory needed for each ring-buffer
  slot's own retained copy texture — reuse it directly (with a `debugName` like
  `"FrameDebuggerHistorySlot0"`.."FrameDebuggerHistorySlot7"`), never hand-roll a new
  image-creation path.
- **Lazy allocation, not eager**: mirror `SwapchainCaptureService`'s own explicit "buffers are
  allocated LAZILY - only once RequestCapture() is called for the very first time" precedent
  (`SwapchainCaptureService.h`'s own header comment) — do not create 8 full-resolution retained
  `RenderTexture`s the moment the Editor starts if the Frame Debugger window has never even been
  opened.

## Step 3: The Plan

### 3.1 New type: `FrameDebuggerHistory`

Home: `src/Editor/FrameDebuggerHistory.h`/`.cpp` (`GTE_ENABLE_EDITOR`-only). Shape:

```cpp
struct FrameDebuggerHistoryEntry {
    FrameDebuggerSnapshot snapshot;      // real, from PHASE2's builder
    std::optional<RenderTexture> preview; // std::nullopt until the very first real capture ever
                                           // populates this slot - see "lazy allocation" above.
};

class FrameDebuggerHistory {
public:
    static constexpr int kCapacity = 8; // tunable - document the real number chosen here.

    // Called once per real capture trigger (PHASE3's own wiring below) - copies `snapshot` in,
    // and blits/copies `gameViewSource`'s CURRENT contents into this slot's own retained
    // RenderTexture (creating it lazily via `renderer.CreateRenderTexture()` the first time this
    // exact ring index is ever used). Evicts the OLDEST entry once `kCapacity` is exceeded (a
    // plain circular index, not a full container shift).
    void CaptureFrame(Renderer& renderer, const FrameDebuggerSnapshot& snapshot, RenderTexture& gameViewSource);

    int Count() const noexcept;      // how many real captures exist right now (0..kCapacity)
    int CursorIndex() const noexcept; // which one is currently being VIEWED (0-based, into the
                                       // real, currently-populated range only)
    void StepCursor(int delta);      // Prev/Next - clamps to [0, Count()-1], a no-op at either end
                                      // (never wraps) - PHASE4's new Frame-History mini-toolbar
                                      // buttons call this directly.
    const FrameDebuggerHistoryEntry* CurrentEntry() const noexcept; // nullptr if Count() == 0
};
```

Pure index/cursor arithmetic (`StepCursor()`'s clamping, `Count()`, eviction bookkeeping) MUST be
extractable into small, plain, Tier-1-testable free functions/methods operating on plain
ints — mirror `ClampSelectedEventIndex()`'s own "pure integer arithmetic, no live state" shape —
even though `CaptureFrame()` itself is inherently Tier-2 (it touches a real `Renderer`/
`RenderTexture`).

### 3.2 The real capture TRIGGER — three call sites, one shared function

A single, shared function (e.g. `FrameDebuggerPanel::TriggerCapture(EditorContext&, Renderer&,
RenderTexture& gameView)` or free-standing, implementer's call) that: (a) calls PHASE2's
`BuildRealFrameDebuggerSnapshot(...)` against the CURRENT frame's real `RenderGraphSnapshot` +
`FrameDebuggerCaptureContext`, then (b) calls `m_history.CaptureFrame(renderer, snapshot,
gameView)`, then (c) resets `m_selectedEventIndex` to `-1` (a freshly captured frame has nothing
selected yet, matching `frame-debugger-2`'s own "freshly opened window starts with nothing
selected" convention). Called from exactly three places:

1. **`m_enabled`'s false->true edge** inside `FrameDebuggerPanel::BuildToolbarRow()` (the SAME
   edge that already sets `ctx.playbackPaused = true`) — the very first "Enable" click also
   performs the very first real capture, so the tree is never left on "No frame captured yet."
   the moment Enable is checked (a materially better first-impression than requiring a SECOND,
   separate user action just to see anything at all).
2. **The existing Step action, gated on `m_enabled`** — from `Application::Run()`'s own per-frame
   loop, exactly where `IEditorLayer::TryConsumeStepRequest()` is already checked: if that step
   was actually consumed THIS frame AND the Frame Debugger is currently enabled, trigger a
   recapture of the JUST-STEPPED frame, once that frame's own Game View rendering has finished for
   real (order matters — capture AFTER the stepped frame's own Game View pass executes, not
   before).
3. **A new explicit "Capture" button**, added to `BuildToolbarRow()` next to "Enable" (only
   enabled/clickable while `m_enabled` is true) — for re-capturing on demand without stepping
   (e.g. after moving the Scene-view camera, or after an unrelated scene edit, while the
   simulation itself stays paused).

### 3.3 Threading the real `RenderTexture&`/`RenderGraphSnapshot`/capture-context down to the Panel

`FrameDebuggerPanel::Build(EditorContext& ctx)`'s signature will need to grow (or `ctx` itself
grows a new field/reference) so it can reach: the CURRENT frame's real Game View `RenderTexture&`,
the CURRENT frame's real `gte::rg::RenderGraphSnapshot`, the CURRENT frame's real
`FrameDebuggerCaptureContext&`, and a live `Renderer&` — confirm the least-invasive way to thread
these by reading how `ImGuiEditorLayer::BuildUI()` already threads `Game&`/`Renderer&`/
`const rg::RenderGraph&` into other panels today (its own existing call signature already proves
this pattern is established) and follow that exact precedent, rather than inventing a new global/
singleton access path.

### 3.4 Where does `FrameDebuggerCaptureContext` get ARMED and RESET each frame?

`Application::Run()`'s own per-frame loop is the one place that knows, before Game View rendering
happens, whether this frame should be recorded at all — the simplest correct rule: the capture
context is armed for THIS frame's Game View render whenever `ctx.frameDebuggerWindowOpen &&
m_frameDebuggerPanel's own m_enabled` are both true (confirm the exact accessor `Application`
needs — `FrameDebuggerPanel` may need a small public `bool WantsCaptureThisFrame() const` reader).
`Reset()` happens once per frame, before `Game::Render()`'s Game-View branch, exactly mirroring
`FrameRecorder::BeginFrame()`'s own per-frame-clear convention — never accumulating stale data
from a previous frame into a new one.

### 3.4b Actually threading the armed pointer: `Game::Render()` and `AddGameViewPass()` DO change
(a correction/addendum found during the 2nd-iteration review — PHASE1's own Step 3.1 only defers
this wiring to PHASE3 "eventual end-state" text; this phase is where it must actually happen, and it
was missing from an earlier draft of this file's own file-change inventory below)

`Application::Run()` never calls `RenderSystem::Draw()` directly — the actual call site is one layer
down, inside `RenderPasses.cpp`'s `AddGameViewPass()`'s own `execute` lambda (`game.Render(renderer,
aspectWidthOverHeight)`), which itself calls `RenderSystem::Draw()` via `Game::Render()`. Threading
PHASE3's real, non-null `FrameDebuggerCaptureContext*` from `Application::Run()` down to
`RenderSystem::Draw()` therefore genuinely requires growing BOTH of these two signatures, not just
`RenderSystem::Draw()`'s own (already done in PHASE1):
- `Game::Render(Renderer&, float aspectWidthOverHeight, const Mat4* viewProjectionOverride = nullptr,
  FrameDebuggerCaptureContext* frameDebuggerCapture = nullptr)` — a new, defaulted, LAST parameter,
  forwarded straight through to the float-aspect `RenderSystem::Draw()` overload ONLY (the branch
  taken when `viewProjectionOverride == nullptr`) — never into the `viewProjectionOverride`
  branch, since that branch is what Scene View's own call site uses (out of scope, Locked Design
  Decision #7).
- `AddGameViewPass(...)` (`src/Application/RenderPasses.h`/`.cpp`) gains a new parameter (e.g.
  `FrameDebuggerCaptureContext* capture`) that its `execute` lambda captures and forwards into its own
  `game.Render(renderer, aspectWidthOverHeight, nullptr, capture)` call. `Application::Run()` passes
  the real, armed-or-null pointer here (see Step 3.4 above for exactly when it is armed) at the SAME
  call site that already builds `AddGameViewPass(...)`'s other arguments.
- **`AddPresentPass(...)`'s OWN direct-render fallback branch (`directGameRenderAspect.has_value()`,
  the "both Game and Scene panels hidden this frame" case — genuinely reachable even in an Editor
  build, per `Application.cpp`'s own comment at its `PresentViaRenderGraph()` call site, not only in
  a release/non-Editor build) must NEVER be handed a real, non-null capture pointer — always pass
  `nullptr` explicitly at that one call site, never the same variable used for `AddGameViewPass()`.
  This is the correct, honest behavior anyway (this fallback path renders in place of, never
  alongside, the real `"GameView"` pass this same frame — the two are mutually exclusive per frame
  by construction, since `AddGameViewPass()` is simply never declared at all in the frame where this
  fallback runs), but it is also an easy mistake to make (naively forwarding one shared variable into
  both call sites) — call this out explicitly in code review/self-review before considering this
  phase done.
- `RenderPasses.h`'s own existing header comment for `AddGameViewPass()` (and the file-level comment
  above it) currently states `execute` calls `Game::Render()` "(UNCHANGED signature)" — that sentence
  becomes stale the moment this phase adds a new parameter; update it in the same edit (a one-line
  fix: reword to say the signature is unchanged FOR EVERY PARAMETER THIS FILE'S OWN PRE-EXISTING
  DOCUMENTATION ALREADY DESCRIBED, plus one new, always-defaulted, campaign-specific parameter — do
  not just delete the claim and say nothing).
- Apply PHASE1's own Step 3.1b forward-declare + `#if GTE_ENABLE_EDITOR`-guarded-dereference pattern
  at both of these two new call sites too — `Game.h`/`RenderPasses.h` are core, always-compiled files
  exactly like `RenderSystem.h`, and `FrameDebuggerCaptureContext` is just as absent from a
  `GTE_ENABLE_EDITOR=OFF` build here as it is there. Since neither `Game.cpp` nor `RenderPasses.cpp`
  ever needs to DEREFERENCE the pointer themselves (they only ever forward it onward, as a bare
  pointer, to whatever they call), most likely NEITHER of their own `.cpp` files needs an `#if`
  guard at all — only the header forward declarations are required — but confirm this is really true
  once the real code is in front of you, and add the guard anyway if either file turns out to touch
  the pointee directly for any reason.

### 3.5 What this phase explicitly does NOT do

- Does not change `FrameDebuggerPanel`'s own displayed content yet beyond the new Capture button
  (PHASE4's job — the event tree/inspector still read `BuildPlaceholderFrameDebuggerSnapshot()`
  until PHASE4 switches them over) — this phase's own compile check therefore only needs to prove
  `FrameDebuggerHistory`/the trigger wiring compile and unit-test correctly, not that the UI
  visibly changed yet.
- Does not implement Channels/Levels (PHASE6) or any HTTP endpoint (PHASE7).
- Does not handle a live window-resize of the Game View mid-capture specially beyond whatever
  `RenderTexture`'s own existing resize story already does — a captured historical frame's own
  retained texture is a fixed-size SNAPSHOT and is expected to keep its own original dimensions
  even if the live Game View later resizes; do not attempt to resize a past capture's retained
  texture in place.

### 3.6 Tier-1 testing

`tests/Editor/FrameDebuggerHistoryTests.cpp` — the pure cursor/count/eviction arithmetic only
(extract it into free functions/methods that take/return plain ints, exactly like
`ClampSelectedEventIndex()`): capturing fewer than `kCapacity` frames keeps `Count()` growing;
capturing MORE than `kCapacity` evicts the oldest, keeps `Count() == kCapacity`; `StepCursor()`
clamps at both ends without wrapping; a fresh, empty history reports `Count() == 0` and
`CurrentEntry() == nullptr`.

### 3.7 Compile check

Fast compile check: build `GreatTamanaEngine` (the real executable, since this phase touches
`Application.cpp`/`Renderer` wiring, not just Editor-local pure data) plus
`GreatTamanaEngineTests`'s `--gtest_filter=*FrameDebuggerHistory*`. A live runtime smoke test is
optional here (not required until PHASE8) but strongly encouraged given this phase's Tier-2 risk —
launch the engine, confirm no crash/regression via `/get_swapchain`, then close it.

### 3.8 File-change inventory

New: `src/Editor/FrameDebuggerHistory.h`, `src/Editor/FrameDebuggerHistory.cpp`,
`tests/Editor/FrameDebuggerHistoryTests.cpp`.
Modified: `src/Editor/Panels/FrameDebuggerPanel.h`/`.cpp` (Capture button, trigger wiring, new
member `FrameDebuggerHistory m_history`), `src/Editor/EditorContext.h` (only if new shared state is
genuinely needed there — prefer keeping ownership inside `FrameDebuggerPanel` itself per its own
existing stateful-class precedent), `src/Application/Application.h`/`.cpp` (arm/reset the capture
context each frame; observe the Step-consumed edge), `src/Editor/ImGuiEditorLayer.cpp`/`.h` (thread
the new required references into `FrameDebuggerPanel::Build()`), `src/Game/Game.h`/`.cpp` (new
defaulted `FrameDebuggerCaptureContext*` parameter on `Render()`, forwarded to `RenderSystem::Draw()`
— see Step 3.4b above), `src/Application/RenderPasses.h`/`.cpp` (new parameter on `AddGameViewPass()`
threading the armed pointer into its own `game.Render(...)` call; `AddPresentPass()`'s own direct-
render fallback call site passes `nullptr` explicitly, never this same pointer — see Step 3.4b;
also update `RenderPasses.h`'s own now-stale "Game::Render() (UNCHANGED signature)" comment),
`CMakeLists.txt`/`tests/CMakeLists.txt`.

Write `PHASE3_COMPLETION_REPORT.md` once done.
