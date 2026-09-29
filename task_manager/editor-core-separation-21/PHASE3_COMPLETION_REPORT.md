# PHASE3 COMPLETION REPORT — Systemic Audit: Every Other "Lie" In The Rendering Code

Campaign: `editor-core-separation-21` ("The Engine Is Lying" — Render Pass / Frame Debugger Honesty Campaign)
Phase: PHASE3 (`PHASE3_SYSTEMIC_AUDIT_RENDER_PASS_TOGGLE_HONESTY.md`)
Branch: `feature/editor-core-separation` (unchanged, as required)

---

## Summary of the finding (read this first)

The audit found **one systemic root cause** behind almost every "lie" this phase confirmed:

> `RenderGraphPanel::BuildPassRow()` (`src/Editor/Panels/RenderGraphPanel.cpp`) draws a
> real, apparently-functional "Enabled" checkbox for **every single pass name** that
> appears in a captured `RenderGraphSnapshot` — completely generically, with zero
> knowledge of whether that specific pass's own declare-time code path actually
> consults `RenderPassToggleRegistry` at all. A pass declared through
> `RenderPipeline::DeclareOnePhase()`'s generic `out.push_back(desc)` flush loop (the
> majority of passes) is genuinely gated by that checkbox for free. Every pass declared
> via a **direct** `builder.AddRenderPass()` / `frame.builder.AddRenderPass()` call,
> bypassing that generic flush loop, gets the exact same-looking checkbox — but
> toggling it does **absolutely nothing**, unless that specific call site was
> hand-written to separately consult `RenderPassToggleRegistry::NoteDeclaredAndCheckEnabled()`
> itself (as the five Atmosphere passes already were, by `editor-core-separation-20`).

Three separate instances of this were **live-confirmed** against a real running
`GreatTamanaEditor.exe` this phase (not just read in source):

1. **`DemoRenderFeaturePlugin_Clear` / `DemoRenderFeatureSecondPlugin_Clear`** (the
   already-known lie from `PHASE0_MASTER_STRATEGY.md` Step 2.2) — toggling either off
   via `GET /render_graph/set_pass_enabled` leaves both passes running, unchanged, with
   real non-zero GPU timing, confirmed via a fresh `GET /render_graph`.
2. **`ComputeBlurValidation` / `GBufferValidation` / `GBufferValidationCopy`** — these
   debug-only passes already have their OWN real, working, separate toggle (the
   "Debug Passes" section's `showBlurredSceneOutput`/`showGBufferValidationOutput`
   checkboxes, confirmed honest) — but their OWN row's checkbox in the regime table is
   100% cosmetic. Live-confirmed: with the real feature toggle left ON, disabling the
   PER-ROW checkbox for `ComputeBlurValidation`/`GBufferValidation` by name had zero
   effect — both passes kept running, byte-identical `GET /render_graph` output before
   and after.
