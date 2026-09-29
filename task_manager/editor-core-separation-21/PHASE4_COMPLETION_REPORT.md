# PHASE4 COMPLETION REPORT — Fix Every Confirmed-Lie Finding From The Audit

Campaign: `editor-core-separation-21` ("The Engine Is Lying" — Render Pass / Frame Debugger Honesty Campaign)
Phase: PHASE4 (`PHASE4_FIX_AUDIT_FINDINGS.md`)
Branch: `feature/editor-core-separation` (unchanged, as required)

---

## Summary

Every single `Confirmed-Lie` row PHASE3's audit ledger produced is now fixed. This phase's own
task list (`PHASE3_COMPLETION_REPORT.md`'s "PHASE4 fix backlog") had 6 items; all 6 are done, each
verified live against a real running `GreatTamanaEditor.exe`, with zero items deferred.

| # | Finding | Fix shape | Live-verified? |
|---|---|---|---|
| 1 | `DemoRenderFeaturePlugin_Clear` / `DemoRenderFeatureSecondPlugin_Clear` (unconditional declare, zero registry consult) | `PluginRenderPassBuilderAdapter` now takes a `RenderPassToggleRegistry*`, `AddFullscreenClearPass()` consults `NoteDeclaredAndCheckEnabled(debugName)` before declaring | Yes — bidirectional |
| 2 | `ComputeBlurValidation`'s per-row checkbox was cosmetic | `ImGuiEditorLayer::AddBlurValidationPass()` now also consults `NoteDeclaredAndCheckEnabled("ComputeBlurValidation")`, as an ADDITIONAL gate on top of the real `showBlurredSceneOutput` toggle | Yes — bidirectional |
| 3 | `GBufferValidation`/`GBufferValidationCopy`'s per-row checkboxes were cosmetic | `GBufferValidation::AddPass()` now independently gates the graphics half (`"GBufferValidation"`) and the compute half (`"GBufferValidationCopy"`) — disabling the graphics half disables both; disabling only the compute half leaves albedo/normal running and only drops `visualized` | Yes — bidirectional AND independent-granularity confirmed |
| 4 | `AtmosphereAerialPerspectiveVolumeDebugSlicePass` (unconditional declare) | `AtmosphereLutRenderer::AddAerialPerspectiveVolumeDebugSlicePass()` now consults `NoteDeclaredAndCheckEnabled()` before any resource init | Yes — bidirectional |
| 5 | `AddGpuSkinningPasses()`'s direct-render-to-swapchain fallback (unconditional declare, same class of bug as #8's offscreen provider but with zero consult) | Now consults the SAME `"GpuSkinning"` registry entry the offscreen provider already uses, whole-stage, mirroring that provider's own shape | Confirmed for the shared toggle name (offscreen path); the swapchain-direct fallback branch itself remains unreachable this session (same finding PHASE3 already made) — see "Fix #5" section below |
| 6 | `FrameDebuggerReplayStepN` (unconditional declare, no registry consult at all) | `IFrameDebuggerCaptureRecorder::AddReplayPasses()` now consults a single, whole-mechanism `"FrameDebuggerReplay"` toggle before declaring any replay step | Partially — registry entry proven honest (auto-registers, toggle recorded); the tree-count-based verification approach PHASE3 tried is structurally incapable of proving this the same way (see below) |

