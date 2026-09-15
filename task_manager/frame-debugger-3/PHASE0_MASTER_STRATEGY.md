# PHASE0 — MASTER STRATEGY: Real Frame Capture for the Editor "Frame Debugger"

Campaign folder: `task_manager/frame-debugger-3/`
Branch: `feature/frame-debugger-impl`
Depends on: `task_manager/frame-debugger-1/` (Pause/Resume/Step, `gte::Time`/`EngineContext`) and
`task_manager/frame-debugger-2/` (the GUI-only scaffolding: `src/Editor/FrameDebuggerData.h/.cpp`,
`src/Editor/Panels/FrameDebuggerPanel.h/.cpp`, `docs/conventions/frame-debugger.md`).

This is the **orchestrator** document. It contains no implementation instructions of its own —
each child phase (`PHASE1`..`PHASE8`) is a self-contained, independently compilable chunk. Read
this file first, then work the phases **in numeric order** (each assumes every previous one
already landed). Every phase file below MUST open with "## Parent -> PHASE0_MASTER_STRATEGY.md,
read it first" and end with its own fast-compile-check command plus a
`PHASEn_COMPLETION_REPORT.md` write-up, exactly like `frame-debugger-2`'s own precedent.

## Phase list

| Phase | File | One-line summary |
|---|---|---|
| 1 | `PHASE1_RENDERER_CAPTURE_INSTRUMENTATION.md` | Cosmetic `Pipeline` shader-identity name, a real, zero-overhead-when-disarmed `FrameDebuggerCaptureContext` threaded through `Renderer::Submit()`/`RenderSystem::Draw()`, and a pure `DescribeStandardPipelineState()` free function reporting this engine's real (constant) blend/Z/stencil configuration. |
| 2 | `PHASE2_FRAME_DEBUGGER_SNAPSHOT_BUILDER.md` | A new, pure, Tier-1-tested real snapshot builder that reshapes `gte::rg::RenderGraphSnapshot` + PHASE1's capture context into a real, non-empty `FrameDebuggerSnapshot`, filtered to ONLY the Game View's own passes. |
| 3 | `PHASE3_FRAME_HISTORY_RING_BUFFER_AND_CAPTURE_TRIGGER.md` | A real multi-frame ring buffer (`FrameDebuggerHistory`), each slot holding one real snapshot plus a retained GPU copy of that historical frame's real Game View output texture; wires the actual capture TRIGGER (Enable edge / Step / an explicit Capture button). |
| 4 | `PHASE4_PANEL_REAL_TREE_AND_FRAME_HISTORY_UI.md` | Swaps `FrameDebuggerPanel` from the placeholder snapshot to PHASE3's ring buffer; adds a new Frame-History mini-toolbar (Prev/Next captured frame); wires the RenderTarget preview box to the real retained texture. |
| 5 | `PHASE5_EVENT_DETAILS_REAL_DATA_WIRING.md` | Verifies/finishes the event-details section against real, non-null data — the smallest phase, since `frame-debugger-2` already fully built this UI against the exact same struct shape. |
| 6 | `PHASE6_CHANNELS_AND_LEVELS_REAL_PREVIEW.md` | Makes the Channels (All/R/G/B/A) and Levels controls functionally real via a small, dedicated preview-compositing shader — never reusing `/get_texture`'s unrelated `channel=color\|depth` parameter. |
| 7 | `PHASE7_NETWORK_HTTP_AUTOMATION_AND_MAIN_VIEWPORT_PINNING.md` | A brand-new `FrameDebuggerCommandBridge` + `/frame_debugger/*` HTTP endpoints (open/enable/capture/select_event/step_history/set_channel/set_levels/state), plus the main-viewport-pinning fix so the window is guaranteed visible to `GET /get_swapchain`. |
| 8 | `PHASE8_INTEGRATION_BUILD_DOCS_AND_FULL_VERIFICATION.md` | Build-system final wiring, full doc sweep (`AGENTS.md`/`docs/`/`README.md`/`TODO.md`), full clean build (both Editor configs) + full `ctest` regression, and a live, HTTP-automation-driven end-to-end smoke test — finally closing the "manual verification limitation" both prior campaigns had to accept. |