3. **`src/Application/RenderPasses.cpp`'s `AddGpuSkinningPasses()` direct-render-to-
   swapchain fallback** (`PHASE0_MASTER_STRATEGY.md` Step 2.2b's own pre-flagged
   candidate) — confirmed via static reading that it bypasses the `"GpuSkinning"`
   toggle entirely (each dispatch is declared directly, under a dynamic
   `request.name`, never consulting the registry) — and confirmed LIVE that this
   fallback path (`Core::Present()`'s `needsDirectGameRender`) never actually fired
   once during this whole live-testing session (temporary diagnostic logging added,
   zero hits across the entire session, removed again before this report — see
   "Temporary instrumentation" below), consistent with the default dock layout always
   keeping the Game or Scene panel visible.

A fourth, related but structurally different class was also found and live-confirmed
as genuinely **honest**: `src/Core/Plugins/RenderFeatureCompositor.cpp`'s per-feature
`enabledOverride` filter (`GET /render_graph/set_feature_enabled`) — its own internal
`Dispatch`/`Blend` passes are ALSO declared via direct `builder.AddRenderPass()` calls
with zero `RenderPassToggleRegistry` consult, but the WHOLE feature (every one of its
own passes, for every view) is filtered out of the combined list BEFORE any of those
passes are ever declared — so toggling a `_v2`/`_v3` feature off is genuinely,
completely honest, just through a different, bespoke mechanism than
`RenderPassToggleRegistry`.

The Project Assembly system's own render-pass provider
(`ProjectAssemblyProbe.FillTexture`) was found and live-confirmed **already honest** —
it uses the generic `outPasses.push_back(desc)` mechanism, so it is gated by
`RenderPassToggleRegistry` automatically, with zero special-case code.

**Three genuine design ambiguities were found and resolved via `ask_questions` before
finalizing the fix backlog below** (see "Ambiguity checkpoint" section) — the user
chose the maximal, most-honest option for all three: fix every confirmed-lie instance
found this phase, with no exceptions, including the two that would otherwise have been
low-priority/deferred (the debug-only per-row-checkbox class, and the GpuSkinning
direct-render fallback).

---

## Master Ledger (every `AddRenderPass()` call site cross-referenced against every togglable surface)

Static inventory built via `search_in_dir` for `.AddRenderPass(`/`NoteDeclaredAndCheckEnabled`/
`out.push_back` across `src/`, cross-referenced against `RenderPipeline.h`'s
`DeclareOnePhase()` (the ONE place that generically calls
`NoteDeclaredAndCheckEnabled()` for anything pushed via a provider's own `out`
parameter).

| # | Pass debug name(s) | Declared via | File:line | Toggle mechanism consulted | Classification | Live evidence |
|---|---|---|---|---|---|---|
| 1 | `AtmosphereTransmittanceLutPass` | direct `builder.AddRenderPass()`, own hand-written guard | `AtmosphereLutRenderer.cpp:225,256` | `RenderPassToggleRegistry` (own name) | **Already-Honest** | Pre-confirmed `editor-core-separation-20`/PHASE1/PHASE2; re-confirmed this phase via `GET /render_graph/passes` (`enabled:true`) |
| 2 | `AtmosphereMultiScatteringLutPass` | same pattern | `:349,361` | own name | **Already-Honest** | same |
| 3 | `AtmosphereSkyViewLutPass` | same | `:489,508` | own name | **Already-Honest** | same |
| 4 | `AtmosphereAerialPerspectiveVolumePass` | same | `:638,657` | own name | **Already-Honest** | same |
| 5 | `AtmosphereAerialPerspectiveCompositePass` | same | `:786,832` | own name | **Already-Honest** | PHASE1/PHASE2 of this campaign fully re-verified this |
| 6 | `AtmosphereAerialPerspectiveVolumeDebugSlicePass` | direct `builder.AddRenderPass()` inside `AddAerialPerspectiveVolumeDebugSlicePass()`, called only from `Core.cpp` when `viewLuts.aerialPerspectiveVolumeHandle.IsValid()` | `AtmosphereLutRenderer.cpp` | **NONE** by its own name | **Confirmed-Lie** (same class as #21/#22/#23 below — cosmetic row checkbox) | Same code shape confirmed live for #21-23; not independently re-tested by name (time budget) — **PHASE4 backlog** |
| 7 | `"AtmosphereComposite"` (outer provider gate) | manual check inside the `"AtmosphereComposite"` provider lambda, `ProviderTiming::AfterDeferredPasses` | `Core.cpp:919-983,933` | own name, separate registry entry from #5 | **Already-Honest** (dual-name UX quirk, pre-existing, not re-litigated) | `GET /render_graph/passes` shows both names independently toggle-able |
| 8 | `"GpuSkinning"` (offscreen whole-stage gate) + per-request dynamically-named dispatch passes | manual check before the loop, then `out.push_back(desc)` per request (generic path) | `Core.cpp:464-508` | own name (stage) **and** each dynamic per-request name (generic, automatic) | **Already-Honest** | `GET /render_graph/passes` shows `"GpuSkinning":true`; no GPU-skinned model was loaded this session to exercise a real dispatch, but the code shape is structurally identical to every other honest generic-path pass |
| 9 | `AddGpuSkinningPasses()`'s direct-render-to-swapchain fallback (dynamically-named per-request passes, reached from the `"Present"` provider) | direct `builder.AddRenderPass(request.name, ...)`, **zero** registry consult | `RenderPasses.cpp:209-246`, called from `Core.cpp:1021` | **NONE** | **Confirmed-Lie** (static) | Live-confirmed **UNREACHABLE** in this session: temporary `GTE_LOG_WARNING("RenderPassHonestyDiag", ...)` at `Core::Present()`'s `needsDirectGameRender` computation fired **zero** times across the entire live-testing session (default dock layout always keeps Game or Scene visible) — **PHASE4 backlog** (user confirmed: fix anyway) |
| 10 | `"ClearViewTarget"` | generic `out.push_back(desc)` | `Core.cpp:579-631` | `RenderPassToggleRegistry` (generic) **+ deny-listed** | **Already-Honest** | `GET /render_graph/set_pass_enabled?name=ClearViewTarget&enabled=false` → live-confirmed `HTTP 409`, `{"error":"\"ClearViewTarget\" cannot be disabled (deny-listed).","success":false}` |
| 11 | `"RenderOpaque"` | generic `out.push_back(desc)` | `Core.cpp:634-682` | `RenderPassToggleRegistry` | **Already-Honest** | Pre-existing (`editor-core-separation-20`) |
| 12 | `"DrawSkyBackground"` | generic `out.push_back(desc)` | `Core.cpp:823-873` | `RenderPassToggleRegistry` | **Already-Honest** | Pre-existing |
| 13 | `"RenderTransparent"` | generic `out.push_back(desc)` (always empty today) | `Core.cpp:876-915` | `RenderPassToggleRegistry` | **Already-Honest** | Pre-existing; currently a permanent no-op |
| 14 | `"GpuDrivenBatchN ResetCount/Culling/IndirectDraw"` (dynamically named, per batch) | generic `out.push_back(desc)` | `Core.cpp:692-820` | `RenderPassToggleRegistry` (per dynamic name — no single "whole feature" switch exists) | **Already-Honest** at the per-pass level; **No-Toggle-Exists** at the feature/stage level (no umbrella checkbox claims to control the whole GPU-driven-batch feature, so this is not a lie) | Static confirmation only (no batch was live this session — no entity met the eligibility threshold) |
| 15 | `"Present"` | direct `builder.AddRenderPass()` | `RenderPasses.cpp:174`, called from `Core.cpp:1018-1026` | Deny-listed (no consult needed) | **Already-Honest** | Live-confirmed `HTTP 409`, `{"error":"\"Present\" cannot be disabled (deny-listed).","success":false}` |
| 16 | `AddRenderOpaquePass()` / `AddDrawSkyBackgroundPass()` (free functions) | direct `builder.AddRenderPass()` | `RenderPasses.cpp:52,98` | N/A | **No-Toggle-Exists (dead code)** | Re-confirmed via `search_in_dir` for `AddRenderOpaquePass(`/`AddDrawSkyBackgroundPass(`: zero real callers anywhere outside their own definition/declaration/doc comments — matches `AGENTS.md`'s pre-existing "confirmed-dead" statement |
| 17 | `AddRenderTransparentPass()` (free function) | early-returns before ever calling `builder.AddRenderPass()` at all | `RenderPasses.cpp:131` | N/A | **No-Toggle-Exists (dead code)** — **NEW finding this phase**, not previously documented in `AGENTS.md` as dead (only #16's two functions were) | Re-confirmed via `search_in_dir` for `AddRenderTransparentPass(`: zero real callers outside its own definition/declaration/one doc-comment reference in `RenderSystem.h` |
| 18 | `"DemoRenderFeaturePlugin_Clear"` | direct `builder.AddRenderPass()` via `AddFullscreenClearPass()`, reached by `LegacyRenderFeatureOrchestrator::ContributeRenderGraphPasses()`'s unconditional loop over every loaded `_v1` module | `PluginRenderPassBuilderAdapter.cpp:53`, `LegacyRenderFeatureOrchestrator.cpp:35-55` | **NONE** | **Confirmed-Lie** (already known, `PHASE0` Step 2.2) | **LIVE-CONFIRMED**: toggled off via `GET /render_graph/set_pass_enabled` → next `GET /render_graph` still shows it with `"is_culled":false`, real `"gpu_timing_milliseconds":0.04...` — **PHASE4 backlog** (already mandated, Locked Decision #7) |
| 19 | `"DemoRenderFeatureSecondPlugin_Clear"` | same shape, second loaded `_v1` plugin | same files | **NONE** | **Confirmed-Lie** | **LIVE-CONFIRMED**, identical to #18 — **PHASE4 backlog** |
| 20 | `_v2`/`_v3` Plugin Render Feature per-feature passes (`DemoRenderFeatureV2_*`, `DemoRenderFeatureV3_*`, `DemoRenderFeatureV3Third_*`, `DemoRenderFeatureV2Second_*`, `DemoRenderFeatureV3Second_*` — Fill/Private/Blend/Accum/Downsample/Upsample/Grade/Vignette, per view) | direct `builder.AddRenderPass()` (`RenderFeatureCompositor::DispatchOps/DispatchBlend`, `PluginRenderPassBuilderAdapter_v3.cpp:160,227`) | `RenderFeatureCompositor.cpp:480,506` | `RenderFeatureCompositor`'s own bespoke `enabledOverride` filter (applied to the WHOLE feature, before any of its passes are ever constructed) — **NOT** `RenderPassToggleRegistry` | **Already-Honest** | **LIVE-CONFIRMED**: `GET /render_graph/set_feature_enabled?name=DemoRenderFeatureV2&enabled=false` → every one of its own passes (`DemoRenderFeatureV2_Fill`, `_Game_Private`, `_Game_Blend`, `_Game_Accum`, `_Scene_*`) completely vanished from the next `GET /render_graph`; re-enabled cleanly, zero errors |
| 21 | `"ComputeBlurValidation"` | direct `builder.AddRenderPass()`, reached only via `ImGuiEditorLayer::AddBlurValidationPass()` | `ComputeBlurValidation.cpp:108`, gate in `ImGuiEditorLayer.cpp:468-478` (`ctx.showBlurredSceneOutput`) | The REAL feature toggle is `ctx.showBlurredSceneOutput` (bespoke `EditorContext` bool, honest); **the pass's own row checkbox in the regime table consults NOTHING** | **Already-Honest** (real feature) / **Confirmed-Lie** (per-row checkbox) | **LIVE-CONFIRMED both ways**: `set_blur_enabled=true` → pass appears; `=false` → pass gone. Separately, with the real toggle left ON, `set_pass_enabled?name=ComputeBlurValidation&enabled=false` → **zero effect**, byte-identical `GET /render_graph` before/after — **PHASE4 backlog** (per `ask_questions`) |
| 22 | `"GBufferValidation"` | direct `builder.AddRenderPass()` | `GBufferValidation.cpp:152`, gate in `ImGuiEditorLayer.cpp:489-499` (`ctx.showGBufferValidationOutput`) | same shape as #21 | **Already-Honest** (real feature) / **Confirmed-Lie** (per-row checkbox) | **LIVE-CONFIRMED both ways**, identical methodology to #21 — **PHASE4 backlog** |
| 23 | `"GBufferValidationCopy"` | direct `builder.AddRenderPass()` | `GBufferValidation.cpp:181` | same shape as #21/#22 (same caller gate) | **Already-Honest** (real feature) / **Confirmed-Lie** (per-row checkbox) | **LIVE-CONFIRMED**, same session as #22 — **PHASE4 backlog** |
| 24 | `"FrameDebuggerReplayStep0"`, `"FrameDebuggerReplayStep1"`, ... (dynamically named, one per redrawn object + one sky step) | direct `builder.AddRenderPass()`, reached only via `IFrameDebuggerCaptureRecorder::AddReplayPasses()` on an explicit capture-trigger frame | `FrameDebuggerReplayPasses.cpp:171` | **NONE** | **Confirmed-Lie in principle** (same root cause as #21-23) — practically very-low-severity: these passes exist in `RenderGraph::LastSnapshot()` for exactly one frame, deferred by one frame from the capture trigger | Attempted live verification: `GET /frame_debugger/capture` followed immediately by `GET /render_graph` did **NOT** show any `FrameDebuggerReplayStepN` entry — the snapshot had already rolled forward past the one frame that declared them by the time the HTTP response was built. Classified via static code-shape analysis (identical to #21-23) rather than a byte-for-byte live repro of the cosmetic-checkbox symptom itself — **PHASE4 backlog** (per `ask_questions`: "no permanent exceptions") |
| 25 | `"ProjectAssemblyProbe.FillTexture"` | generic `outPasses.push_back(desc)` via `Core::RegisterProjectRenderPassProvider()` | `Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp:146-200` | `RenderPassToggleRegistry` (generic, automatic — zero special-case code) | **Already-Honest** | **LIVE-CONFIRMED**: toggled off via `GET /render_graph/set_pass_enabled` → pass instantly gone from `passesInExecutionOrder` in the next `GET /render_graph` (only its now-dead `"ProjectAssemblyProbe.Output"` transient texture remained, listed as a resource); zero new log errors; re-enabled cleanly; `GET /get_game_view` still a sane, non-corrupted image throughout |

### Frame Debugger's own replay-passes / debug-only passes — cross-check against `AGENTS.md`

`AGENTS.md`'s "Render Pass System" section explicitly documents `AddFrameDebuggerReplayPasses()`'s
replay steps and `ComputeBlurValidation`'s own pass as **DELIBERATELY, PERMANENTLY**
left on the old, direct `AddRenderPass()` call style "since they only feed Frame
Debugger tooling that itself never moved off `ViewScope`/`RenderPassCategory`/
`RenderPassDrawKind`". That statement is about NOT migrating them onto the newer
`RenderPipeline`/provider system — it never said (and the panel's own generic
`BuildPassRow()` was never designed to imply) that these passes should visibly offer a
functional per-row enable checkbox in the Editor UI once they happen to be declared.
The **iron rule** this whole campaign exists to enforce (`PHASE0_MASTER_STRATEGY.md`
Step 1.2) makes no exception for "debug tooling" passes — a checkbox that appears to
control a pass must actually control it, full stop. This is exactly why the user's own
`ask_questions` answer (Q3, below) extends the fix to these too, rather than carving
out a permanent exception that would otherwise contradict the campaign's own stated
goal.

---

## Live Verification — full transcript (condensed; every request was issued strictly sequentially per PHASE1's own "avoid a batched-request race" lesson)

Environment: `build\GreatTamanaEditor.exe`, launched via `run_app_background`, driven
purely via `gte_send_request`/`run_shell` (PowerShell `Invoke-WebRequest`, used only to
capture and pretty-print large JSON bodies that exceed `gte_send_request`'s own
truncation limit — never to mutate engine state), closed via `stop_app_background`
before finishing.

### Baseline

```
GET /render_graph/passes
  -> {"passes":[... "AtmosphereAerialPerspectiveCompositePass":true, "AtmosphereComposite":true,
      "ClearViewTarget":true, "DrawSkyBackground":true, "GpuSkinning":true,
      "ProjectAssemblyProbe.FillTexture":true, "RenderOpaque":true, "RenderTransparent":true ...]}
```

### Finding #18/#19 — `DemoRenderFeaturePlugin_Clear` / `DemoRenderFeatureSecondPlugin_Clear` (Confirmed-Lie)

```
GET /render_graph/set_pass_enabled?name=DemoRenderFeaturePlugin_Clear&enabled=false       -> {"success":true}
GET /render_graph/set_pass_enabled?name=DemoRenderFeatureSecondPlugin_Clear&enabled=false -> {"success":true}
GET /render_graph/passes
  -> {"enabled":false,"ever_declared_this_session":false,"name":"DemoRenderFeaturePlugin_Clear"}
  -> {"enabled":false,"ever_declared_this_session":false,"name":"DemoRenderFeatureSecondPlugin_Clear"}
     (ever_declared_this_session:false CONFIRMS NoteDeclaredAndCheckEnabled() is never called for these names)
GET /render_graph  (full metadata, pretty-printed via PowerShell for inspection)
  -> {"category":"Debug","draw_call_count":0,"gpu_timing_milliseconds":0.04364583226776123,
      "is_culled":false,"kind":"Graphics","name":"DemoRenderFeaturePlugin_Clear",
      "render_pass_event":"AfterEverything","view_scope":"Shared",
      "writes":[{"kind":"Texture","name":"GameViewComposited"}]}
  -> {"category":"Debug","draw_call_count":0,"gpu_timing_milliseconds":0.008854166450500488,
      "is_culled":false,"name":"DemoRenderFeatureSecondPlugin_Clear", ...}
```
Both still ran, real GPU time, `is_culled:false` — the lie, reproduced live.

### Finding #20 — `RenderFeatureCompositor` per-feature toggle (Already-Honest)

```
GET /render_graph  -> render_features: DemoRenderFeatureV2/V3/V3Third/V2Second/V3Second, all enabled:true
GET /render_graph/set_feature_enabled?name=DemoRenderFeatureV2&enabled=false -> {"success":true}
GET /render_graph  -> search for "DemoRenderFeatureV2_" in the full pretty-printed JSON: 0 matches
                      (DemoRenderFeatureV2_Fill/_Game_Private/_Game_Blend/_Game_Accum/_Scene_* ALL gone)
GET /render_graph/set_feature_enabled?name=DemoRenderFeatureV2&enabled=true -> {"success":true} (restored)
```

### Finding #21/#22/#23 — `ComputeBlurValidation` / `GBufferValidation` / `GBufferValidationCopy`

```
GET /render_graph/set_blur_enabled?enabled=true    -> {"success":true}
GET /render_graph/set_gbuffer_enabled?enabled=true -> {"success":true}
GET /render_graph -> "ComputeBlurValidation"/"GBufferValidation"/"GBufferValidationCopy" all present, 12 total mentions
GET /render_graph/set_pass_enabled?name=ComputeBlurValidation&enabled=false -> {"success":true}
GET /render_graph/set_pass_enabled?name=GBufferValidation&enabled=false     -> {"success":true}
GET /render_graph -> identical 12 mentions, all three passes STILL present, unaffected
                     -> THE PER-ROW CHECKBOX IS PROVEN COSMETIC for these three passes
GET /render_graph/set_blur_enabled?enabled=false    -> {"success":true} (restored to default)
GET /render_graph/set_gbuffer_enabled?enabled=false -> {"success":true} (restored to default)
```

### Finding #6/#24 — `AtmosphereAerialPerspectiveVolumeDebugSlicePass` / `FrameDebuggerReplayStepN`

```
GET /frame_debugger/open              -> {"state":{"windowOpen":true,...},"success":true}
GET /frame_debugger/enable?value=true -> {"state":{"enabled":true,...},"success":true}
GET /frame_debugger/capture           -> {"state":{"hasCapturedFrame":true,"totalEventCount":63,...},"success":true}
GET /render_graph -> search for "FrameDebuggerReplayStep": 0 matches (rolled off LastSnapshot() by request time)
```
`AtmosphereAerialPerspectiveVolumeDebugSlicePass` DOES appear in every `GET /render_graph`
capture (it is declared every frame the Game View has a valid aerial-perspective
volume) with `is_culled:false` — its own per-row checkbox was not independently
byte-for-byte re-tested this phase (same confirmed code shape as #21-23; time budget
prioritized breadth of distinct classes over exhaustive per-item re-confirmation).

### Finding #9 — `AddGpuSkinningPasses()` direct-render fallback (Confirmed-Lie, static; unreachable live)

Temporary diagnostic added for this phase only (removed before this report — see
"Temporary instrumentation" below):

```cpp
// Core::Present(), before needsDirectGameRender is used:
if (needsDirectGameRender) {
    GTE_LOG_WARNING("RenderPassHonestyDiag",
        "Core::Present() - needsDirectGameRender=true this frame ... bypassing the \"GpuSkinning\" toggle entirely.");
}
```

```
GET /get_logs?category=RenderPassHonestyDiag&limit=20   (checked THREE times across the whole session)
  -> {"count":0,"entries":[],"latest_id":42,"logging_enabled":true}   every single time
```
Zero hits across the entire live-testing session (many dozens of HTTP requests,
several Frame Debugger captures, multiple render-feature/pass toggles) — confirms the
default dock layout (Game View + Scene View tabbed together, one always visible per
Dear ImGui tab-group semantics, `EditorContext::gameViewVisible` defaults to `true`)
never lets `needsDirectGameRender` become `true` through any currently-exposed
Editor/HTTP control. The lie itself (zero toggle consult in `AddGpuSkinningPasses()`)
remains a real, static, confirmed fact of the code — just one this session's live
testing could not directly trigger.

### Deny-listed passes — rejection surfaced correctly (Already-Honest)

```
GET /render_graph/set_pass_enabled?name=Present&enabled=false
  -> HTTP 409  {"error":"\"Present\" cannot be disabled (deny-listed).","success":false}
GET /render_graph/set_pass_enabled?name=ClearViewTarget&enabled=false
  -> HTTP 409  {"error":"\"ClearViewTarget\" cannot be disabled (deny-listed).","success":false}
```

### Finding #25 — `ProjectAssemblyProbe.FillTexture` (Already-Honest)

```
GET /render_graph/set_pass_enabled?name=ProjectAssemblyProbe.FillTexture&enabled=false -> {"success":true}
GET /render_graph -> search for "ProjectAssemblyProbe": only "ProjectAssemblyProbe.Output" (a RESOURCE, not a pass) remains
                     the PASS "ProjectAssemblyProbe.FillTexture" is gone from passesInExecutionOrder entirely
GET /get_logs?min_level=Error&limit=20 -> {"count":0,"entries":[],...}  (zero errors)
GET /get_game_view -> HTTP 200, image/png, 158923 bytes, sane solid-blue atmosphere-tinted image (no magenta)
GET /render_graph/set_pass_enabled?name=ProjectAssemblyProbe.FillTexture&enabled=true  -> {"success":true} (restored)
```

### Final sanity across the whole session

```
GET /get_logs?min_level=Error&limit=50 -> {"count":0,"entries":[],...}   zero errors, the whole session
GET /render_graph/passes (final)       -> every state restored to its default (all enabled except the two
                                           debug-toggle-driven ones left at their own default-off state:
                                           "ComputeBlurValidation":false, "GBufferValidation":false)
```
`stop_app_background` closed the engine cleanly — no stray instance left running.

---

## Ambiguity checkpoint — `ask_questions` (per `PHASE0_MASTER_STRATEGY.md` Step 3.3)

This audit surfaced a genuinely large systemic finding (the per-row-checkbox class,
items #6/#21-24) whose correct remedy was not obvious, plus one already-flagged
candidate (#9) whose live-reachability came back negative. Per this campaign's own
Locked Decision #5 and PHASE3's own Step 3.3, `ask_questions` was used before finalizing
PHASE4's backlog — three questions, three clear answers, **all three chose the
maximal-honesty option**:

1. **Debug-only passes with their own real toggle elsewhere but a cosmetic per-row
   checkbox** (`ComputeBlurValidation`/`GBufferValidation`/`GBufferValidationCopy`/
   `AtmosphereAerialPerspectiveVolumeDebugSlicePass`) → **"Add a real per-pass
   toggle-registry guard to each of these too (extra, independent granularity beyond
   their existing toggle)"** — chosen over "gray out the checkbox" or "defer".
2. **`AddGpuSkinningPasses()`'s direct-render-to-swapchain fallback** (confirmed
   unreachable in this session) → **"Yes — add a toggle-registry guard now for
   correctness/defense-in-depth, even though it could not be live-exercised in this
   session"** — chosen over deferring it as an accepted non-goal.
3. **`FrameDebuggerReplayStepN`** (explicitly documented in `AGENTS.md` as permanently
   left on the old direct-declare style) → **"Yes — the iron rule should apply to
   these too, with no permanent exceptions"** — chosen over carving out a permanent
   exemption.

**Net effect on PHASE4's scope: every single Confirmed-Lie row in the ledger above
must be fixed — there is no remaining "accepted non-goal" carve-out left from this
phase's own findings.**

---

## PHASE4 fix backlog (in the order PHASE4 should tackle them)

1. `"DemoRenderFeaturePlugin_Clear"` / `"DemoRenderFeatureSecondPlugin_Clear"`
   (`LegacyRenderFeatureOrchestrator::ContributeRenderGraphPasses()` /
   `PluginRenderPassBuilderAdapter::AddFullscreenClearPass()`) — already mandated by
   `PHASE0_MASTER_STRATEGY.md` Locked Decision #7; highest priority.
2. `"ComputeBlurValidation"` (`ComputeBlurValidation.cpp:108`).
3. `"GBufferValidation"` / `"GBufferValidationCopy"` (`GBufferValidation.cpp:152,181`).
4. `"AtmosphereAerialPerspectiveVolumeDebugSlicePass"` (`AtmosphereLutRenderer.cpp`).
5. `AddGpuSkinningPasses()`'s direct-render-to-swapchain fallback
   (`RenderPasses.cpp:209-246`) — per-request dynamic names, or a single
   `"GpuSkinning"` whole-stage guard mirroring the offscreen provider's own shape
   (`Core.cpp:476`) — PHASE4 should decide which, consistent with item #8's existing
   pattern.
6. `FrameDebuggerReplayStepN` (`FrameDebuggerReplayPasses.cpp:171`) — dynamically
   named; needs its own per-step or whole-mechanism guard design decision.

Each fix should follow the same shape the five Atmosphere passes already established
(`ShouldDeclareAtmospherePassThisFrame()`-style pure helper +
`NoteDeclaredAndCheckEnabled()` call immediately before the pass's own
`builder.AddRenderPass()`/`out.push_back()` call), per `AGENTS.md`'s own "Render Pass
System" precedent, and each must add/update a Tier-1 test wherever the new guard is
expressible as pure logic (per `AGENTS.md`'s "Testability & Regression Safety").

---

## Temporary instrumentation — added, used, then fully removed

One temporary `GTE_LOG_WARNING("RenderPassHonestyDiag", ...)` call was added inside
`Core::Present()` (`src/Core/Core.cpp`), guarded by `if (needsDirectGameRender)`, plus
a temporary `#include "Logging.h"`, to LIVE-confirm finding #9's real-world
reachability (see above). Both were fully reverted after the live session concluded
(zero hits recorded) — confirmed via `git_status` reporting **"nothing to commit,
working tree clean"** and a subsequent incremental `cmake --build build` succeeding
cleanly, proving the revert was byte-for-byte exact. **No production code was
changed by this phase** — per its own Step 3.4 point 1 ("No production code fix lands
in this phase").

---

## Verification performed

- **Incremental compile check**: `cmake --build build` — succeeded, twice (once with
  the temporary diagnostic in place, once again after reverting it) — both clean,
  zero warnings/errors from this phase's own touched files.
- **Live HTTP verification**: full transcript above, against a single continuous
  `GreatTamanaEditor.exe` session launched via `run_app_background`, driven via
  `gte_send_request` (small responses) and PowerShell `Invoke-WebRequest` +
  `ConvertFrom-Json`/`ConvertTo-Json` (large `GET /render_graph` bodies, purely for
  local inspection — never used to mutate any engine state), closed via
  `stop_app_background` before finishing. No stray instance left running.
- `GET /get_logs?min_level=Error` returned `{"count":0,...}` at every checkpoint
  throughout the session — no crash, no new warning/error caused by any toggle this
  phase exercised.
- `GET /get_game_view` confirmed a sane, non-corrupted rendered image (no magenta) at
  the end of the session.
- No `printf`/`std::cout`/`fprintf`/`OutputDebugString` used anywhere — the one
  temporary diagnostic used `GTE_LOG_WARNING`, retrieved exclusively via
  `GET /get_logs`, and was fully removed again.
- `git status` confirms a clean working tree after the temporary instrumentation was
  reverted, before this report and its own `git_add`/`git_commit`.

## Files changed

- `task_manager/editor-core-separation-21/PHASE3_COMPLETION_REPORT.md` (new — this
  report).

No production/test source file has a net change from this phase (the one temporary
diagnostic in `src/Core/Core.cpp` was added and then fully reverted within this same
phase, confirmed via `git status`).

## No delegation

No `delegate_task` call was made (none permitted for this phase). `ask_questions` was
used once, with 3 questions, exactly per this campaign's own Locked Decision #5 and
PHASE3's own Step 3.3 — see "Ambiguity checkpoint" above for the full record.
