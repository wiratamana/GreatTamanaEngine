# PHASE3 — Completion Report

_Parent: `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE3_UNIFIED_STEP_TIMELINE_AND_PER_DRAW_REPLAY_RENDERING.md`. Builds on
Phase 1 (`FrameDebuggerCurrentCapture`) and Phase 2 (`m_pendingCaptureAfterEnable`,
now renamed/generalized here)._

## Summary

Every real object-draw step (and every compute-pass step) inside one
captured frame can now get its own real, correct "accumulated Game View as
of this exact step" image. `AddFrameDebuggerReplayPasses()`
(`src/Application/RenderPasses.h/.cpp`) declares N brand-new, debug-only,
self-contained Render Graph passes on an explicit capture-trigger frame —
one per real object the "GameView" pass draws that frame — each redrawing
objects `[0..i]` FROM SCRATCH into its own dedicated destination
`RenderTexture`, imported/declared exactly like the real, always-on
`"GameView"` pass, but never modifying it in any way. The two-bool pending/
serviced handshake specified in the phase document's own Step 3.0
(`m_pendingCaptureTrigger` / `m_replayServicedThisFrame`) was implemented
exactly as written, generalizing Phase 2's single-trigger deferral to all
three capture triggers (Enable-edge, Step, "Capture" button/HTTP route).

**Two real, confirmed bugs were found and fixed during this phase's own
Step 4 manual visual spot-check** (not just theoretical — both were caught
by actually looking at the rendered pixels), documented in detail below.
Both fixes are already included in the code as committed; this report
explains what was found and why the fix is correct, per the workflow
rules' "explain and justify any deviation" requirement (Step 5 of the phase
document).

## Renamed / new identifiers future phases need to know about

### `m_pendingCaptureTrigger` / `m_replayServicedThisFrame` (Step 3.0)

- `src/Editor/Panels/FrameDebuggerPanel.h/.cpp`:
  - `bool m_pendingCaptureAfterEnable` (Phase 2) → **renamed**
    `bool m_pendingCaptureTrigger` — now set to `true` by **all three**
    capture-trigger call sites (previously only the Enable-edge did):
    - `ApplyEnabledEdge()`'s false→true branch (unchanged logic, renamed
      field).
    - The "Capture" button in `BuildToolbarRow()` — **no longer calls
      `TriggerCapture()` synchronously**; only sets the flag.
    - The Step-triggered-capture consumption in `BuildToolbarRow()`
      (`m_stepCaptureRequested`) — **no longer calls `TriggerCapture()`
      synchronously**; only sets the flag (when `m_enabled`).
  - **New**: `bool m_replayServicedThisFrame = false;` — the LATE half of
    the handshake (see below).
  - **New public method**: `bool FrameDebuggerPanel::ConsumePendingReplayRequest()`
    — the EARLY half. Read-and-clear on `m_pendingCaptureTrigger`; if it
    was set, also sets `m_replayServicedThisFrame = true` and returns
    `true`. Called once per frame by `Application::Run()` (via
    `IEditorLayer::ConsumePendingFrameDebuggerReplayRequest()`), from inside
    the offscreen `build` lambda, **before** the "GameView" pass (and
    therefore `AddFrameDebuggerReplayPasses()`) is declared.
  - **Top of `Build()`** (before the existing Phase 1 "clear on
    Resume-while-Enabled" check): `if (m_replayServicedThisFrame) {
    m_replayServicedThisFrame = false; TriggerCapture(); }` — this is now
    the **only** place `TriggerCapture()` is ever called from (all three
    trigger sites just arm `m_pendingCaptureTrigger`, consumed one frame
    later by this same check).
  - The old top-of-`BuildToolbarRow()` `if (m_pendingCaptureAfterEnable) {
    ...; TriggerCapture(); }` block (Phase 2) was **removed entirely** —
    superseded by the new two-bool handshake above.
- `src/Editor/EditorLayer.h` / `ImGuiEditorLayer.cpp` / `NullEditorLayer.cpp`:
  - **New virtual**: `bool IEditorLayer::ConsumePendingFrameDebuggerReplayRequest()`
    — forwards to `FrameDebuggerPanel::ConsumePendingReplayRequest()` in
    `ImGuiEditorLayer`; always returns `false` in `NullEditorLayer`.
- **`FrameDebuggerPanel::CaptureNowFromCommand()`** (the `GET
  /frame_debugger/capture` HTTP route's real implementation) — **also
  updated** to set `m_pendingCaptureTrigger = true` instead of calling
  `TriggerCapture()` directly. This was **not** explicitly called out by
  name in the phase document's Step 3.0 (which only names "Enable edge /
  Step / Capture button"), but it is unambiguously the HTTP mirror of the
  hand-driven "Capture" button and shares the exact same timing constraint
  — found and fixed during this phase's own Step 4 manual spot-check (see
  "Bugs found" below for the concrete symptom this produced before the
  fix). `IEditorLayer::FrameDebuggerCaptureNow()`'s doc comment was updated
  to match.

### `AddFrameDebuggerReplayPasses()` (Step 3.3)

`src/Application/RenderPasses.h/.cpp` — new function:

```cpp
std::vector<rg::TextureHandle> AddFrameDebuggerReplayPasses(rg::RenderGraphBuilder& builder, Game& game,
    Renderer& renderer, float aspectWidthOverHeight, std::size_t objectCount,
    const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers,
    const std::function<void(VkCommandBuffer)>& recordSkyBackground, RenderTexture& gameTarget,
    FrameDebuggerCaptureContext& capture);