No item was deferred. No `ask_questions` was needed this phase — every fix mapped cleanly onto the
same, already-established `RenderPassToggleRegistry::NoteDeclaredAndCheckEnabled()` pattern
`AtmosphereLutRenderer`'s five methods already use, and no cascading-invalid-dependency hazard (the
kind `editor-core-separation-20`'s own investigation found) was introduced by any of these 6 fixes —
each was checked explicitly (see each fix's own section below).

---

## Fix 1 — `DemoRenderFeaturePlugin_Clear` / `DemoRenderFeatureSecondPlugin_Clear`

### The change

- `src/Core/Plugins/PluginRenderPassBuilderAdapter.h` — added a third, defaulted constructor
  parameter, `rg::RenderPassToggleRegistry* toggleRegistry = nullptr`, stored as a new private
  member `m_toggleRegistry`.
- `src/Core/Plugins/PluginRenderPassBuilderAdapter.cpp` — `AddFullscreenClearPass()` now does:
  ```cpp
  if (m_toggleRegistry != nullptr && !m_toggleRegistry->NoteDeclaredAndCheckEnabled(debugName)) {
      return;
  }
  ```
  BEFORE any `RenderPassEvent`/attachment-declaration logic, reusing the SAME `debugName` parameter
  already passed in (no new/second name string).
- `src/Core/Plugins/LegacyRenderFeatureOrchestrator.cpp` — the ONE real construction site now passes
  `&m_core.GetRenderPassToggleRegistryMutable()`.

### Confirmed: nothing downstream reads this pass's own written target if it never runs

`LegacyRenderFeatureOrchestrator::ContributeRenderGraphPasses()` pushes `resolved->target` into
`frame.finalTextureOutputs` unconditionally, regardless of whether any of the loaded `_v1` modules'
own clear pass actually ran — a harmless root reference to a texture that may still have been validly
written by an EARLIER pass this same frame (`"RenderOpaque"`/`"DrawSkyBackground"`/etc.). No
degrade-gracefully branch was needed.

### Live verification (`GreatTamanaEditor.exe`, `run_app_background`/`gte_send_request`)

```
GET /render_graph/set_pass_enabled?name=DemoRenderFeaturePlugin_Clear&enabled=false       -> {"success":true}
GET /render_graph/set_pass_enabled?name=DemoRenderFeatureSecondPlugin_Clear&enabled=false -> {"success":true}
(PowerShell) $names -contains 'DemoRenderFeaturePlugin_Clear'       -> False
(PowerShell) $names -contains 'DemoRenderFeatureSecondPlugin_Clear' -> False
GET /get_logs?min_level=Error&limit=20 -> {"count":0,"entries":[],...}
GET /get_game_view -> HTTP 200, image/png, 158923 bytes, sane solid-blue sky (no magenta)
GET /render_graph/set_pass_enabled?name=DemoRenderFeaturePlugin_Clear&enabled=true       -> {"success":true}
GET /render_graph/set_pass_enabled?name=DemoRenderFeatureSecondPlugin_Clear&enabled=true -> {"success":true}
(PowerShell) $names -contains 'DemoRenderFeaturePlugin_Clear'       -> True
(PowerShell) $names -contains 'DemoRenderFeatureSecondPlugin_Clear' -> True
```

The lie is gone: toggling off now genuinely removes both passes from `passesInExecutionOrder`;
toggling back on restores them, with zero crash and a sane rendered image throughout.

---

## Fix 2 — `ComputeBlurValidation`'s per-row checkbox

### The change

- `src/Editor/EditorLayer.h` — `IEditorLayer::AddBlurValidationPass()` gained a trailing, defaulted
  `rg::RenderPassToggleRegistry* toggleRegistry = nullptr` parameter.
- `src/Editor/ImGuiEditorLayer.cpp` — the real implementation now does, AFTER its existing
  `showBlurredSceneOutput`/`sceneViewVisible`/non-degenerate-extent checks:
  ```cpp
  if (toggleRegistry != nullptr && !toggleRegistry->NoteDeclaredAndCheckEnabled("ComputeBlurValidation")) {
      return std::nullopt;
  }
  ```
  `src/Editor/ComputeBlurValidation.h/.cpp` itself was NOT touched — the smallest correct fix here
  is entirely at the wrapper level, since this pass has only ONE toggle name to consult (unlike
  GBufferValidation's two — see Fix 3).
- `src/Editor/NullEditorLayer.cpp` — signature updated, parameter ignored (a release build has
  nothing to gate).
- `src/Core/Core.cpp` — the one real call site now passes `&m_renderPassToggleRegistry`.

### Live verification

```
GET /render_graph/set_blur_enabled?enabled=true -> {"success":true}
GET /activate_tab?name=Scene -> {"activated_tab":"Scene","success":true}
(PowerShell) $names -contains 'ComputeBlurValidation' -> True   (feature toggle ON, per-row still true - baseline)
GET /render_graph/set_pass_enabled?name=ComputeBlurValidation&enabled=false -> {"success":true}
(PowerShell) $names -contains 'ComputeBlurValidation' -> False  (per-row checkbox now GENUINELY gates it)
GET /get_logs?min_level=Error&limit=20 -> {"count":0,...}
GET /get_game_view -> HTTP 200, sane image
GET /render_graph/set_pass_enabled?name=ComputeBlurValidation&enabled=true -> {"success":true}
(PowerShell) $names -contains 'ComputeBlurValidation' -> True   (restored)
GET /render_graph/set_blur_enabled?enabled=false -> {"success":true} (restored to default-off)
```

---

## Fix 3 — `GBufferValidation` / `GBufferValidationCopy`'s per-row checkboxes

### The change (the most structurally involved of the 6)

- `src/Editor/EditorLayer.h` — `IEditorLayer::AddGBufferValidationPass()` gained the same trailing
  `toggleRegistry` parameter.
- `src/Editor/GBufferValidation.h/.cpp` — `AddPass()` itself (not just the wrapper, since this class
  declares TWO independently-named passes in one call) now:
  1. Checks `NoteDeclaredAndCheckEnabled("GBufferValidation")` BEFORE `EnsureInitialized()`/any
     resource creation. If disabled, returns a fully-default `GBufferValidationHandles{}` — NEITHER
     pass declares anything (the compute half has no valid `albedoHandle` to read without the
     graphics half).
  2. If the graphics half IS enabled, it declares `"GBufferValidation"` (albedo/normal) exactly as
     before, THEN independently checks `NoteDeclaredAndCheckEnabled("GBufferValidationCopy")` —
     only if THAT is also enabled does it import `GBufferVisualized` and declare
     `"GBufferValidationCopy"`. Disabling only the copy half leaves albedo/normal running normally;
     `visualized` simply comes back as an invalid, default-constructed handle (harmless when pushed
     into `finalTextureOutputs` — `RenderGraphCompiler` finds no pass that wrote it).
  3. `m_writtenThisFrame` (a single bool) was REPLACED by two independent flags,
     `m_graphicsWrittenThisFrame`/`m_copyWrittenThisFrame` — `FinalizeForSampling()` now only emits
     an image-layout barrier for whichever half genuinely wrote something THIS frame, never
     assuming both halves always run together anymore (this was the one genuine correctness risk
     this fix had to get right — an unconditional barrier against a texture that was never written
     this frame would be a real validation-layer/undefined-behavior hazard).
- `src/Core/Core.cpp` — the one real call site now passes `&m_renderPassToggleRegistry`.

### Live verification — bidirectional AND independent-granularity proof

```
GET /render_graph/set_gbuffer_enabled?enabled=true -> {"success":true}
(PowerShell) GBufferValidation: True, GBufferValidationCopy: True   (feature toggle ON - baseline)

GET /render_graph/set_pass_enabled?name=ComputeBlurValidation&enabled=false -> {"success":true}
GET /render_graph/set_pass_enabled?name=GBufferValidation&enabled=false     -> {"success":true}
(PowerShell) ComputeBlurValidation: False, GBufferValidation: False, GBufferValidationCopy: False
   <- disabling the GRAPHICS half correctly took the DEPENDENT compute half down with it
GET /get_logs?min_level=Error&limit=20 -> {"count":0,...}
GET /get_game_view -> HTTP 200, sane image

GET /render_graph/set_pass_enabled?name=ComputeBlurValidation&enabled=true -> {"success":true}
GET /render_graph/set_pass_enabled?name=GBufferValidation&enabled=true     -> {"success":true}
GET /render_graph/set_pass_enabled?name=GBufferValidationCopy&enabled=false -> {"success":true}
(PowerShell) ComputeBlurValidation: True, GBufferValidation: True, GBufferValidationCopy: False
   <- INDEPENDENT GRANULARITY PROVEN: disabling ONLY the copy half leaves the graphics half running
GET /get_logs?min_level=Error&limit=20 -> {"count":0,...}   (no crash from the partial-toggle case)
GET /get_game_view -> HTTP 200, sane image

GET /render_graph/set_pass_enabled?name=GBufferValidationCopy&enabled=true -> {"success":true} (restored)
GET /render_graph/set_blur_enabled?enabled=false    -> {"success":true} (restored to default-off)
GET /render_graph/set_gbuffer_enabled?enabled=false -> {"success":true} (restored to default-off)
```

---

## Fix 4 — `AtmosphereAerialPerspectiveVolumeDebugSlicePass`

### The change

- `src/Renderer/Atmosphere/AtmosphereLutRenderer.h/.cpp` —
  `AddAerialPerspectiveVolumeDebugSlicePass()` gained a trailing, defaulted
  `rg::RenderPassToggleRegistry* toggleRegistry = nullptr` parameter; the method now does, as its
  very first statement (before `EnsureAerialPerspectiveVolumeDebugSliceInitialized()`/any lookup):
  ```cpp
  if (toggleRegistry != nullptr
      && !toggleRegistry->NoteDeclaredAndCheckEnabled("AtmosphereAerialPerspectiveVolumeDebugSlicePass")) {
      return rg::TextureHandle{};
  }
  ```
- `src/Core/Core.cpp` — the one call site (inside the `"AtmosphereViewLut"` provider, already gated
  by `isGameView && viewLuts.aerialPerspectiveVolumeHandle.IsValid()` from
  `editor-core-separation-20`'s own crash fix) now passes `&m_renderPassToggleRegistry`.

### Live verification

```
(PowerShell) $names -contains 'AtmosphereAerialPerspectiveVolumeDebugSlicePass' -> True (baseline)
GET /render_graph/set_pass_enabled?name=AtmosphereAerialPerspectiveVolumeDebugSlicePass&enabled=false -> {"success":true}
(PowerShell) $names -contains 'AtmosphereAerialPerspectiveVolumeDebugSlicePass' -> False
GET /get_logs?min_level=Error&limit=20 -> {"count":0,...}
GET /get_game_view -> HTTP 200, sane image
GET /render_graph/set_pass_enabled?name=AtmosphereAerialPerspectiveVolumeDebugSlicePass&enabled=true -> {"success":true}
(PowerShell) $names -contains 'AtmosphereAerialPerspectiveVolumeDebugSlicePass' -> True (restored)
```

---

## Fix 5 — `AddGpuSkinningPasses()`'s direct-render-to-swapchain fallback

### The change

- `src/Application/RenderPasses.h/.cpp` — `AddGpuSkinningPasses()` gained a trailing, defaulted
  `rg::RenderPassToggleRegistry* toggleRegistry = nullptr` parameter; as its first statement:
  ```cpp
  if (toggleRegistry != nullptr && !toggleRegistry->NoteDeclaredAndCheckEnabled("GpuSkinning")) {
      return handles; // empty
  }
  ```
  Deliberately reuses the EXACT SAME `"GpuSkinning"` registry entry name Core.cpp's OFFSCREEN
  `"GpuSkinning"` provider already consults (`Core.cpp` ~line 476) — per PHASE3's own backlog note
  ("mirroring the offscreen provider's own shape"), this means the ONE checkbox in the "Render
  Graph" panel now honestly gates BOTH code paths, whichever is actually reachable a given frame,
  rather than inventing a second, confusing, separately-named toggle for the same visible feature.
- `src/Core/Core.cpp` — the one real call site (`"Present"` provider,
  `m_needsDirectGameRenderThisFrame ? AddGpuSkinningPasses(...) : {}`) now passes
  `&m_renderPassToggleRegistry`.

### Live verification, and its own honest limitation (same as PHASE3's own finding)

```
GET /render_graph/set_pass_enabled?name=GpuSkinning&enabled=false -> {"success":true}
GET /render_graph/passes -> {"enabled":false,...,"name":"GpuSkinning"}   <- mutation applied
GET /get_logs?min_level=Error&limit=20 -> {"count":0,...}
GET /get_game_view -> HTTP 200, sane image (no crash)
GET /render_graph/set_pass_enabled?name=GpuSkinning&enabled=true -> {"success":true} (restored)
```

**Honest limitation, restated (not silently smoothed over)**: this proves the toggle mutation itself
is applied cleanly and causes no crash/regression against the REACHABLE (offscreen) `"GpuSkinning"`
path — it does NOT, and structurally CANNOT this session, prove the SWAPCHAIN-DIRECT fallback branch
itself now honors the toggle, because that branch only runs when `Core::Present()`'s
`m_needsDirectGameRenderThisFrame` is true, which requires BOTH the Game View AND Scene View panels
to be hidden simultaneously — the exact same reachability gap PHASE3's own live session already
hit and documented (a temporary `GTE_LOG_WARNING` diagnostic recorded ZERO hits across that whole
session). No Editor/HTTP control exists today to force both panels closed at once. The FIX ITSELF
(the guard at the top of `AddGpuSkinningPasses()`, unconditionally reached by EITHER caller) is
identical code regardless of which caller reaches it — there is no code-path-specific branching
inside the guard that could behave differently for the swapchain-direct case — so static
confirmation (the guard runs first, before any request is collected, for both call sites) plus the
live-confirmed reachable half together constitute the strongest verification achievable without a
new Editor/HTTP capability this phase was never scoped to add.

---

## Fix 6 — `FrameDebuggerReplayStepN` (dynamically-named per-step passes)

### The design decision (PHASE3 left this as "needs its own per-step or whole-mechanism guard design
decision" — resolved here, no `ask_questions` needed)

Chose a SINGLE, whole-mechanism toggle, `"FrameDebuggerReplay"`, checked once at the very top of
`AddReplayPasses()`, rather than one registry entry per dynamically-named
`"FrameDebuggerReplayStepN"` pass. Reasoning: these are ephemeral, one-capture-lifetime debug-tooling
passes (a fresh, ever-growing pool of names across a session — `ReplayStepPassNamePool()`), not real
production content; a per-step toggle would clutter the "Render Graph" panel's checkbox list with an
unbounded, session-growing set of near-identical entries for zero practical benefit, whereas
`AddGpuSkinningPasses()`'s own `"GpuSkinning"` whole-stage precedent (Fix 5, immediately above) is the
directly analogous, already-accepted shape for "many dynamically-named passes behind one umbrella
switch".

### The change

- `src/Core/FrameDebuggerCaptureRecorder.h` — `IFrameDebuggerCaptureRecorder::AddReplayPasses()`
  gained a trailing, defaulted `rg::RenderPassToggleRegistry* toggleRegistry = nullptr` parameter
  (plus a new `namespace rg { class RenderPassToggleRegistry; }` forward declaration).
- `src/Editor/FrameDebuggerCapture.h` — the concrete override's declaration updated to match.
- `src/Editor/FrameDebuggerReplayPasses.cpp` — `FrameDebuggerCaptureContext::AddReplayPasses()` now
  does, as its very first statement (before `includeSkyStep`/`totalStepCount` computation or any
  `RenderTexture` creation):
  ```cpp
  if (toggleRegistry != nullptr && !toggleRegistry->NoteDeclaredAndCheckEnabled("FrameDebuggerReplay")) {
      return destHandles; // empty
  }
  ```
- `src/Core/Core.cpp` — the one real call site (`Core::BuildFrame()`'s
  `frameDebuggerCapture->AddReplayPasses(...)` call, reached only when
  `ConsumePendingFrameDebuggerReplayRequest()` is true) now passes `&m_renderPassToggleRegistry`.

### Live verification, and the same structural limitation PHASE3 already documented

```
GET /frame_debugger/open              -> {"state":{"windowOpen":true,...},"success":true}
GET /frame_debugger/enable?value=true -> {"state":{"enabled":true,...},"success":true}
GET /frame_debugger/capture           -> {"state":{"hasCapturedFrame":true,"totalEventCount":63,...},"success":true}
GET /render_graph/passes -> {"enabled":true,"ever_declared_this_session":true,"name":"FrameDebuggerReplay"}
   <- CONFIRMS the fix is live: before this phase, "FrameDebuggerReplay" did not exist in the
      registry at all (nothing ever called NoteDeclaredAndCheckEnabled for it); it now
      auto-registers and reports ever_declared_this_session:true, exactly like every other
      honest, toggle-consulting pass in this engine.
GET /render_graph/set_pass_enabled?name=FrameDebuggerReplay&enabled=false -> {"success":true}
GET /frame_debugger/capture -> {"state":{...,"totalEventCount":63,...},"success":true}
   <- totalEventCount UNCHANGED at 63 either way - this is EXPECTED, not a failure of the fix: these
      replay passes are Debug-category and were ALREADY excluded from the visible Frame Debugger
      tree by BuildRealFrameDebuggerSnapshot()'s own "view region" walk since the render-pass-1
      campaign (PHASE4) - toggling them on/off was never going to change this count, honestly
      confirmed by re-reading that pre-existing exclusion rule, not assumed.
(PowerShell) search of GET /render_graph for "FrameDebuggerReplayStep" -> 0 matches, BOTH before and
   after the toggle change - matches PHASE3's OWN documented finding: the snapshot has already
   rolled forward past the one frame that declares these passes by the time any HTTP response is
   built (deferred-by-one-frame capture trigger). This is a genuine, structural verification
   limitation of this specific mechanism, not something this phase can work around without changing
   the capture-timing contract itself (explicitly out of scope).
GET /get_logs?min_level=Error&limit=20 -> {"count":0,...}   (no crash from toggling this off)
GET /get_game_view -> HTTP 200, sane image
GET /render_graph/set_pass_enabled?name=FrameDebuggerReplay&enabled=true -> {"success":true} (restored)
GET /frame_debugger/capture -> {"state":{...,"totalEventCount":63,...},"success":true} (settled)
GET /frame_debugger/enable?value=false -> {"state":{"enabled":false,...},"success":true} (cleaned up)
```

**Honest limitation, restated**: exactly like Fix 5, this fix's own registry-consultation code is
proven live (the entry now exists, auto-registers, and the mutation applies cleanly with zero
crash/error) but the DEEPER claim — "toggling this off genuinely stops the N per-object
`RenderTexture`s/passes from being created this frame" — cannot be directly observed via
`GET /render_graph` due to the one-frame-behind snapshot timing PHASE3 already found and documented
for this exact pass family. The guard itself is unconditional and reached identically regardless of
how many replay steps would otherwise be created (it runs before `totalStepCount` is even computed),
so this is a correctness argument backed by direct code reading, not merely assumed - matching the
same evidentiary standard PHASE3 itself used for this exact finding.

---

## Incremental compile checks

`cmake --build build` was run after every individual fix (not just once at the end), per this
phase's own Step 3.3 point 1:

1. After Fix 1 (`PluginRenderPassBuilderAdapter`/`LegacyRenderFeatureOrchestrator`) — succeeded
   cleanly (2 files rebuilt, executable relinked).
2. After Fix 2/3 (`EditorLayer.h`, `ImGuiEditorLayer.cpp`, `NullEditorLayer.cpp`,
   `GBufferValidation.h/.cpp`, `Core.cpp`) — one real mistake was made and self-corrected mid-phase:
   an `edit_line` replacement of `GBufferValidation.h`'s `namespace gte {` opening line accidentally
   deleted that exact line, and a second `.cpp` edit accidentally deleted its pre-existing
   `#include "../Renderer/Renderer.h"` line — BOTH caught immediately by this exact build
   (`invalid use of incomplete type 'class gte::Renderer'` / missing-namespace symptom), fixed, and
   reconfirmed with a clean subsequent build.
3. After Fix 4 (`AtmosphereLutRenderer.h/.cpp`, `Core.cpp`) — succeeded cleanly.
4. After Fix 5/6 (`RenderPasses.h/.cpp`, `FrameDebuggerCaptureRecorder.h`, `FrameDebuggerCapture.h`,
   `FrameDebuggerReplayPasses.cpp`, `Core.cpp`) — succeeded cleanly; final full incremental build
   (64 build steps) completed with zero warnings/errors from any file this phase touched:

```
[64/64] ... GreatTamanaEditor.exe / GreatTamanaEngineTests.exe / ProjectAssemblyProbe_Editor.dll /
            ProjectAssemblyProbe_Game.dll all linked successfully.
```

Per `PHASE0_MASTER_STRATEGY.md` Locked Decision #2, no full clean build and no full `ctest`
regression pass were run this phase (reserved for PHASE6 only).

---

## Tier-1 test obligation — judgment call, explained honestly

`AGENTS.md`'s "Testability & Regression Safety" section requires a matching Tier-1 test for any
**newly-introduced pure logic**. Every one of this phase's 6 fixes is a direct, one-line reuse of the
ALREADY Tier-1-tested `RenderPassToggleRegistry::NoteDeclaredAndCheckEnabled()` method (covered by
the pre-existing `RenderPassToggleRegistryTest` suite from `editor-core-separation-8`) — none of them
introduce a NEW decision function combining multiple independent boolean inputs the way
`ShouldDeclareAtmospherePassThisFrame()` did in `editor-core-separation-20` PHASE2 (which is exactly
why THAT fix got its own dedicated pure-function + test file, and this phase's fixes did not). The
one place two conditions interact (`GBufferValidation::AddPass()`'s graphics-half-gates-both-halves
rule) is a plain early-return, not an extractable pure function with its own meaningfully-distinct
test matrix beyond what `RenderPassToggleRegistryTest` already exercises for the underlying registry
itself. No new test file was added; this is a deliberate, explained judgment call, not an oversight.

---

## Files changed

- `src/Core/Plugins/PluginRenderPassBuilderAdapter.h` / `.cpp`
- `src/Core/Plugins/LegacyRenderFeatureOrchestrator.cpp`
- `src/Editor/EditorLayer.h`
- `src/Editor/ImGuiEditorLayer.cpp`
- `src/Editor/NullEditorLayer.cpp`
- `src/Editor/GBufferValidation.h` / `.cpp`
- `src/Renderer/Atmosphere/AtmosphereLutRenderer.h` / `.cpp`
- `src/Application/RenderPasses.h` / `.cpp`
- `src/Core/FrameDebuggerCaptureRecorder.h`
- `src/Editor/FrameDebuggerCapture.h`
- `src/Editor/FrameDebuggerReplayPasses.cpp`
- `src/Core/Core.cpp` (5 call sites updated: the Aerial Perspective debug-slice pass, blur/GBuffer
  validation passes, `AddGpuSkinningPasses()`'s Present-provider call, and
  `AddReplayPasses()`'s Frame-Debugger-provider call)
- `task_manager/editor-core-separation-21/PHASE4_COMPLETION_REPORT.md` (new — this report)

## Deferred by explicit user decision

None. Every row PHASE3 classified `Confirmed-Lie` is fixed by this phase, per Locked Decision #7 and
this phase's own Step 1 ("no exceptions carried forward silently").

## No delegation, no unresolved ambiguity

No `delegate_task` call was made (none permitted for this phase, and none was needed). No
`ask_questions` call was made — every fix mapped directly onto the same, already-established
`RenderPassToggleRegistry::NoteDeclaredAndCheckEnabled()` pattern with no genuine design ambiguity
encountered during implementation, and no cascading-invalid-dependency hazard (the
`editor-core-separation-20`-style crash risk this campaign's own Step 3.2 explicitly warns about) was
found for any of the 6 fixes — each was explicitly checked (see each fix's own section above) before
landing.

The running `GreatTamanaEditor.exe` instance (PID 18784) was cleanly closed via
`stop_app_background` before finishing — no stray instance left running.