---

## Step 1: The Goal (Where are we going?)

Turn the `frame-debugger-2` campaign's GUI-only scaffolding into a genuinely working,
Unity-style Frame Debugger for the **Game View** render target: pressing "Enable" freezes and
captures one real rendered frame's worth of real Render Graph passes, the left-hand tree shows
those real passes (not a "No frame captured yet." message), clicking one shows REAL shader/
blend/Z/stencil/texture/vector/matrix data plus a REAL preview image reconstructed as of that
exact point in the frame, a real multi-frame history ring buffer lets you step backward/forward
through several past captured frames, the Channels/Levels controls actually affect the preview
image, and the whole feature is drivable end-to-end over the existing embedded HTTP server so an
AI agent with no mouse can open the window, enable it, select an event, and visually verify the
result via `GET /get_swapchain` — closing a gap `frame-debugger-1`/`frame-debugger-2` both had to
leave as an accepted limitation.

## Step 2: The Situation (Where are we now?)

Investigated directly in this repository, on `feature/frame-debugger-impl`:

- **`frame-debugger-2` shipped a complete, real, ImGui-wired shell with exactly ONE remaining
  job**: every value it shows is a disabled control or a placeholder message, because
  `BuildPlaceholderFrameDebuggerSnapshot()` (`src/Editor/FrameDebuggerData.h/.cpp`) always returns
  an empty tree. `docs/conventions/frame-debugger.md`'s own "exact glue seams a future real-capture
  campaign should replace" section lists precisely three seams — this campaign is that future
  campaign.
- **The render graph is coarse today, and that is a GIFT for this campaign, not a gap**: real
  frame execution goes through exactly `"GameView"`, `"SceneView"`, `"Present"`
  (`src/Application/RenderPasses.cpp`'s `AddGameViewPass()`/`AddSceneViewPass()`/
  `AddPresentPass()`), plus one real, individually-named GPU-skinning compute pass per skinned
  model this frame (`AddGpuSkinningPasses()`, named from
  `AnimationSystem::GpuSkinningDispatchRequest::name`). `gte::rg::BuildRenderGraphSnapshot()`
  (`src/Renderer/RenderGraph/RenderGraphSnapshot.h/.cpp`) ALREADY turns a compiled graph into a
  real, displayable `RenderGraphSnapshot` — real pass names, real read/write resource names, real
  per-pass `DrawStats` (draw-call/triangle counts), real (tri-state) GPU timing — consumed today by
  the existing "Render Graph" panel (`Panels/RenderGraphPanel.cpp`). This campaign's own snapshot
  builder is a NEW, PARALLEL reshape of that SAME real data, filtered down to Game-View-only
  passes, not a competing capture mechanism.
- **There is exactly ONE Pipeline configuration in the entire engine today** (`Pipeline.cpp`):
  `blendEnable = VK_FALSE` everywhere, depth test always `VK_COMPARE_OP_LESS`, no stencil test
  anywhere. This is a load-bearing simplification for this campaign: "full shader/pipeline-state
  reflection" (the user's own answer, see below) can be a small, genuinely REAL, constant-valued
  free function — no per-material variation exists to reflect yet, so there is nothing to
  fabricate and nothing to guess at.
- **`RenderSystem::Draw()`/`Renderer::Submit()`** (`src/Game/RenderSystem.h/.cpp`,
  `src/Renderer/Renderer.h/.cpp`) is the ONE real call site every Game-View draw call passes
  through, once per visible entity, per frame, per render target. This is the instrumentation
  point PHASE1 hooks — not `FrameRecorder::RecordFrame()` (the legacy, non-graph path, unused by
  Game View since the Render Graph migration) and not `Pipeline`/`Mesh` themselves (both stay
  exactly as dumb as they are today).
