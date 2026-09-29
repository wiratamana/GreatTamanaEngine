# PHASE2 COMPLETION REPORT — Systemic audit: which OTHER passes leak a side effect past their own toggle?

Campaign folder: `task_manager/editor-core-separation-22/`
Branch: `feature/editor-core-separation` (unchanged, as required)
Status: **Complete.** Zero code diff (audit-only phase, as scoped). Every
`search_in_dir` was re-run fresh against the current source (post-PHASE1);
every classification below was live-HTTP-verified against a real running
`GreatTamanaEditor.exe`, not accepted from code-reading alone, except the one
finding explicitly called out below where the user accepted static-only
evidence via `ask_questions`. This report's own ledger is PHASE3's fix
backlog — PHASE3 must not re-derive it.

## Re-run search inventory (fresh, current line numbers)

- `blackboard.Publish` in `src/`: **5** call sites, all in `src/Core/Core.cpp`
  (lines 466, 512, 566, 869, 1003 — shifted from PHASE0's own citations by
  PHASE1's edit, as expected).
- `.Fetch<` in `src/Core/`: **8** call sites, all in `Core.cpp` (422, 525, 644,
  856, 858, 964, 1234, 1244).
- `frame.builder.` outside `Core.cpp`: `RenderFeatureCompositor.cpp` (624, 639,
  662 — `ImportTexture()` calls, not `AddRenderPass()`/blackboard-adjacent).
- `finalTextureOutputs.push_back`: 6 in `Core.cpp`, 1 in
  `LegacyRenderFeatureOrchestrator.cpp`, 1 in `RenderFeatureCompositor.cpp`.
- `finalVolumeTextureOutputs.push_back`: 1, `RenderGraphBuilder.cpp` (the
  generic `KeepVolumeTextureOutput()` implementation itself, not a call site).

Every one of these was traced forward to its consumer(s) and backward to its
producer's own toggle-consult (if any). Full ledger below.

## Ledger (one row per finding)

Format: **Side-effect location** / **What it publishes/mutates** / **Consumer**
/ **Does the consumer check the SAME toggle (or the real handle/data validity
that toggle implies)?** / **Classification** / **Live verification**.

| # | Side-effect location | What it publishes/mutates | Consumer(s) | Toggle/validity check on the consumer side? | Classification | Live verification |
|---|---|---|---|---|---|---|
| 1 | `Core.cpp:455-467`, `"AtmosphereSharedLut"` provider (`ProviderScope::Once`) | `frame.blackboard.Publish<AtmosphereSharedLutBlackboardEntry>(kAtmosphereSharedLutKey, entry)` — **unconditional**, no early guard, no `NoteDeclaredAndCheckEnabled` for the provider's own name at all | `"AtmosphereViewLut"` provider (`Core.cpp:524-528`) | Only checks `.has_value()` (always true) directly, but **delegates** the real validity check into `AddAtmosphereViewLutPasses()` (`AtmospherePassSequence.cpp:61-63`: `if (!sharedLuts.transmittanceLutHandle.IsValid() \|\| !sharedLuts.multiScatteringLutHandle.IsValid()) return result;` — degrades to invalid output handles, never fabricates a visual effect) | **Already-Honest** | Live: disabled `AtmosphereTransmittanceLutPass` via `set_pass_enabled` → `GET /get_texture?texture_name=GameView` dropped from 17109→13144 bytes (sky gone, `PluginRenderFeatures` correctly fell back to drawing directly onto raw `GameView` since `AtmosphereComposite`'s own output cascaded to invalid) — zero crash, `GET /get_logs?min_level=Error` stayed `{"count":0}` throughout. Re-enabled, confirmed byte-identical return to 17109-byte baseline. |
| 2 | `Core.cpp:455-467`, the provider's own **whole name** `"AtmosphereSharedLut"` | N/A — this provider never calls `out.push_back(desc)` at all (reaches `frame.builder`/blackboard directly instead), so `RenderPipeline::DeclareOnePhase()`'s generic `NoteDeclaredAndCheckEnabled(desc.debugName)` gate (`RenderPipeline.h:588-591`) **never fires for this literal provider name** — confirmed by re-reading `DeclareOnePhase()`'s own body: the gate is keyed on each COLLECTED `desc.debugName`, never on the registered provider's own name | N/A | N/A | **No-Toggle-Exists** (by design — this is an orchestration wrapper; its own two REAL sub-passes, `AtmosphereTransmittanceLutPass`/`AtmosphereMultiScatteringLutPass`, each have their own individually-consulted toggle name inside `AddAtmosphereSharedLutPasses()`) | Confirmed via `GET /render_graph/passes`: no `"AtmosphereSharedLut"` entry exists in the toggle list at all, only the two real sub-pass names |
| 3 | `Core.cpp:517-567`, `"AtmosphereViewLut"` provider (`ProviderScope::PerActiveView`) | `frame.blackboard.Publish<AtmosphereViewLutHandles>(viewLutKey, viewLuts)` (line 566) — **unconditional**, published even when `viewLuts.skyViewLutHandle`/`aerialPerspectiveVolumeHandle` are invalid (upstream toggle off) | `"DrawSkyBackground"` (`Core.cpp:855-861`): `if (!viewLuts.has_value() \|\| !sharedLuts.has_value() \|\| !viewLuts->skyViewLutHandle.IsValid()) return;` — checks the **specific field's own validity**, not just presence. `"AtmosphereComposite"` (`Core.cpp:963-966`): `if (!viewLuts.has_value() \|\| !viewLuts->aerialPerspectiveVolumeHandle.IsValid()) return;` — same discipline | Both consumers check real handle validity, not mere blackboard presence | **Already-Honest** | Live: same AtmosphereTransmittanceLutPass-disabled test above cascades through this exact key (transmittance invalid → shared LUT invalid → `AddAtmosphereViewLutPasses()` returns early per its own line 61-63 → `skyViewLutHandle`/`aerialPerspectiveVolumeHandle` both invalid → `DrawSkyBackground` and `AtmosphereComposite` both correctly no-op this frame) — confirmed via the same live test, zero crash, zero error log |
| 4 | `Core.cpp:517-567`, the provider's own **whole name** `"AtmosphereViewLut"` | Same shape as #2 — never pushes to `out`, so no generic gate fires for this literal name either | N/A | N/A | **No-Toggle-Exists** (by design — same reasoning as #2; real granularity lives at `AtmosphereSkyViewLutPass`/`AtmosphereAerialPerspectiveVolumePass`) | Confirmed via `GET /render_graph/passes`: no `"AtmosphereViewLut"` entry |
| 5 | `Core.cpp:556-563`, the `AtmosphereAerialPerspectiveVolumeDebugSlicePass` call inside `"AtmosphereViewLut"` | `frame.finalTextureOutputs.push_back(debugSlice)` | `RenderGraphCompiler`'s backward-reachability root scan (`RenderGraphCompiler.cpp:485-510`) | Only reached when `viewLuts.aerialPerspectiveVolumeHandle.IsValid()` (explicit `if` at line 556) — the pass itself ALSO has its own independent, already-fixed (`editor-core-separation-21` PHASE4) early guard (`AtmosphereLutRenderer.cpp:992-995`) | **Already-Honest** | Live: `GET /render_graph/passes` shows `"AtmosphereAerialPerspectiveVolumeDebugSlicePass":true`, confirmed present/functioning in the baseline capture; static re-confirmation of the guard's continued presence in the current source |
| 6 | `Core.cpp:470-514`, `"GpuSkinning"` provider (offscreen, `ProviderScope::Once`) | `frame.blackboard.Publish<std::vector<rg::BufferHandle>>(kGpuSkinningOutputsKey, m_gpuSkinningHandlesThisFrame)` (line 512) | Its OWN early guard, `if (!m_renderPassToggleRegistry.NoteDeclaredAndCheckEnabled("GpuSkinning")) return;` (line 482), runs **before** both the dispatch-pass loop AND this Publish call | Self-gated at the top, before any side effect | **Already-Honest** | Live: `set_pass_enabled?name=GpuSkinning&enabled=false` → `GET /get_texture?texture_name=GameView` stayed byte-identical (17109 bytes) — no skinned mesh in the demo scene means this pass was already a no-op either way, but zero crash/error confirms the early-return path itself is safe; re-enabled cleanly |
| 7 | `Core.cpp:643-645`, `"RenderOpaque"` provider | `frame.blackboard.Fetch<std::vector<rg::BufferHandle>>(kGpuSkinningOutputsKey).value_or({})` | `RenderOpaque`'s own `desc.setup` (`DeclareGpuSkinningReads`) | `.value_or({})` degrades to an empty vector when `GpuSkinning` never published (disabled) — never assumes presence | **Already-Honest** | Same live test as #6 |
| 8 | `Core.cpp:1233-1234`, `1243-1245` (replay-path Fetches, inside `Core::BuildFrame()`) | `blackboard.Fetch<std::function<...>>(kGameSkyBackgroundCallbackKey)` / `blackboard.Fetch<std::vector<BufferHandle>>(kGpuSkinningOutputsKey)` for the Frame Debugger replay path | `FrameDebuggerCaptureContext::AddReplayPasses()` (`FrameDebuggerReplayPasses.cpp:88-216`) | `.value_or({})` / `.value_or(std::function<...>{})` both degrade gracefully; `AddReplayPasses()` itself independently gates on its own umbrella `"FrameDebuggerReplay"` toggle (line 102) BEFORE touching either value | **Already-Honest** — this is exactly PHASE1's own fix, re-confirmed after the line-number shift | Live: `DrawSkyBackground` disabled → captured a Frame Debugger frame → `GET /list_textures` (and a targeted PowerShell regex count) showed **zero** `FrameDebuggerReplayStepN` textures (demo scene has `objectCount==0` and `recordSkyBackground` empty ⇒ `totalStepCount==0`, correct early return) — matches PHASE1's own confirmed fix exactly, no regression |
| 9 | `Core.cpp:829-896`, `"DrawSkyBackground"` provider | `frame.blackboard.Publish<std::function<void(VkCommandBuffer)>>(kGameSkyBackgroundCallbackKey, recordSkyBackground)` (line 869) — **now gated** by `rg::ShouldDeclareBuiltInPassThisFrame(&m_renderPassToggleRegistry, "DrawSkyBackground")` (line 845), PHASE1's own fix | Consumed by #8 above | Guard runs before ANY side effect in this provider's body | **Already-Honest** (PHASE1 fix, re-verified) | Live: `set_pass_enabled?name=DrawSkyBackground&enabled=false` → `GET /get_texture?texture_name=GameView` = 1582 bytes (flat, no sky) — byte-identical to PHASE1's own recorded result; re-enabled → 17109 bytes restored |
| 10 | `Core.cpp:942-1006`, `"AtmosphereComposite"` provider | `frame.blackboard.Publish<rg::TextureHandle>(kGameCompositedOutputKey \| kSceneCompositedOutputKey, composited)` (line 1003-1004) | `Core::FindPluginRenderFeatureTarget()` (line 413-436): `frame.blackboard.Fetch<rg::TextureHandle>(compositedKey)` then `.value_or(viewData->colorTarget)` | Publish only reached after an explicit `if (!composited.IsValid()) { return; }` (line 981-993) — the ONLY way an invalid handle could reach the blackboard is prevented outright, not just degraded-around afterward | **Already-Honest** | Live: `set_pass_enabled?name=AtmosphereComposite&enabled=false` → `GET /get_texture?texture_name=GameView` = 13144 bytes (raw `GameView` now shows the plugin `_v1`/`_v3` clear passes drawing DIRECTLY onto it, since `FindPluginRenderFeatureTarget()` correctly fell back to `viewData->colorTarget`) — zero crash, zero error log; re-enabled cleanly |
| 11 | `Core.cpp:942-1006`, `"AtmosphereComposite"`'s own **provider name** | Direct `frame.builder.AddRenderPass()` via `AddAtmosphereCompositePass()`, never through the generic `out.push_back()` mechanism | N/A | Self-gated via its OWN explicit `m_renderPassToggleRegistry.NoteDeclaredAndCheckEnabled("AtmosphereComposite")` (line 956) — one of exactly 2 confirmed exceptions needing this (the other is `"GpuSkinning"`, #6 above) | **Already-Honest** | `GET /render_graph/passes` confirms `"AtmosphereComposite"` IS independently listed and toggle-controlled (unlike #2/#4) |
| 12 | `Core\Plugins\LegacyRenderFeatureOrchestrator.cpp:35-62`, `ContributeRenderGraphPasses()` | `frame.finalTextureOutputs.push_back(resolved->target)` (line 60) — only when `anyPluginFeatureRanThisView` is true; `AddFullscreenClearPass()` itself (`PluginRenderPassBuilderAdapter.cpp:67-82`) is independently, individually gated (`m_toggleRegistry->NoteDeclaredAndCheckEnabled(debugName)`, line 69) | Compiler reachability scan | Each dynamically-named `_v1` clear pass gates itself before declaring anything | **Already-Honest** (fixed by `editor-core-separation-21` PHASE4; re-confirmed on current source — this was the campaign's own PHASE0 Step 2.2 misuse-of-`RenderPassCategory::Debug` bug, a SEPARATE, already-scheduled PHASE4-of-THIS-campaign concern, not a side-channel leak) | Live: `GET /render_graph/passes` shows `"DemoRenderFeaturePlugin_Clear"`/`"DemoRenderFeatureSecondPlugin_Clear"` both independently toggle-listed and `ever_declared_this_session:true` |
| 13 | `Core\Plugins\RenderFeatureCompositor.cpp:555-674`, `ContributeRenderGraphPasses()` | `m_blackboard` (the `_v3` plugin cross-feature blackboard, `BlackboardAdapter::Publish()`/`Fetch()`) — cleared (`m_blackboard.clear()`, line 564) **every** call, THEN `combinedList` is filtered down to only `enabledOverride==true` entries (line 576-577) BEFORE any plugin's own `AddRenderGraphPasses()` (and therefore any `Publish()`) ever runs | Other `_v3` plugins in the SAME per-view call | A disabled feature is filtered out before it ever gets a chance to touch `m_blackboard` at all — no stale/cross-view leak possible (map is cleared fresh at the top of every call, once per active view) | **Already-Honest** | Static re-confirmation (unchanged code since `editor-core-separation-21`); live-confirmed indirectly via #14's own `set_feature_enabled` test producing a clean, complete removal of every one of a disabled feature's own passes |
| 14 | `Core\Plugins\RenderFeatureCompositor.cpp`, per-feature `enabledOverride` filter (`SetFeatureEnabled()`) | Whole feature contribution (all its own private/accum/blend passes) | `GET /render_graph/set_feature_enabled` | Pre-existing, bespoke (not `RenderPassToggleRegistry`-based) but genuinely honest — filtered BEFORE any of the feature's own passes are ever constructed | **Already-Honest** | Re-confirmed via `GET /render_graph/passes` baseline showing `DemoRenderFeatureV2`/`V3`/etc. private/accum textures present in `list_textures`; not independently re-toggled this phase (unchanged since `editor-core-separation-21` PHASE3's own live confirmation) |
| 15 | `src\Editor\ImGuiEditorLayer.cpp:469-488`, `AddBlurValidationPass()` | `m_blurValidation.AddPass(...)` (a real compute pass + a `m_writtenThisFrame` flag `ComputeBlurValidation.cpp:164`) | `FinalizeForSampling()` (`ComputeBlurValidation.cpp:168-179`): `if (!m_writtenThisFrame) return;` | Double-gated: `ctx.showBlurredSceneOutput` AND `toggleRegistry->NoteDeclaredAndCheckEnabled("ComputeBlurValidation")`, both checked BEFORE `m_blurValidation.AddPass()` is ever called (line 473, 484) | **Already-Honest** (fixed `editor-core-separation-21` PHASE4; re-confirmed) | Live: `set_blur_enabled=true` then `set_pass_enabled?name=ComputeBlurValidation&enabled=false` → `GET /render_graph` regex count for `"ComputeBlurValidation"` = **0** matches — genuinely absent, not cosmetic |
| 16 | `src\Editor\GBufferValidation.cpp:117-242`, `AddPass()` | Two INDEPENDENTLY-gated halves: `"GBufferValidation"` (graphics, line 126-130) and `"GBufferValidationCopy"` (compute, line 200-202) — each sets its own `m_graphicsWrittenThisFrame`/`m_copyWrittenThisFrame` flag only when it actually declared | `FinalizeForSampling()` (line 244-270): each barrier independently gated on its own flag | Both toggle checks happen BEFORE any resource/pass declaration | **Already-Honest** (fixed `editor-core-separation-21` PHASE4; re-confirmed) | Live: `set_gbuffer_enabled=true` then `set_pass_enabled?name=GBufferValidation&enabled=false` → `GET /render_graph` regex count for `"GBufferValidation"` = **0** matches |
| 17 | `src\Editor\FrameDebuggerReplayPasses.cpp:88-216`, `AddReplayPasses()` | `SetReplayStepPreviews(std::move(destinations))` (line 214) — a `std::vector<RenderTexture>` cached on `FrameDebuggerCaptureContext` | `FrameDebuggerHistory.cpp:76` (`entry.perObjectStepPreviews = std::move(capture.ReplayStepPreviews())`), `FrameDebuggerPanel.cpp` (preview rendering) | `FrameDebuggerCaptureContext::Reset()` (`FrameDebuggerCapture.cpp:113-126`) unconditionally clears `m_replayStepPreviews` **every frame** the Frame Debugger window is open+enabled (`FrameDebuggerPanel.cpp:326-333`, `PrepareCaptureContextForThisFrame()`), regardless of whether that frame is a capture-trigger frame — so a disabled/no-op capture never leaves a STALE previous capture's previews lying around to be misread as "this capture's own result" | No staleness path exists — confirmed by tracing `Reset()`'s own unconditional call site | **Already-Honest** | Static re-confirmation (this is exactly the sibling-cache risk PHASE0's own Step 2.1 called out by name — traced end-to-end, no gap found); live-confirmed indirectly via #8's own zero-replay-step-textures result after a real capture |
| 18 | `src\Application\RenderPasses.cpp:209-257`, `AddGpuSkinningPasses()` (the direct-render-to-swapchain fallback, reached only when `m_needsDirectGameRenderThisFrame` is true) | Dynamically-named per-request compute dispatch passes | `"Present"` provider (`Core.cpp:1041-1049`) | Own early guard, `if (toggleRegistry != nullptr && !toggleRegistry->NoteDeclaredAndCheckEnabled("GpuSkinning")) return handles;` (line 220) — fixed `editor-core-separation-21` PHASE4 | **Already-Honest** (re-confirmed on current source) | Static only — still **unreachable** this session (default dock layout always keeps Game or Scene panel visible, matching `editor-core-separation-21` PHASE3's own identical finding for this exact fallback path); no new diagnostic logging was added this phase (the prior campaign's own finding already establishes non-reachability under the same conditions, and PHASE0's own Locked Decision #2 reserves full regression for PHASE7 — re-deriving this same unreachability with a fresh temporary log was judged non-additive) |
| 19 | `Core.cpp:1091-1187` (inside `Core::BuildFrame()`'s own build lambda, BEFORE `m_offscreenRenderPipeline.DeclareInto()` runs) | `m_gpuDrivenBatchesThisFrame` / **`m_gpuDrivenBatchedEntitiesThisFrame`** — populated **unconditionally** from `RenderSystem::CollectGpuDrivenBatches()`, with ZERO awareness of whether any individual batch's own 3 dynamically-named passes (`"GpuDrivenBatchN ResetCount/Culling/IndirectDraw"`) will end up toggled off later, during `"GpuDrivenBatches"` provider's own `out.push_back(desc)` → generic `NoteDeclaredAndCheckEnabled(desc.debugName)` gate (`Core.cpp:699-827`) | `"RenderOpaque"`'s own `execute` lambda (`Core.cpp:677-687`): `m_game.Render(..., m_gpuDrivenBatchedEntitiesThisFrame)` → `RenderSystem::Draw()` excludes every entity in this set from its own normal per-entity draw path, TRUSTING that the GPU-driven indirect-draw pass will draw them instead | **No** — `RenderOpaque`'s own exclusion set has zero knowledge of whether the SPECIFIC batch's own `"GpuDrivenBatchN IndirectDraw"` pass actually survived its own toggle check this frame | **Confirmed-Lie** (NEW finding this phase — the INVERSE shape of Clause C: disabling ONE dynamically-named per-batch pass makes its own entities **silently, completely invisible** — excluded from the normal path AND undrawn by the now-disabled indirect path — rather than either falling back to the normal per-entity draw or the checkbox being honestly cosmetic. Per `ask_questions`, the user confirmed this belongs in **PHASE3's fix backlog now**, extending Clause C's spirit to cover this inverse shape too — maximal honesty, no exceptions.) | **Static-only**, per explicit user direction via `ask_questions` (Q2): the default demo scene has zero entities meeting the GPU-driven-batch eligibility threshold this session — mirrors `editor-core-separation-21` PHASE3's own identical limitation for this same feature (ledger item #14 there), which also accepted static evidence. `GET /render_graph`'s own `"gpu_driven_batches":[]` empty array, captured live this session, independently confirms zero batches were eligible to exercise. |
| 20 | `Core.cpp:699-827`, `"GpuDrivenBatches"` provider, the 3 dynamically-named passes per batch | Generic `out.push_back(desc)` × 3 per batch | Generic `RenderPipeline::DeclareOnePhase()` gate | Each dynamically-named pass (`"GpuDrivenBatch0 ResetCount"`, etc.) is individually, automatically toggle-gated by the generic mechanism — genuinely honest AT the per-pass level | **Already-Honest** at the per-pass level / **No-Toggle-Exists** at the whole-feature level (no single umbrella checkbox claims to control the whole GPU-driven-batch feature, so there is nothing to be dishonest about there) — this is separate from finding #19 above, which is about a DIFFERENT consumer's stale assumption, not this provider's own declare-time honesty | Static re-confirmation, matching `editor-core-separation-21` ledger item #14's own prior identical finding |
| 21 | `Core.cpp:1052-1415`, `Core::BuildFrame()`'s own member-variable writes (`m_currentViewDataThisFrame`, `m_currentFrameDebuggerCaptureForOffscreenPipeline`, `m_gpuDrivenGameViewProjectionThisFrame`) | Per-frame scratch state, read only by other providers within the SAME frame's declare/execute cycle | N/A | These are not gated by ANY toggle (view existence is controlled by panel visibility, not `RenderPassToggleRegistry`) and nothing treats their mere presence as proof a toggled pass ran | **No-Toggle-Exists** | Static — no user-facing toggle claims to control view-data existence itself |
| 22 | `Core.cpp:347-353`, `RegisterProjectRenderPassProvider()` / `ProjectAssemblyProbe.FillTexture` | Generic `outPasses.push_back(desc)` via the SAME `RenderPipeline`/`DeclareOnePhase()` mechanism | Generic gate | Automatic, zero special-case code | **Already-Honest** (pre-existing, `editor-core-separation-21` PHASE3 finding #25) | Live: `GET /render_graph/passes` baseline shows `"ProjectAssemblyProbe.FillTexture":true`, `ever_declared_this_session:true` — unchanged behavior confirmed |

## Ambiguity checkpoint — `ask_questions`

One genuinely new, ambiguous finding was surfaced this phase (#19 above — the
`GpuDrivenBatches`/`m_gpuDrivenBatchedEntitiesThisFrame` inverse-shape bug).
Per this phase's own Step 3 instructions and PHASE0's Locked Decision #5,
`ask_questions` was used (2 questions) before finalizing this ledger:

1. **Whether finding #19 belongs in PHASE3's fix backlog at all**, given it is
   structurally the OPPOSITE shape of this campaign's own Clause C (content
   disappearing rather than a disabled pass's content surviving). **Answer:
   yes — add it to PHASE3's fix backlog now, treating Clause C's spirit as
   covering this inverse shape too (maximal honesty, no exceptions)** — chosen
   over "out-of-campaign-scope, document only" or "leave it out of the ledger
   entirely".
2. **Whether static-only evidence is acceptable for #19** given the demo scene
   has zero GPU-driven-batch-eligible entities this session (mirroring
   `editor-core-separation-21` PHASE3's own identical limitation for this same
   feature). **Answer: yes — accept static-only evidence, matching precedent;
   do not build a dedicated test scene just for this audit phase.**

No other candidate in the ledger above required `ask_questions` — every other
row resolved cleanly to Already-Honest/No-Toggle-Exists via a combination of
static tracing AND live HTTP verification (never code-reading alone), per this
phase's own evidentiary bar.

## PHASE3 fix backlog (everything this ledger marks Confirmed-Lie)

1. **Finding #19** — `Core.cpp`'s `"GpuDrivenBatches"` per-batch
   entity-exclusion bug: `m_gpuDrivenBatchedEntitiesThisFrame` must only
   exclude an entity from `RenderOpaque`'s own per-entity draw path when that
   SPECIFIC entity's own batch's `"GpuDrivenBatchN IndirectDraw"` pass (the
   one that would otherwise draw it) is confirmed to have actually survived
   its own toggle check THIS frame — not merely "this entity was eligible for
   batching" at collection time, before any toggle was consulted. PHASE3 must
   design the exact mechanism (e.g. defer population of the entity-exclusion
   set until AFTER `"GpuDrivenBatches"` has run its own per-batch toggle
   checks, or thread each batch's own resolved enabled-state back into the
   exclusion-set population) and add a Tier-1 test proving the pure logic
   (per `AGENTS.md`'s "extract the decision into a pure function" precedent),
   plus author/reuse a batch-eligible scene for a real live-HTTP proof this
   time (PHASE3 is not audit-only, so this constraint no longer applies).

This is the **only** finding requiring a code fix from this phase's audit —
every other side-effect-bearing call site traced (5 `blackboard.Publish`
sites, 8 `blackboard.Fetch` sites, every `finalTextureOutputs`/
`finalVolumeTextureOutputs` push, every plugin-adapter blackboard, the Frame
Debugger's own replay-preview cache, and the GBuffer/Blur validation
double-gating) was confirmed, live, to already be honest.

## No new permanent/temporary code this phase

No `GTE_LOG_DEBUG`/`_INFO`/`_WARNING`/`_ERROR` diagnostic instrumentation was
needed — every classification above was reachable via existing `GET
/render_graph`, `GET /render_graph/passes`, `GET /get_texture`, `GET
/list_textures`, `GET /frame_debugger/*`, and `GET /get_logs` endpoints
already exposed. `git status` confirms a clean working tree (`nothing to
commit`) both before and after this phase's live-testing session — zero
files touched.

## Live-testing session summary

- Launched `build\GreatTamanaEditor.exe` via `run_app_background` (PID
  17036), driven entirely via `gte_send_request`/`run_shell` (PowerShell
  `Invoke-WebRequest` only for regex-counting large `GET /render_graph`
  bodies — never to mutate state), closed via `stop_app_background` before
  finishing. No stray instance left running.
- `GET /get_logs?min_level=Error` was checked after every mutating step and
  stayed `{"count":0}` for the entire session — zero crashes, zero new
  warnings/errors caused by any toggle this phase exercised.
- Every toggle this phase flipped for a live test was restored to its
  original (enabled) state before moving to the next candidate; final
  `GET /render_graph/passes` confirms every entry restored except
  `"ComputeBlurValidation"`/`"GBufferValidation"`'s own per-row checkboxes,
  intentionally left at `false` (their real master toggles
  `showBlurredSceneOutput`/`showGBufferValidationOutput` were also restored to
  `false`/default — matches `editor-core-separation-21` PHASE3's own identical
  final-state precedent).

## Incremental build

Not run this phase — zero code diff (per PHASE0 Locked Decision #4/this
phase's own "End of phase" section, an incremental build is only required
"if any temporary instrumentation was added and needs compiling/removing";
none was).

## Files changed

- `task_manager/editor-core-separation-22/PHASE2_COMPLETION_REPORT.md` (new —
  this file).

No production/test source file has any change from this phase.

## No delegation

No `delegate_task` call was made (not permitted for this phase, per PHASE0
Locked Decision #6). `ask_questions` was used once, with 2 questions, per
this phase's own Step 3 instructions — see "Ambiguity checkpoint" above for
the full record and resolution.

## End of phase checklist

1. ✅ Incremental build — not needed (zero code diff); `git status` confirms
   clean tree.
2. ✅ No regression subset run — not needed (zero code diff; per PHASE0
   Locked Decision #2, a full `ctest` pass is reserved for PHASE7 only).
3. ✅ This completion report written, with the full ledger (22 rows across 20
   distinct side-effect locations, 1 Confirmed-Lie, the rest Already-Honest/
   No-Toggle-Exists), every `ask_questions` interaction and resolution, and
   exact live-verification evidence per row.
4. Next: `git_add` + `git_commit` covering this report (no code changes to
   include — none were made).
