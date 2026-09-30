# PHASE1 — COMPLETION REPORT: `RenderTexture`/`GpuResourceFactory::CreateRenderTexture()` gain `createDepthCompanion`

Campaign folder: `task_manager/editor-core-separation-27/`
Branch: `feature/editor-core-separation` (unchanged, no new branch created)

## Summary

Added a new, trailing, DEFAULTED `bool createDepthCompanion = true` parameter
all the way through the full "create a `RenderTexture`" call chain:

`RenderTexture`'s constructor → `RenderTexture::Create()` →
`GpuResourceFactory::CreateRenderTexture()` → `Renderer::CreateRenderTexture()`

When `false`, `RenderTexture::Create()` now skips building the companion
`DepthBuffer` entirely — `m_depthBuffer` simply stays null for that
`RenderTexture`'s whole lifetime. Every pre-existing consumer already
null-checks `m_depthBuffer` (`Target()`'s `if (m_depthBuffer) {...}`,
`DepthSampler()`'s ternary), so zero further code change was needed there —
confirmed true exactly as PHASE0/PHASE1's own "Situation" analysis predicted.

This is the direct enabler for a future `RenderGraphPersistentResourceCache`
color-only entry (`TextureDesc::hasDepth == false`) to carry EXACTLY ONE
tracked GPU allocation instead of a permanently-unused depth companion.

## Step 3.1 re-confirmation (done fresh, before editing)

Re-read every file this phase touches, directly, immediately before editing
(not assumed from the phase doc's approximate line numbers):

- `RenderTexture.h`'s constructor (confirmed at lines 86-90, matching the
  phase doc's estimate).
- `RenderTexture.cpp`'s `Create()` (confirmed the unconditional
  `m_depthBuffer = std::make_unique<DepthBuffer>(...)` call at line 191, and
  `Destroy()`'s already-safe unconditional `m_depthBuffer.reset()`).
- `GpuResourceFactory::CreateRenderTexture()` (`.h` line 70-72, `.cpp` line
  197-207) — confirmed it forwards straight into `RenderTexture`'s
  constructor with no other logic beyond the `allowStorageImageAccess`
  format-capability check.
- **Confirmed the missing link the phase doc flagged as needing live
  verification**: `RenderGraphResourcePool::AcquireTexture()`
  (`RenderGraphResourcePool.cpp` line 28) calls
  `m_renderer->CreateRenderTexture(...)` — i.e. it goes through
  `Renderer::CreateRenderTexture()` (`Renderer.h` line 262, `Renderer.cpp`
  line 150-163), a THIRD layer above `GpuResourceFactory`, exactly as the
  phase doc suspected. **`Renderer::CreateRenderTexture()` therefore DOES
  need the new trailing parameter forwarded through** — this was done (see
  below). This confirms the future `RenderGraphPersistentResourceCache`
  (PHASE4) should call through `Renderer::CreateRenderTexture()`, mirroring
  `RenderGraphResourcePool::AcquireTexture()`'s own exact call path.

No ambiguity was found beyond what PHASE0/PHASE1 already resolved — `ask_questions`
was not needed for this phase.

## Changes made

1. **`src/Renderer/RenderTexture.h`**
   - Added `bool createDepthCompanion = true` as the new, LAST trailing
     parameter to the constructor, with a new doc comment explaining it
     (referencing `editor-core-separation-27` BIG STEP 3 and forward-
     referencing `RenderGraphPersistentResourceCache` as its one real future
     consumer).
   - Added `bool m_createDepthCompanion = true;` as a new private member,
     alongside `m_allowDepthSampledAccess`.

2. **`src/Renderer/RenderTexture.cpp`**
   - Constructor: added `createDepthCompanion` parameter, threaded into the
     initializer list (`m_createDepthCompanion(createDepthCompanion)`).
   - Move constructor / move-assignment operator: threaded
     `m_createDepthCompanion` through exactly like `m_allowDepthSampledAccess`
     already was (plain copy for a `bool`, no `std::exchange` needed since
     it's not a resource handle).
   - `Create()`: gated the existing
     `m_depthBuffer = std::make_unique<DepthBuffer>(...)` call behind
     `if (m_createDepthCompanion) { ... }`. Nothing else in `Create()`/
     `Destroy()` needed to change — `Destroy()`'s `m_depthBuffer.reset()` is
     already an unconditional, safe no-op on a null pointer.

3. **`src/Renderer/GpuResourceFactory.h` / `.cpp`**
   - `CreateRenderTexture()`: added the same trailing `createDepthCompanion`
     parameter, forwarded straight through to `RenderTexture`'s constructor,
     with an updated doc comment.

4. **`src/Renderer/Renderer.h` / `.cpp`**
   - `CreateRenderTexture()`: added the same trailing `createDepthCompanion`
     parameter (confirmed genuinely needed per the Step 3.1 re-confirmation
     above), forwarded straight through to
     `GpuResourceFactory::CreateRenderTexture()`, with an updated doc comment
     explicitly noting `RenderGraphResourcePool::AcquireTexture()`'s own
     existing call chain through this method.

No other call site was touched. Every existing caller of any of these four
functions (`RenderFeatureCompositor.cpp`, `AssetPreviewMesh.cpp`,
`BlitValidation.cpp`, `BoneViewerWindow.cpp`, `ComputeBlurValidation.cpp`,
`FrameDebuggerHistory.cpp`, `FrameDebuggerReplayPasses.cpp`,
`GBufferValidation.cpp`, `ImGuiEditorLayer.cpp`,
`Atmosphere/AtmosphereLutRenderer.cpp`, `RenderGraphResourcePool.cpp`) passes
at most 6 positional arguments — well short of the new 7th, trailing,
defaulted parameter — so every one of them compiles and behaves
byte-for-byte unchanged (still gets a depth companion, exactly as before).
Confirmed via `search_in_dir "CreateRenderTexture(" src` before AND after the
change — no other file needed editing.

## Note on an editing mistake caught and corrected mid-phase

While using `edit_line`, two of my own line-count miscalculations
(`RenderTexture.cpp`'s main constructor, and `GpuResourceFactory.h`'s doc
comment block) briefly left duplicated leftover lines in the file. Both were
caught immediately by re-reading the file right after the edit, and fixed
with a follow-up `edit_line` removing the stale duplicate block before
compiling. Final `read_file` passes over both files confirm clean, non-
duplicated content — this is disclosed here for transparency, not because it
affected the final shipped result.

## Verification

1. **Incremental compile check**: `cmake --build build` — succeeded with
   zero errors (84/84 steps), including `GreatTamanaEditor.exe`,
   `GreatTamanaEngineTests.exe`, and both Project Assembly `.dll`s.
2. **Live-Editor smoke check** (per this phase's own Step 4 — no new
   pass-author-facing behavior exists yet, so no automated test is
   fabricated here; that's PHASE4/PHASE9's job once a real
   `createDepthCompanion = false` caller exists):
   - Launched `GreatTamanaEditor.exe` via `run_app_background`.
   - `GET /get_logs?min_level=Warning&limit=50` — the only warnings present
     are pre-existing, unrelated ones (demo plugin priority-tie-break
     warnings, GPU-timing-slot-budget-exhaustion warnings for demo render
     features) — nothing new, nothing referencing `RenderTexture`,
     `DepthBuffer`, or this phase's own change.
   - `GET /get_game_view` — returned a valid 34288-byte PNG, visually a
     normal rendered frame (the demo plugins' own radial-vignette blend
     effect) — confirms the Game View's `RenderTexture` (still constructed
     with `createDepthCompanion` at its default `true`) renders exactly as
     before.
   - `stop_app_background` — the process was cleanly terminated.
3. No full build, no full `ctest` run was performed (correctly deferred to
   PHASE9, per the campaign's own rules) — the real, permanent regression
   test for `createDepthCompanion == false` genuinely skipping the depth
   allocation (checked via `GpuMemoryTracker`) is explicitly deferred to
   PHASE4/PHASE9, once a real caller passing `false` exists (this phase has
   none — it is purely additive, zero-behavior-change plumbing).

## Honestly-flagged open items

- This phase, by design, introduces no new automated test coverage of its
  own — the phase file itself states this is correct ("do not attempt to
  fabricate that proof in THIS phase with no real caller yet"). The actual
  proof that `createDepthCompanion = false` produces exactly one tracked GPU
  allocation is PHASE4/PHASE9's job.
- No other open issues. Every acceptance point this phase's own `.md` file
  lists is satisfied.

## Git

Changes staged and committed together with this report:
- `src/Renderer/RenderTexture.h`
- `src/Renderer/RenderTexture.cpp`
- `src/Renderer/GpuResourceFactory.h`
- `src/Renderer/GpuResourceFactory.cpp`
- `src/Renderer/Renderer.h`
- `src/Renderer/Renderer.cpp`
- `task_manager/editor-core-separation-27/PHASE1_COMPLETION_REPORT.md`