```

- Takes `RenderTexture& gameTarget` (the real Game View target, READ-ONLY —
  only its `Extent()`/`Format()` are read, never written) as an explicit
  parameter — the phase document's own Step 3.3/3.5 left this as a
  paraphrased placeholder (`gameTargetWidth`/`gameTargetHeight`/
  `gameTargetFormat`, "either [threading width/height/format or passing
  gameTarget itself] is fine, implementer's choice"); this implementation
  chose to pass `gameTarget` itself, since the real Application.cpp call
  site already has it on hand inside the same `if (gameTarget != nullptr)`
  block (the phase document's own Step 3.5 text explicitly lists
  `gameTarget` among the things that block needs).
- **Returns `std::vector<rg::TextureHandle>`** (the phase document's own
  Step 3.3 sketch showed this as a `void` function) — see "Bugs found"
  below for why this return value is load-bearing and not just a
  convenience.
- A no-op (returns an empty vector) whenever `objectCount == 0`.
- Body is wrapped in `#if GTE_ENABLE_EDITOR` / `#else` (empty, parameters
  cast to `(void)`) so this CORE, always-compiled file still compiles AND
  links cleanly with `GTE_ENABLE_EDITOR=OFF` (confirmed — see
  "Verification" below) — mirrors `RenderSystem.cpp`'s own existing Step
  3.1b precedent, applied here to a whole callee body instead of a
  passthrough parameter, since this function's body (unlike
  `AddGameViewPass()`'s) genuinely needs to call
  `capture.SetReplayStepPreviews()`, which needs the real, complete
  `FrameDebuggerCaptureContext` type.

### `ReplayStepPassName()` permanent pool (Step 3.3b)

`src/Application/RenderPasses.cpp`, anonymous namespace — implemented
**exactly** as the phase document's own fully-specified code:

```cpp
std::deque<std::string>& ReplayStepPassNamePool();       // static local std::deque<std::string>, whole-process lifetime.
const char* ReplayStepPassName(std::size_t index);       // lazily grows the pool, returns a STABLE, permanent pointer.
```

`std::deque` (never `std::vector`) so growing the pool never invalidates an
already-handed-out `c_str()` pointer previously stored inside a
`PassRecord::name`. Pass names are `"FrameDebuggerReplayStep0"`,
`"FrameDebuggerReplayStep1"`, ... — reused verbatim as each replay
destination texture's own `ImportTexture()` name too (a texture and its
pass share one name — harmless, since texture names and pass names are
different tables in the render graph).

Confirmed working across repeated captures this session (see Verification
below) — no crash, no corruption, on the second AND third capture.

### `FrameDebuggerCaptureContext::ReplayStepPreviews()` (Step 3.4)

`src/Editor/FrameDebuggerCapture.h/.cpp` — new:

```cpp
void SetReplayStepPreviews(std::vector<RenderTexture>&& previews) noexcept;
const std::vector<RenderTexture>& ReplayStepPreviews() const noexcept;
```

- New private member `std::vector<RenderTexture> m_replayStepPreviews;`.
- `Reset()` now also calls `m_replayStepPreviews.clear()` (RAII-destroys
  last-armed-frame's leftover textures, per `AGENTS.md`'s RAII rule).
- **Lifetime, exactly as documented in the phase document's own Step 3.4**:
  populated only on an explicit capture-trigger frame (inside
  `AddFrameDebuggerReplayPasses()`, at pass-declaration time — filled with
  garbage/uninitialized content until each pass's own `execute` lambda
  actually runs, later the SAME `Execute()` call), and cleared again on
  the very next ARMED frame's `Reset()` call (i.e. it survives for
  roughly one real frame's duration before being destroyed — Phase 4's own
  explicit job is to read/move this vector out into permanent storage,
  strictly inside `TriggerCapture()`, before that next Reset() runs; the
  Step 3.0 two-bool handshake already guarantees `TriggerCapture()` runs
  later the SAME frame the replay passes were declared/executed).

### `Game::CountGameViewDrawCommandsThisFrame()` (Step 3.1)

`src/Game/Game.h` — new, inline (mirrors
`CollectGpuSkinningDispatchRequests()`'s own inline-in-header precedent):

```cpp
std::size_t CountGameViewDrawCommandsThisFrame()
{
    return RenderSystem::CollectRenderables(m_registry).size();
}
```

**Non-const** (not `const` as I initially considered) — `Registry&
m_registry` would otherwise need a `const_cast` to satisfy
`CollectRenderables(Registry&)`'s non-const parameter; matching the phase
document's own illustrative signature (non-const) was simpler and avoids
the cast entirely.

**Documented assumption** (verbatim, per the phase document's own explicit
instruction to record this): `objectCount` here is
`CollectRenderables(m_registry).size()` — the number of entities that HAVE
a `MeshRenderer` component, **not** automatically identical to the number
of entities that successfully resolve a real `renderer.Submit()` call
inside `RenderSystem::Draw()` (a `DrawCommand` whose mesh/pipeline handle
doesn't resolve is silently skipped, and only a successfully-resolved draw
calls `capture->RecordEntityDraw()`, i.e. ends up in
`capture.DrawRecords()`). In every scenario this engine exercises today,
every `MeshRenderer`'s handles resolve successfully, so `objectCount ==
capture.DrawRecords().size()` holds in practice — but if a future feature
ever unregisters a mesh/pipeline while entities still reference it, the
`i`-th replay pass's own `maxDrawCount` cutoff would silently stop lining
up 1:1 with the `i`-th entry of `capture.DrawRecords()`, from the first
skipped entity onward. Not reachable with today's engine — recorded here,
and in `Game.h`'s own doc comment, for a future maintainer.

### `RenderSystem::Draw()` / `Game::Render()` `maxDrawCount` (Step 3.2)

Both `RenderSystem::Draw()` overloads and `Game::Render()` gained a new,
LAST, defaulted parameter: `std::optional<std::size_t> maxDrawCount =
std::nullopt`. Purely additive — every pre-existing call site compiles
unchanged. The `Mat4&` overload (which owns the real loop) stops iterating
— `break`s — once `maxDrawCount` COMMANDS (loop iterations over
`CollectRenderables()`'s result, not "successfully resolved draws" — see
the assumption above) have been considered. `Game::Render()`'s
`viewProjectionOverride` branch (Scene View's own call site) **never**
forwards `maxDrawCount` — mirrors `frameDebuggerCapture`'s own identical
rule, since that branch is out of scope for the whole Frame Debugger
feature (Locked Design Decision #7).

### Application.cpp wiring (Step 3.5)

Inside the offscreen `build` lambda, immediately after
`AddGameViewPass(...)`/`outputs.push_back(h);`, still inside `if (gameTarget
!= nullptr)`:

```cpp
if (frameDebuggerCapture != nullptr && m_editorLayer->ConsumePendingFrameDebuggerReplayRequest()) {
    const std::size_t objectCount = m_game.CountGameViewDrawCommandsThisFrame();
    const std::vector<rg::TextureHandle> replayStepHandles = AddFrameDebuggerReplayPasses(
        b, m_game, m_renderer, aspect, objectCount, gpuSkinningBuffers,
        recordGameSkyBackground, *gameTarget, *frameDebuggerCapture);
    for (const rg::TextureHandle& replayHandle : replayStepHandles) {
        outputs.push_back(replayHandle);
    }
}
```

The `for` loop appending every returned handle into `outputs` is
**correctness-critical** — see "Bugs found" below.

## Bugs found and fixed during this phase's own Step 4 manual spot-check

**Both of these were caught by literally looking at the resulting images**,
not by code review alone — this is exactly why the phase document mandated
a real visual spot-check rather than a compile-only check for this phase.

### Bug A — replay passes were silently culled (garbage/uninitialized VRAM)

The very first implementation attempt declared `AddFrameDebuggerReplayPasses()`
as `void`, never adding its N destination `TextureHandle`s to the `build`
lambda's own `outputs` root set. `RenderGraphCompiler::Compile()`'s
backward-reachability culling scan (`RenderGraphCompiler.cpp`, confirmed by
reading it directly) only keeps a pass whose write reaches `finalOutputs`
(directly or transitively) — since none of these N passes' writes are ever
read by anything else in the graph, and none were added to `outputs`, every
one of them was silently culled: `execute` never ran, so each destination
`RenderTexture` (created, but never rendered into — a fresh Vulkan image's
contents are undefined, never zero-initialized) was captured showing
genuine uninitialized VRAM garbage (striped noise / confetti-like patterns,
confirmed via `load_image` on a temporary debug PNG dump — see
"Verification" below). **Fix**: `AddFrameDebuggerReplayPasses()` now
returns `std::vector<rg::TextureHandle>` (every destination handle it
declared a pass for), and the Application.cpp call site appends every one
of them into `outputs`. After this fix, every replay image showed the
correct dark navy `kGameClearColor` background and the correct accumulated
geometry (see Verification below) — confirmed via the SAME temporary debug
dump, before-and-after.

### Bug B — `GET /frame_debugger/capture` (the HTTP "Capture" route) still captured synchronously

`FrameDebuggerPanel::CaptureNowFromCommand()` (the real implementation
behind `GET /frame_debugger/capture`) still called `TriggerCapture()`
directly, exactly like it did before this phase — it was never updated to
join the new two-bool deferred handshake, unlike the hand-driven "Capture"
button in `BuildToolbarRow()`. This meant a SECOND capture triggered via
this HTTP route ran on a frame where `AddFrameDebuggerReplayPasses()` had
never been declared, so `m_captureContext.ReplayStepPreviews()` was empty
at `TriggerCapture()` time — the capture itself still "succeeded"
(`hasCapturedFrame` stayed `true`, the event tree was still correct), but
silently carried ZERO replay-step images, which would have been a
completely invisible regression until Phase 4/5 tried to wire the (empty)
data up. **Fix**: `CaptureNowFromCommand()` now sets
`m_pendingCaptureTrigger = true` instead, exactly mirroring the button.
Confirmed fixed by re-running the two-capture HTTP test after the fix — the
second (and third) capture's replay images were present and correct (see
Verification below).

## What Phase 3 deliberately does NOT do (confirmed unchanged from the phase document)

- Does not touch `ChooseFrameDebuggerPreviewSource()`, the event tree, or
  the panel UI at all — confirmed the new replay passes never appear in
  the Frame Debugger's own tree (they are plain `AddPass()`, not
  `AddComputePass()`, so `isComputePass == false` and
  `BuildRealFrameDebuggerSnapshot()` never discovers them) — this phase's
  own live spot-check (`GET /get_swapchain` screenshot, see Verification)
  confirms the tree still shows exactly the pre-existing shape (Compute
  Dispatches groups, `GameView` + its two per-entity leaves, Compute
  Dispatches Post-GameView) with no new/duplicate rows.
- Does not remove `computePassPreviews`/`CollectComputePassTextureWrites()`
  — untouched, exactly as instructed (Phase 4's job).

## Verification performed

### Compile check (Step 4.1)

- `cmake --build build --target gte_core -j 4` — succeeded, zero
  errors/warnings introduced.
- `cmake --build build --target GreatTamanaEngineTests -j 4` — succeeded.
- `cmake --build build --target GreatTamanaEngine -j 4` — succeeded (full
  executable link).
- `cmake --build build-editor-off --target gte_core -j 4` AND `--target
  GreatTamanaEngine -j 4` — succeeded (a genuine `GTE_ENABLE_EDITOR=OFF`
  build, confirming every `#if GTE_ENABLE_EDITOR` guard added this phase
  — `RenderPasses.cpp`'s new function body, `Application.cpp`'s temporary
  debug block before it was removed — is correctly balanced and the CORE
  path never dereferences an incomplete `FrameDebuggerCaptureContext`).
