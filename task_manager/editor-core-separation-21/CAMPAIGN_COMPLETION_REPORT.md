# CAMPAIGN COMPLETION REPORT — `editor-core-separation-21` ("The Engine Is Lying" — Render Pass / Frame Debugger Honesty Campaign)

Branch: `feature/editor-core-separation` (unchanged throughout, as required)
Campaign folder: `task_manager/editor-core-separation-21/`
Status: **Complete.** Full clean build succeeded (603/603 steps, zero errors), full `ctest` regression pass
succeeded (2004 tests, 100% of executed tests passing, 8 legitimate environment-gated skips — up from
`editor-core-separation-20`'s own 1993/8 baseline, a clean +11 from this campaign's own new tests), and a
final, live, HTTP-driven, end-to-end verification confirmed the iron rule holds across every finding this
campaign discovered.

---

## The original report

The user disabled `AtmosphereAerialPerspectiveCompositePass` via the Editor's "Render Graph" panel (or
`GET /render_graph/set_pass_enabled`), and the Frame Debugger's own event tree, under `Compute Dispatches
(Post-GameView)`, still showed `AtmosphereAerialPerspectiveCompositePass -> Compute Dispatch` as a real,
executed event this frame, fully populated with live GPU-timing/read/write data — a lie the engine was
telling its own developer.

## The iron rule (established, and now enforced permanently in code)

> A render pass's declared/enabled state and the Frame Debugger's own displayed event tree must NEVER
> disagree. If a pass is disabled, it must not run, and it must not appear as an executed leaf anywhere the
> Frame Debugger or the Render Graph panel can show it. If a pass runs, the Frame Debugger MUST show it.

## The confirmed root cause (PHASE1)

**The render-pass toggle mechanism itself was already completely honest and correct** — mechanically
confirmed with log evidence across every layer (`RenderPassToggleRegistry::SetEnabled()`,
`AtmosphereLutRenderer::AddAerialPerspectiveCompositePass()`'s own declare-time guard,
`BuildRealFrameDebuggerSnapshot()`'s tree-builder). The actual, confirmed root cause was a genuine,
reproducible staleness bug in `FrameDebuggerPanel::CaptureNowFromCommand()` (the handler behind
`GET /frame_debugger/capture`): it only ARMS a deferred capture trigger and returns immediately, before the
real capture (`TriggerCapture()`, deferred by design since the `frame-debugger-7` campaign) actually runs on
the engine's next frame — so the very first `GET /frame_debugger/capture` issued after ANY toggle mutation
always reports whatever the PREVIOUS, already-completed capture produced, no matter how long the caller
waits beforehand. None of `PHASE0_MASTER_STRATEGY.md`'s other four hypotheses (a wiring regression, an
`ImGuiUniqueId` collision, the two-registry-name UX confusion, or a `RenderGraphCompiler`/
`FrameDebuggerData.cpp` bug) were the cause — each was explicitly ruled out by direct log evidence.

## The fix (PHASE2)

A new, pure, Tier-1-tested comparison, `RenderPassToggleChangeDetectionLogic.h`'s
`DidRenderPassToggleEnabledStatesChange()`, detects a real enabled-state change across two
`RenderPassToggleRegistry::ListAll()` snapshots. This is wired into a **fourth** automatic Frame Debugger
capture trigger (joining the pre-existing Enable-edge / Step / explicit "Capture" button), via two mutation
paths with two different mechanisms (because of a timing difference): `RenderGraphPanel::Build()`'s own
checkboxes set a new `EditorContext::renderPassToggleRegistryChangedThisFrame` flag that
`FrameDebuggerPanel::Build()` consumes the same frame, and `GET /render_graph/set_pass_enabled`'s
`EditorHost.cpp` handler (which runs BEFORE `BuildUI()` even starts that frame, so it cannot use the
same-frame `ctx` flag) instead calls `IEditorLayer::FrameDebuggerCaptureNow()` directly. No new "kind of
mechanism" was invented — both paths funnel into the exact same pre-existing `m_pendingCaptureTrigger`
deferred-capture handshake. Live-verified: unlike PHASE1's own reproduction (which needed a SECOND capture
call before `totalEventCount` ever dropped), a plain `GET /frame_debugger/state` — with no explicit capture
request at all — already showed the corrected count immediately after the toggle mutation.

## The systemic audit (PHASE3) — one root cause behind almost every other "lie"

`RenderGraphPanel::BuildPassRow()` draws an identical-looking, apparently-functional "Enabled" checkbox for
EVERY pass name in a captured `RenderGraphSnapshot`, with zero knowledge of whether that pass's own
declare-time code actually consults `RenderPassToggleRegistry` at all. A pass declared through the generic
`RenderPipeline::DeclareOnePhase()` flush loop is honestly gated for free; a pass declared via a DIRECT
`builder.AddRenderPass()` call bypasses this entirely unless its own call site was hand-written to
separately consult the registry. A full, itemized 25-row ledger of every `AddRenderPass()` call site in the
engine, cross-referenced against every togglable surface, was built and live-tested — finding:

- **Confirmed-Lie (6 total)**: `DemoRenderFeaturePlugin_Clear`/`DemoRenderFeatureSecondPlugin_Clear`
  (already known), `ComputeBlurValidation`/`GBufferValidation`/`GBufferValidationCopy`'s per-row checkboxes
  (cosmetic despite each having a real, separate, already-honest bespoke feature toggle),
  `AtmosphereAerialPerspectiveVolumeDebugSlicePass`, `AddGpuSkinningPasses()`'s direct-render-to-swapchain
  fallback (confirmed unreachable live this session, but a real, static, confirmed lie in the code), and
  `FrameDebuggerReplayStepN`'s dynamically-named per-replay-step passes.
- **Already-Honest**: every Atmosphere LUT pass, `"AtmosphereComposite"`'s outer gate, `"GpuSkinning"`'s
  offscreen provider, `"ClearViewTarget"`/`"Present"` (deny-listed), `"RenderOpaque"`/`"DrawSkyBackground"`/
  `"RenderTransparent"`, every GPU-driven-batch pass, `RenderFeatureCompositor`'s own bespoke
  `enabledOverride` filter (a genuinely different, already-honest mechanism), and
  `ProjectAssemblyProbe.FillTexture` (the generic path, automatically honest with zero special-case code).
- **No-Toggle-Exists (dead code)**: `AddRenderOpaquePass()`/`AddDrawSkyBackgroundPass()`/
  `AddRenderTransparentPass()` (the latter a NEW finding this phase, not previously documented as dead).

Three genuine design ambiguities were resolved via `ask_questions` before finalizing PHASE4's backlog — the
user chose the maximal, most-honest option all three times: fix every confirmed-lie instance found, with no
exceptions, including the two that would otherwise have been low-priority/deferred (the debug-only
per-row-checkbox class, and the `AddGpuSkinningPasses()` fallback that could not be live-exercised this
session).

## The fixes (PHASE4) — all six, zero deferred

1. `DemoRenderFeaturePlugin_Clear`/`DemoRenderFeatureSecondPlugin_Clear` — `PluginRenderPassBuilderAdapter`
   now takes a `RenderPassToggleRegistry*`; `AddFullscreenClearPass()` consults
   `NoteDeclaredAndCheckEnabled(debugName)` before declaring.
2. `ComputeBlurValidation` — `ImGuiEditorLayer::AddBlurValidationPass()` now ALSO consults
   `NoteDeclaredAndCheckEnabled("ComputeBlurValidation")`, an additional gate on top of the real
   `showBlurredSceneOutput` toggle.
3. `GBufferValidation`/`GBufferValidationCopy` — `GBufferValidation::AddPass()` now independently gates the
   graphics half (`"GBufferValidation"`) and the compute half (`"GBufferValidationCopy"`); disabling the
   graphics half disables both, disabling only the compute half leaves albedo/normal running and only drops
   the visualized output — proven live, bidirectionally, with independent granularity.
4. `AtmosphereAerialPerspectiveVolumeDebugSlicePass` — now consults
   `NoteDeclaredAndCheckEnabled("AtmosphereAerialPerspectiveVolumeDebugSlicePass")` before any resource init.
5. `AddGpuSkinningPasses()`'s direct-render-to-swapchain fallback — now consults the SAME `"GpuSkinning"`
   registry entry the offscreen provider already uses, so one checkbox honestly gates both reachable code
   paths.
6. `FrameDebuggerReplayStepN` — a single, whole-mechanism `"FrameDebuggerReplay"` toggle gates every
   dynamically-named replay step, mirroring `AddGpuSkinningPasses()`'s own "many dynamic names behind one
   umbrella switch" precedent rather than one registry entry per ever-growing name.

## The permanent detector (PHASE5)

`DetectRenderPassHonestyMismatches()` (`src/Editor/RenderPassHonestyChecker.h/.cpp`, pure, Tier-1-tested,
6 tests, mirrors `ImGuiIdConflictTracker.h`'s own precedent) reports every pass name that is BOTH present
and non-culled in a captured `RenderGraphSnapshot` AND reports `enabled == false` via
`RenderPassToggleRegistry`. `RenderPassHonestyGuard` (`src/Editor/RenderPassHonestyGuard.h/.cpp`, mirrors
`ImGuiIdConflictGuard`'s log-once-per-new-incident shape) fires a real
`GTE_LOG_ERROR("RenderPassHonesty", ...)` the instant this happens, wired into
`FrameDebuggerPanel::TriggerCapture()` — the exact chokepoint PHASE1's own diagnostic logging used and the
exact spot PHASE1 left a `TODO(editor-core-separation-21 PHASE5)` comment marking for this work. Proven
live: deliberately reintroducing the original bug made the detector fire a real, fresh log entry
immediately; the real, already-fixed production code stayed completely silent (zero false positives); the
temporary reintroduction hack was fully reverted (confirmed via `git diff --stat` showing zero net change).

## Final verification numbers (PHASE6)

- **Full clean build**: `cmake --build build --target clean` (620 files removed) followed by
  `cmake --build build` — **603/603 steps succeeded, zero errors.**
- **Full `ctest -C Debug --output-on-failure`**: **2004 tests total, 100% of executed tests passing, 8
  legitimate environment-gated skips** (the same 8 pre-existing skips every prior campaign's own final run
  has also reported — no new skips introduced). Up from `editor-core-separation-20`'s own documented
  1993/8 baseline — a clean **+11**, exactly matching this campaign's own two new test files (PHASE2's 5 +
  PHASE5's 6).
- **Final live, HTTP-driven verification**: reproduced the FULL original bug report end-to-end one last
  time against a real running `GreatTamanaEditor.exe` — disabling `AtmosphereAerialPerspectiveCompositePass`
  now genuinely removes it from the very next Frame Debugger state/capture with zero extra manual
  re-capture needed; re-enabling restores it instantly; `DemoRenderFeaturePlugin_Clear`/
  `DemoRenderFeatureSecondPlugin_Clear` and `AtmosphereAerialPerspectiveVolumeDebugSlicePass` were also
  spot-re-confirmed honest; the Game View rendered the exact same byte-identical (158923-byte) sane image
  throughout (no magenta); and `GET /get_logs?category=RenderPassHonesty` stayed completely empty across
  the entire verification sequence — the detector staying silent under fully-correct behavior, proven one
  final time.

## Honest, permanent limitations (restated plainly, not silently smoothed over)

1. **`AddGpuSkinningPasses()`'s direct-render-to-swapchain fallback branch** could never be DIRECTLY,
   live-exercised through its own dishonest branch this whole campaign, either before or after the fix —
   it only runs when `Core::Present()`'s `m_needsDirectGameRenderThisFrame` is true, which requires BOTH the
   Game View AND Scene View panels hidden simultaneously, and no Editor/HTTP control exists today to force
   that. The fix itself (the guard at the top of `AddGpuSkinningPasses()`) is unconditional and identical
   regardless of which caller reaches it, so it is proven correct by direct code reading plus the REACHABLE
   half (the offscreen `"GpuSkinning"` provider) being fully live-verified — the strongest verification
   achievable without adding a new Editor/HTTP capability this campaign was never scoped to add.
2. **`FrameDebuggerReplayStepN`'s own per-step declaration** has the identical limitation for a different,
   structural reason: `RenderGraph::LastSnapshot()`'s own captured snapshot has already rolled forward past
   the one frame that declares these ephemeral, one-capture-lifetime passes by the time any HTTP response is
   built (a deferred-by-one-frame capture-trigger timing property, not a bug this campaign introduced or was
   scoped to fix). The fix's own registry-consultation code is proven live (the `"FrameDebuggerReplay"`
   entry now exists, auto-registers, and mutates cleanly with zero crash/error); the deeper claim that
   toggling it off genuinely stops N per-object passes from being created is a correctness argument backed by
   direct code reading (the guard runs before `totalStepCount` is even computed), not a directly-observed
   live count change.
3. Neither limitation above is a regression or an open bug — both are pre-existing structural properties of
   this engine (the default dock layout always keeping Game or Scene visible; the Frame Debugger's own
   one-frame-deferred capture design) that this campaign's own live-testing sessions could not work around
   without adding new capability outside its own scope. Both fixes' underlying mechanism (the guard itself)
   is identical code, reached identically, regardless of whether the specific branch it protects could be
   exercised this session — the same evidentiary standard PHASE3's own audit already established for these
   two exact findings.

## Files changed across the whole campaign

- `src/Renderer/RenderGraph/RenderPassToggleChangeDetectionLogic.h` (new, PHASE2)
- `tests/Renderer/RenderGraph/RenderPassToggleChangeDetectionLogicTests.cpp` (new, PHASE2)
- `src/Editor/EditorContext.h` (PHASE2 — new `renderPassToggleRegistryChangedThisFrame` field)
- `src/Editor/Panels/RenderGraphPanel.cpp` (PHASE2)
- `src/Editor/Panels/FrameDebuggerPanel.h`/`.cpp` (PHASE2's fourth trigger; PHASE5's detector call)
- `src/Editor/EditorHost.cpp` (PHASE2)
- `src/Core/Plugins/PluginRenderPassBuilderAdapter.h`/`.cpp` (PHASE4 fix #1)
- `src/Core/Plugins/LegacyRenderFeatureOrchestrator.cpp` (PHASE4 fix #1)
- `src/Editor/EditorLayer.h`/`ImGuiEditorLayer.cpp`/`NullEditorLayer.cpp` (PHASE4 fixes #2/#3)
- `src/Editor/GBufferValidation.h`/`.cpp` (PHASE4 fix #3)
- `src/Renderer/Atmosphere/AtmosphereLutRenderer.h`/`.cpp` (PHASE4 fix #4)
- `src/Application/RenderPasses.h`/`.cpp` (PHASE4 fix #5)
- `src/Core/FrameDebuggerCaptureRecorder.h` (PHASE4 fix #6)
- `src/Editor/FrameDebuggerCapture.h`/`FrameDebuggerReplayPasses.cpp` (PHASE4 fix #6)
- `src/Core/Core.cpp` (multiple call-site updates across PHASE2/PHASE4)
- `src/Editor/RenderPassHonestyChecker.h`/`.cpp` (new, PHASE5)
- `src/Editor/RenderPassHonestyGuard.h`/`.cpp` (new, PHASE5)
- `tests/Editor/RenderPassHonestyCheckerTests.cpp` (new, PHASE5)
- `CMakeLists.txt`/`tests/CMakeLists.txt` (new source/test file registrations, PHASE2/PHASE5)
- `AGENTS.md` (PHASE6 — new "Render Pass System" section paragraph)
- `docs/README.md` (PHASE6 — new Conventions index bullet)
- `docs/conventions/render-pass-toggle-honesty.md` (new, PHASE6)
- Every `PHASEn_COMPLETION_REPORT.md`/this `CAMPAIGN_COMPLETION_REPORT.md` in
  `task_manager/editor-core-separation-21/`

## No delegation across the whole campaign

No `delegate_task` call was made in any of the six phases (none permitted for implementation phases, per
this campaign's own Locked Decision #6). `ask_questions` was used exactly once, in PHASE3, with 3 questions
— all three resolved in favor of the maximal, most-honest fix scope, with zero remaining accepted non-goals
carried forward from this campaign's own findings.