- **The "swapchain capture only sees the MAIN viewport" trap**: `ImGuiEditorLayer.cpp` sets
  `io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable` for the whole Editor (confirmed at that
  file's own line ~164), and `BoneViewerWindow.h`'s own header comment already documents the
  consequence explicitly: **any floating, undocked window (including `FrameDebuggerPanel`) can be
  dragged into its own independent OS-level platform window** — which `GET /get_swapchain`
  (`src/Renderer/SwapchainCaptureService.h`) categorically CANNOT see, since it only ever reads
  back the MAIN window's own swapchain image. The user's own explicit note during design review
  ("you need to make frame debugger attached to main window so it can be captured by swapchain")
  is exactly this trap — PHASE7 fixes it with a forced `ImGui::SetNextWindowViewport(mainViewport)`
  pin, copying `DockLayout.cpp`'s own existing `ImGui::SetNextWindowPos(viewport->WorkPos)` call
  for the main dockspace host window as its precedent.
- **The existing HTTP automation precedent to copy**: `EditorUiCommandBridge`
  (`src/Application/EditorUiCommandBridge.h/.cpp`) is the exact shape (a mutex + condition-variable
  request/result pair, `SubmitAndWait()` from the network thread, `TryPeekPendingCommandRequest()`/
  `FulfillCommand()` pumped once per frame from `Application::Run()`) PHASE7's brand-new
  `FrameDebuggerCommandBridge` copies verbatim. **`AGENTS.md`'s own explicit rule is that a
  genuinely new KIND of request gets its OWN bridge, never a new enum value bolted onto an existing
  one built for an unrelated purpose** — Frame Debugger control (enable/capture/select-event/step-
  history/channel/levels) is a clearly distinct concern from "activate this already-existing docked
  tab" (`EditorUiCommandBridge`'s only existing job), so PHASE7 must NOT extend
  `EditorUiCommandKind` — it creates a new bridge type, mirroring the existing one's file layout
  exactly.
- **`/get_texture`'s existing `channel=color|depth` query parameter means something else entirely**
  (`NetworkRoutes.h`'s `ParsedGetTextureQuery::wantsDepth`) — reusing that same query-parameter NAME
  for Frame-Debugger R/G/B/A channel isolation would be a real, user-facing semantic collision.
  PHASE6/PHASE7 must use their own, differently-named parameters (e.g. `?channel=all|r|g|b|a` on a
  brand-new `/frame_debugger/set_channel` route, never on `/get_texture` itself).
- **`RenderGraphDebugTextureRegistry`/`GET /get_texture`/`GET /list_textures`** already exist and
  already publish every named texture the render graph creates/imports each frame — PHASE3/PHASE7
  MAY (optional, not required) also publish the Frame Debugger's own retained preview texture under
  a well-known name through this SAME registry, getting a second, free way to fetch it, but the
  Frame Debugger's own in-panel `ImGui::Image()` display (the primary requirement) does not depend
  on this at all.

## Step 3: The Plan (detailed strategy)

### 3.1 Architecture at a glance

```
Renderer::Submit() / RenderSystem::Draw()          (PHASE1)
   |  (only when FrameDebuggerCaptureContext is ARMED - zero overhead otherwise)
   v
FrameDebuggerCaptureContext                          (PHASE1: aggregates real shader/texture/
   |                                                   vector/matrix facts for the CURRENT frame)
   v
gte::rg::RenderGraphSnapshot (ALREADY REAL, existing) + FrameDebuggerCaptureContext
   |
   v
BuildRealFrameDebuggerSnapshot(...)                  (PHASE2: pure reshape, Game-View-only filter)
   |
   v
FrameDebuggerHistory (ring buffer, N slots)           (PHASE3: capture trigger + retained texture copy)
   |
   v
FrameDebuggerPanel::Build()                           (PHASE4/PHASE5/PHASE6: real tree, real details,
   |                                                    real preview + Channels/Levels)
   v
FrameDebuggerCommandBridge -> /frame_debugger/*        (PHASE7: HTTP automation + main-viewport pin)
```

### 3.2 Locked Design Decisions (from the user's own answers — do not relitigate
these during implementation; if a phase document's plan conflicts with one of
these, the PHASE document is wrong and must be fixed, not this list)

1. **Event-tree granularity is PASS-LEVEL, not per-individual-draw-call.** One leaf event =
   one real `gte::rg::RenderGraphPassSnapshot`-backed pass relevant to the Game View (the
   `"GameView"` pass itself, plus zero-or-more upstream GPU-skinning compute passes feeding it
   this frame) — never one leaf per mesh/entity. This is a deliberate, explicit divergence from
   the reference Unity screenshot's own per-draw-call granularity, chosen for implementation cost
   reasons.
2. **Capture is snapshot-ON-DEMAND, never continuous.** A "frame" is captured exactly once,
   on a real trigger (Enable's false->true edge, a Step while Enabled, or an explicit new
   "Capture" button — see PHASE3), and stays frozen/inspectable until the next trigger fires.
   `FrameDebuggerPanel` never rebuilds its displayed snapshot on every ImGui frame the way
   `RenderGraphPanel`'s un-paused branch does.
3. **A REAL multi-frame history ring buffer exists** (`FrameDebuggerHistory`, PHASE3), holding
   the last `kFrameDebuggerHistoryCapacity` (8) captured frames, each a full, independent,
   already-resolved `FrameDebuggerSnapshot` PLUS its own retained GPU copy texture. A NEW
   Prev/Next "Frame History" mini-toolbar (PHASE4) scrubs across THIS ring buffer.
4. **The pre-existing "N of M" event stepper row keeps ITS OWN original meaning** (position
   within the CURRENTLY-VIEWED captured frame's own event list — there are typically 2-6 real
   leaf events per captured frame under this campaign's pass-level granularity) — it is a
   SEPARATE, DIFFERENT axis from the new frame-history stepper in point 3 above. Do not conflate
   the two into one control; do not repurpose `FormatFrameStepperLabel()`/`ClampSelectedEventIndex()`
   for frame-history navigation — those two pure helpers, from `frame-debugger-2`, are correct
   exactly as they are for the event axis; frame-history navigation is new, separate state
   (`FrameDebuggerHistory::m_cursor` or equivalent) with its own new formatting helper.
5. **Preview reconstruction is a real, cheap "copy right after the one real image-producing
   event finishes" — not a full mid-pass draw-call replay.** Because scope is Game-View-only AND
   granularity is pass-level, the ONLY event in the whole captured tree that ever produces a
   color image is the terminal `"GameView"` pass itself; every GPU-skinning compute event that
   precedes it writes only a buffer. So: selecting the `"GameView"` leaf shows the REAL retained
   texture copy taken right when that pass finished for that historical frame (a true "as it stood
   at this exact point" reconstruction, at zero extra command-buffer-replay complexity); selecting
   any GPU-skinning leaf shows "No Texture" (real, honest — a compute pass has no color image
   output) alongside real numeric/textual details for that compute dispatch.
6. **Shader/state property "reflection" is real, but PASS-scoped, aggregated across every real
   draw call that pass issued this frame** — never per-individual-mesh. For the `"GameView"`
   event: `shaderName` lists every DISTINCT real Pipeline identity actually used this frame (see
   PHASE1's new cosmetic `Pipeline` debug name), `textures` lists every DISTINCT real bound
   `MaterialTexture` debug name, `vectors` includes the real clear color and real aggregate
   `DrawStats` (draw-call count / triangle count), `matrices` includes the real view-projection
   matrix that pass actually rendered with this frame (identical for every draw within one Game
   View pass, by construction — see `RenderSystem::Draw()`), and blend/Z/stencil rows report this
   engine's real, single, constant `Pipeline` configuration (see PHASE1's
   `DescribeStandardPipelineState()`) — genuinely true facts, never fabricated per-mesh detail
   this codebase doesn't have anywhere yet.
7. **Scope is Game View ONLY.** `"SceneView"`/`"Present"` passes are excluded entirely from the
   Frame Debugger's own captured event tree (PHASE2's filter), even though they exist in the same
   underlying `RenderGraphSnapshot` the "Render Graph" panel already displays in full.
8. **Channels (All/R/G/B/A) and Levels become functionally real** (PHASE6) via a brand-new,
   small, dedicated preview-compositing module/shader — explicitly NOT by reusing `/get_texture`'s
   existing `channel=color|depth` parameter (a different, unrelated meaning — see Step 2 above).
9. **Full HTTP automation is added** (PHASE7) via a brand-new `FrameDebuggerCommandBridge` (never
   an extension of `EditorUiCommandBridge` — see Step 2's "AGENTS.md's own explicit rule" note),
   with the window forced onto the MAIN ImGui viewport whenever opened/controlled this way, so
   `GET /get_swapchain` always shows it — closing the "manual verification limitation" both
   `frame-debugger-1` and `frame-debugger-2` had to accept.
10. **Eight phases** (`PHASE1`..`PHASE8`), each independently compilable, chosen for a
    fine-grained, lower-risk rollout mirroring `frame-debugger-2`'s own precedent, per the user's
    own explicit request.

### 3.3 Non-Goals (explicitly out of scope for this campaign)

- Per-individual-draw-call event granularity (Locked Design Decision #1) — a genuine, deliberate
  simplification versus the Unity reference screenshot, not a future TODO this campaign forgot.
- Scene View or Present-pass capture (Locked Design Decision #7) — a clean, documented future
  extension point (PHASE2's Game-View-only filter is a single, obvious, well-isolated line to
  relax later), not attempted here.
- Any per-material/per-mesh blend/Z/stencil VARIATION — this engine has exactly one Pipeline
  configuration today; nothing here invents a second one just to have something to show.
- A full shader-reflection system reading real SPIR-V reflection data / real descriptor-set
  binding tables generically — PHASE1's "shader identity" is a small, cosmetic, hand-authored
  debug string per real `CreatePipeline()` call site, not a generic reflection library.
- True per-draw-call, frame-by-frame command-buffer replay/reconstruction — obviated entirely by
  Locked Design Decision #5's "copy right after the one real image-producing pass" approach.
- Any change to `SceneView`/`Present`'s own rendering behavior, to `Game::Update()`, or to
  gameplay simulation of any kind — this campaign touches rendering-adjacent CAPTURE/DISPLAY code
  only.
- A full Undo/Redo or persistence story for captured frames (the ring buffer is purely an
  in-memory, per-session, best-effort debugging aid — a resize/device-loss can safely drop it).

### 3.4 File-change inventory (full campaign, across all phases — see each phase
file for its own exact per-phase slice)

New files (final state):
- `src/Editor/FrameDebuggerCapture.h`, `src/Editor/FrameDebuggerCapture.cpp` (PHASE1 — the
  `FrameDebuggerCaptureContext` + `DescribeStandardPipelineState()`; PHASE1 decides the exact
  home, see that phase's own Step 3 for why this lives under `src/Editor/` rather than
  `src/Renderer/` despite touching `Renderer`/`RenderSystem`)
- `src/Editor/FrameDebuggerHistory.h`, `src/Editor/FrameDebuggerHistory.cpp` (PHASE3)
- `src/Editor/FrameDebuggerPreviewProcessing.h`, `src/Editor/FrameDebuggerPreviewProcessing.cpp`
  (PHASE6) + a new `Shaders/FrameDebuggerPreview.comp` (PHASE6)
- `src/Application/FrameDebuggerCommandBridge.h`, `src/Application/FrameDebuggerCommandBridge.cpp`
  (PHASE7)
- `tests/Editor/FrameDebuggerCaptureTests.cpp` (PHASE1/PHASE2)
- `tests/Editor/FrameDebuggerHistoryTests.cpp` (PHASE3)
- `tests/Editor/FrameDebuggerPreviewProcessingTests.cpp` (PHASE6)
- `task_manager/frame-debugger-3/PHASE1_COMPLETION_REPORT.md` .. `PHASE8_COMPLETION_REPORT.md`
- `task_manager/frame-debugger-3/CAMPAIGN_COMPLETION_REPORT.md` (written at the end of PHASE8)

Modified files (across the whole campaign):
- `src/Renderer/Pipeline.h`/`.cpp` (PHASE1 — cosmetic debug-name parameter)
- `src/Renderer/Renderer.h`/`.cpp` (PHASE1 — arms/reads the capture context around `Submit()`)
- `src/Game/RenderSystem.h`/`.cpp` (PHASE1 — threads entity/material identity into the capture)
- `src/Renderer/GpuResourceFactory.h`/`.cpp` (PHASE1 — the same new cosmetic `debugName` parameter on
  `CreatePipeline()`, threaded from `Renderer::CreatePipeline()` into `Pipeline`'s constructor)
- `src/Game/Game.h`/`.cpp` (PHASE3 — new defaulted `FrameDebuggerCaptureContext*` parameter on
  `Render()`, forwarded to `RenderSystem::Draw()`'s float-aspect overload only — see PHASE1's Step
  3.1b/PHASE3's Step 3.4b, added during this 2nd-iteration review)
- `src/Application/RenderPasses.h`/`.cpp` (PHASE3 — new parameter on `AddGameViewPass()` threading
  the armed capture-context pointer into its own `Game::Render()` call; `AddPresentPass()`'s own
  direct-render fallback always passes `nullptr` — see PHASE3's Step 3.4b, added during this
  2nd-iteration review)
- `src/Editor/FrameDebuggerData.h`/`.cpp` (PHASE2 — new real builder function(s), no breaking
  change to any existing struct/field)
- `src/Editor/Panels/FrameDebuggerPanel.h`/`.cpp` (PHASE3/4/5/6/7)
- `src/Editor/EditorContext.h` (PHASE3 or PHASE7 — wherever the shared "armed"/history-owning
  state needs to live; see each phase's own analysis)
- `src/Application/Application.h`/`.cpp` (PHASE3 — capture-trigger wiring; PHASE7 — bridge
  construction + per-frame pump)
- `src/Network/NetworkRoutes.h`/`.cpp`, `src/Network/NetworkServer.h`/`.cpp` (PHASE7)
- `CMakeLists.txt`, `tests/CMakeLists.txt` (every phase that adds a new file)
- `AGENTS.md`, `README.md`, `TODO.md`, `docs/README.md`, `docs/conventions/frame-debugger.md`
  (PHASE8)

### 3.5 Note on review depth (for the double-check / 2nd iteration)

**PHASE1 is the single highest-risk phase** — it is the only one that touches the real,
performance-sensitive per-frame draw path (`Renderer::Submit()`/`RenderSystem::Draw()`), and every
later phase's data correctness depends entirely on it being both CORRECT and truly
zero-overhead-when-disarmed. **PHASE7 is the second-highest-risk phase** — it introduces a brand
new cross-thread bridge type and a genuinely new "pin a floating window to the main viewport"
technique with no exact precedent yet in this codebase. Both are explicitly flagged so a reviewing
pass can choose to double-check either (or both) in isolation before reviewing the campaign as a
whole.

### 3.6 Order of work

Work phases 1 -> 8 strictly in order; each does a fast compile check before moving on. Only
PHASE8 does a full clean build (both `GTE_ENABLE_EDITOR=ON` and `=OFF`) + full `ctest` regression
+ a live, HTTP-automation-driven runtime smoke test. See each phase file for its own exact
compile-check command and file-change inventory.