- Ran `tests\GreatTamanaEngineTests.exe --gtest_filter=*FrameDebugger*` —
  all **85 tests passed**, 0 failed, both before and after this phase's
  code changes (no Tier-1-testable pure logic was added/changed this phase
  that needed a new test — `AddFrameDebuggerReplayPasses()` itself needs a
  live `Renderer`/`RenderGraph`, Tier 2, same as `FrameDebuggerHistory.cpp`'s
  own `CaptureFrame()` already is).

### Manual visual spot-check (Step 4.2/4.3) — INCLUDING the two-capture check

Ran `build\GreatTamanaEngine.exe` via `run_app_background`, drove it
entirely over HTTP via `gte_send_request`:

1. `GET /frame_debugger/open`, `POST /instantiate_primitive` x3 (Cube at
   x=-1.2, Sphere at x=0, Cone at x=1.2 — using the real
   `"world_position":{"x":...}` request shape), `GET
   /frame_debugger/enable?value=true` (the Enable-edge trigger — first
   capture).
2. Temporarily added (and later fully removed, see below) a debug block in
   `Application.cpp`, guarded by `#if GTE_ENABLE_EDITOR`, that dumped every
   `frameDebuggerCapture->ReplayStepPreviews()` entry to a PNG on disk via
   the SAME `Renderer::CaptureRenderTexturePixels()` +
   `Encoding::EncodeRgba8ToPng()` path `GET /get_game_view` already uses —
   this sidesteps the real, one-frame-only HTTP timing race the eventual
   texture-registry-backed path would otherwise have (these RenderTextures
   are only alive from mid-frame-N to early-frame-N+1's `Reset()` — Phase
   4's job to give them a permanent home).
3. **Before Bug A's fix**: the three dumped PNGs showed clear garbage/noise
   (repeating stripe patterns, confetti-like speckling, tiled glyph-like
   shapes) — NOT the expected dark navy `kGameClearColor` background.
4. **After Bug A's fix**: `capture.ReplayStepPreviews().size()` logged as
   `3` (matching `CountGameViewDrawCommandsThisFrame()`'s real object
   count), and the three PNGs showed, in order: step 0 = Cube alone; step 1
   = Cube + Sphere; step 2 = Cube + Sphere + Cone + sky background — a
   correct, progressively-accumulating sequence. Step 2 was pixel-identical
   to a same-moment `GET /get_game_view` capture (the phase document's own
   suggested cross-check).
5. **Two/three-capture check (Step 4.3)**: triggered `GET
   /frame_debugger/capture` a SECOND time (same scene) — before Bug B's
   fix, this produced STALE (unchanged-timestamp) debug PNGs, revealing
   `ReplayStepPreviews()` was empty that capture (Bug B). After Bug B's
   fix, the second capture's PNGs were freshly rewritten and pixel-identical
   to the first capture's (same static scene, as expected) — no crash, no
   garbage, no dangling-pointer symptom. A THIRD capture (via the
   hand-driven-equivalent Enable-edge + two explicit `GET
   /frame_debugger/capture` calls in one session, using a fresh scene with
   `SmokeCube`/`SmokeSphere`) also succeeded cleanly, confirmed via `GET
   /get_swapchain` showing the real, correct event tree
   (`SmokeCube (Entity 1)`, `SmokeSphere (Entity 2)` under `GameView`,
   `0 of 9` events, the preview box showing the correct final composited
   image) with `hasCapturedFrame: true` throughout.
6. **All temporary debug code was fully removed** before the final commit —
   confirmed via `search_in_dir` for `"TEMPORARY"`/`"PHASE3_DEBUG"` in
   `Application.cpp` (only unrelated pre-existing comment matches remain),
   and via a final clean rebuild of `gte_core`/`GreatTamanaEngineTests`/
   `GreatTamanaEngine` (both `GTE_ENABLE_EDITOR` configs) plus a final
   `GreatTamanaEngineTests --gtest_filter=*FrameDebugger*` run (85/85
   passed) with the temporary code already gone.

No full build/regression suite was run (Phase 7's job only, per the
workflow rules) — only the targeted `gte_core`/`GreatTamanaEngineTests`/
`GreatTamanaEngine` targets, in both `GTE_ENABLE_EDITOR` configs, plus the
`*FrameDebugger*` gtest filter and the live HTTP smoke test above.

## Definition of Done — status

- [x] `capture.ReplayStepPreviews()` holds one real, correct, garbage-free
      `RenderTexture` per object drawn this frame, in the same order as
      `capture.DrawRecords()` (confirmed visually — step 0/1/2 correctly
      accumulate; step N-1 pixel-matches `GET /get_game_view`), produced
      only on an explicit capture-trigger frame, with zero effect on the
      real `"GameView"` pass's own output (confirmed: `GET /get_game_view`
      and `GET /get_swapchain`'s own "Game" panel both still show the
      correct, real scene throughout).
- [x] Every replay pass's own `name` argument is backed by permanent,
      session-lifetime storage (Step 3.3b) — verified by triggering THREE
      captures total across two separate live sessions during manual
      spot-checking, with zero crash/corruption.
- [x] `PHASE3_COMPLETION_REPORT.md` written (this file), including the
      two-bool pending/serviced handshake write-up and both bugs found/
      fixed during Step 4, code committed.

## Genuine ambiguities encountered

Only one genuine gap in the phase document was found (not an ambiguity
requiring `ask_questions` — the document's own text already gave explicit
permission to resolve it either way): Step 3.3's illustrative
`AddFrameDebuggerReplayPasses()` signature and Step 3.5's illustrative call
site both omitted the `gameTarget`/width-height-format parameter the
function's own body obviously needs (to size/format the N destination
textures identically to the real Game View) — resolved by adding a
`RenderTexture& gameTarget` parameter, exactly one of the two options the
document's own Step 3.3 closing paragraph explicitly said was "fine,
implementer's choice." No `ask_questions` call was needed for this or
anything else this phase — every other technical claim in the phase
document (PassContext's shape, RenderTexture's color+depth pairing, the
depth-target handling, the pass-name storage mechanics) matched the real
source exactly, as the document's own "Review note" claimed. The two bugs
described above were found by testing, not by any ambiguity in the
document's own instructions — the document's own code sketch for the
`execute` lambda and pass declarations was otherwise implemented verbatim
and was correct; the culling and HTTP-route gaps were implementation
oversights on my part, not something the phase document should have
specified differently (Step 3.5 does not mention `outputs.push_back()` for
these handles at all, which in hindsight is itself a small gap in the
document worth flagging for future readers of this campaign, but not one
that rose to needing a human's input to resolve — RenderGraphCompiler's
culling behavior is straightforward once read, and the fix is the only
sane one).

## Next phase (Phase 4) — what to build on top of this

- `capture.ReplayStepPreviews()` — read/move this out into permanent
  storage inside `TriggerCapture()`, before the frame ends (see
  `FrameDebuggerCapture.h`'s own updated doc comment for the exact ordering
  requirement).
- `m_pendingCaptureTrigger` / `m_replayServicedThisFrame` — already fully
  wired for all three trigger sites (including the HTTP "Capture" route,
  fixed as Bug B above) — Phase 4 should not need to touch this machinery
  at all, just consume its result (`TriggerCapture()` already runs at the
  right time).
- `AddFrameDebuggerReplayPasses()`'s return value (`std::vector<rg::TextureHandle>`)
  is Application.cpp-internal bookkeeping only (feeding `outputs`) — Phase
  4 does not need it; it only ever reads `capture.ReplayStepPreviews()`.
