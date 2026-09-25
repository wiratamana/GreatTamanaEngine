# PHASE5 — Real Blend-Mode Compute Shader + `PostComposite`→`PreUI` Sub-Stage Ordering — COMPLETION REPORT

**Status: DONE.** Implemented per
`PHASE5_BLEND_MODE_COMPUTE_SHADER_AND_PREUI_STAGE.md`'s own Step 3.1–3.3, with
zero deviation from the plan.

## What changed

### 1. New shader — `src/Shaders/RenderFeatureBlend.comp`

The real, permanent, 5-mode blend uber-shader (Replace / AlphaOver / Additive
/ Multiply / ScreenSpaceMask), selected via a `push_constant` integer
(`blendModeAndPad.x`). Content matches the phase file's own Step 3.1 code
block verbatim. Registered via
`gte_add_shader(GreatTamanaEditor src/Shaders/RenderFeatureBlend.comp)` in the
root `CMakeLists.txt`, immediately replacing the deleted
`gte_add_shader(GreatTamanaEditor src/Shaders/RenderFeatureBlendStub.comp)`
line.

`src/Shaders/RenderFeatureBlendStub.comp` (PHASE4's own throwaway,
Replace-only stub) was **deleted outright**, per the phase file's own explicit
instruction.

### 2. `src/Core/Plugins/RenderFeatureCompositor.h`

- Added `RenderFeatureBlendPushConstants` (1 `vec4`, 16 bytes,
  `.x` = `RenderFeatureBlendMode` cast to float), mirroring
  `RenderFeatureOpsPushConstants`'s own existing shape.
- Renamed `m_blendStubPipeline` → `m_blendPipeline`,
  `m_blendStubDescriptorSetLayout` → `m_blendDescriptorSetLayout`.
- Renamed `EnsureBlendStubInitialized()` → `EnsureBlendPipelineInitialized()`.
- Renamed `DispatchBlendStub(...)` → `DispatchBlend(...)`, with a new
  trailing `RenderFeatureBlendMode blendMode` parameter.
- Updated every doc comment referencing the old stub/PHASE4-temporary
  language to reflect PHASE5's own permanent state.

### 3. `src/Core/Plugins/RenderFeatureCompositor.cpp`

- `EnsureBlendPipelineInitialized()` — identical descriptor-set-layout shape
  to PHASE4's stub initializer (2 combined-image-samplers + 1 storage image,
  binding 0/1/2 respectively — confirmed byte-for-byte identical to
  `RenderFeatureBlend.comp`'s own binding declarations), now additionally
  builds a `VkPushConstantRange` (`sizeof(RenderFeatureBlendPushConstants)`)
  and passes it into `CreateComputePipeline(...)`, and loads
  `"shaders/RenderFeatureBlend.comp.spv"` instead of
  `"shaders/RenderFeatureBlendStub.comp.spv"`.
- `DispatchBlend(...)` — now builds a real `RenderFeatureBlendPushConstants`
  from the passed-in `blendMode` and forwards it (pointer + size) into
  `m_renderer.Dispatch(...)`, replacing the stub's `nullptr, 0` push-constant
  arguments.
- `ContributeRenderGraphPasses()`:
  - Calls `EnsureBlendPipelineInitialized(m_renderer)` instead of
    `EnsureBlendStubInitialized(m_renderer)`.
  - The per-view **seed** dispatch now explicitly passes
    `RenderFeatureBlendMode::Replace` — a plain copy, regardless of any
    individual plugin's own declared blend mode, per the phase file's own
    Step 3.2.
  - The per-plugin blend dispatch (`DispatchBlend(...)`) now passes
    `entry.descriptor.blendMode` — this plugin's own author-declared blend
    mode — instead of nothing.
  - No other structural change: the ordering/collision-detection/
    private-target/seeding logic PHASE4 already built is completely
    untouched (confirmed by diffing the function body — only the blend-mode
    argument and the two renamed call targets changed).

### 4. `CMakeLists.txt`

Replaced the PHASE4 comment block + the single
`gte_add_shader(GreatTamanaEditor src/Shaders/RenderFeatureBlendStub.comp)`
line with an updated comment block + `RenderFeatureBlend.comp`'s own
registration line, in the exact same position (immediately after
`RenderFeatureOps.comp`'s own line).

## Deviations from the plan

**None.** Every structural change matches
`PHASE5_BLEND_MODE_COMPUTE_SHADER_AND_PREUI_STAGE.md`'s own Step 3.1/3.2
exactly — same shader binding shape, same push-constant layout, same rename
set, same call-site changes, same seed-dispatch Replace-mode override.

## Verification evidence

### 1. Incremental build

`cmake --build build` (Ninja/MinGW). **7/8 steps succeeded** on the first
attempt with zero compile errors:

```
[1/8] Building CXX object CMakeFiles/gte_core.dir/src/Core/Plugins/PluginRenderPassBuilderAdapter_v2.cpp.obj
[2/8] Building CXX object CMakeFiles/gte_core.dir/src/Core/Plugins/RenderFeatureCompositor.cpp.obj
[3/8] Building CXX object CMakeFiles/gte_core.dir/src/Core/Core.cpp.obj
[4/8] Linking CXX static library libgte_core.a
[5/8] Compiling shader src/Shaders/RenderFeatureBlend.comp -> .../build/shaders/RenderFeatureBlend.comp.spv
[6/8] Linking CXX executable GreatTamanaEditor.exe; ...
[7/8] Linking CXX executable tests\GreatTamanaEngineTests.exe; ...
```

(Only the expected pre-existing `MingwRuntime.cmake`/KTX-version warnings on
`stderr` — no new warnings, no errors.)

### 2. Live smoke test — two scratch stage-A/stage-B plugins loaded (Step 3.3)

Created two temporary plugins per the phase file's own exact spec, registered
in the root `CMakeLists.txt`:

- `plugins/_scratch_render_feature_v2_stage_a/` — `stage = PostComposite,
  priority = 0, blendMode = Replace`, `AddSolidFillPass("StageA_Fill", 1.0,
  0.0, 0.0, 1.0)` (solid opaque RED).
- `plugins/_scratch_render_feature_v2_stage_b/` — `stage = PreUI, priority =
  0, blendMode = AlphaOver`, `AddRadialVignettePass("StageB_Vignette", 0.5,
  0.5, 0.1, 0.6, 0.0, 0.0, 1.0, 1.0)` (a BLUE vignette, centered, opaque at
  center, falling to alpha 0 at the edges).

Rebuilt (5/5 steps, clean compile, `_scratch_render_feature_v2_stage_a.dll`/
`_scratch_render_feature_v2_stage_b.dll` linked successfully), then ran
`build\GreatTamanaEditor.exe` (PID 19268).

**`GET /get_logs?limit=100`** — both scratch plugins loaded correctly
alongside all 4 pre-existing demo plugins, with **no unwired-stage warning,
no priority-collision warning** (each declares a unique priority within its
own stage — `ScratchRenderFeatureV2StageA` in `PostComposite`,
`ScratchRenderFeatureV2StageB` in `PreUI`, never colliding with each other).
The pre-existing `_v1` "2 loaded plugins implement IRenderFeatureModule_v1"
warning is present, byte-for-byte unchanged. (One unrelated, pre-existing,
purely cosmetic `RenderGraph` GPU-timing-slot-budget-exhaustion warning
appeared for 3 new pass names — a documented, harmless, pre-existing
`render-pass-6`-campaign mechanism (`README.md`'s own "PHASE1" entry for that
campaign) unrelated to this phase's own scope; GPU timing simply reads
"Absent" for those passes, nothing else is affected.)

**`GET /get_game_view`** — hand-computed expectation
(`PHASE5_BLEND_MODE_COMPUTE_SHADER_AND_PREUI_STAGE.md`'s own Step 3.3):
CENTER solid BLUE (opaque red from stage A, `AlphaOver`-blended with fully
opaque blue vignette center, `mix(dst, src, 1.0) == src == blue`); EDGES/
corners solid RED (stage A's fill, unmodified, since stage B's vignette alpha
has fallen to 0 there, `mix(dst, src, 0.0) == dst == red`); a soft
red-to-blue gradient ring in between.

**Actual captured image (via `load_image`): MATCHES EXACTLY** — a solid blue
disc at the center, solid red everywhere outside the vignette's outer radius,
and a smooth radial red→blue gradient ring in between. Confirmed
independently via **both** `GET /get_game_view` (Game View alone) and
`GET /get_swapchain` (both the "Scene" AND "Game" panels simultaneously show
the identical pattern, since the compositor runs `ProviderScope::
PerActiveView`) — no crash, no Vulkan validation-layer error text anywhere in
the captured logs.

This is a real, positive proof that:
1. `PostComposite` entries run and composite BEFORE `PreUI` entries (a
   reversed order would show the OPPOSITE center/edge coloring — blue edges,
   red center — exactly the failure mode this specific 2-color, 2-blend-mode
   combination was chosen to catch).
2. The real `AlphaOver` blend formula (`mix(dst.rgb, src.rgb, src.a)`) is
   genuinely being computed per-pixel, not just "last write wins" — the
   smooth gradient ring is only possible if `src.a` (the vignette's radial
   alpha falloff) is actually read and used to interpolate, pixel by pixel.
3. The seed dispatch's own hardcoded `RenderFeatureBlendMode::Replace`
   override works correctly even though the frame has 2 real, differently-
   configured plugins with different declared blend modes.

`stop_app_background` called at the end of the check (PID 19268).

### 3. Scratch plugins removed — baseline reverts

Deleted both `plugins/_scratch_render_feature_v2_stage_a/` and
`plugins/_scratch_render_feature_v2_stage_b/` (folder + their own
`CMakeLists.txt`), their two `add_subdirectory(...)` lines, and the stale
`build/plugins/_scratch_render_feature_v2_stage_a`/`_stage_b` build
directories + `.dll`s. Re-ran `cmake --build build`
(`ninja: no work to do` — confirming zero leftover target), then re-ran
`build\GreatTamanaEditor.exe` (PID 12568):

**`GET /get_game_view`** — returned solid **MAGENTA** again (2368-byte PNG),
confirming this phase's own change to the permanent `_v2` blend pipeline is
correctly and completely inert with zero `_v2` plugins loaded — the
pre-existing `_v1` baseline is untouched.

**`GET /get_logs?limit=100`** — byte-for-byte the same pre-existing 4-plugin
load sequence/warning text as every prior phase's own recorded baseline (no
`ScratchRenderFeatureV2Stage[AB]` line, no new warning, no GPU-timing-slot
warning either, since those 3 extra pass names no longer exist this frame).

`stop_app_background` called at the end of the check (PID 12568).

### 4. `git_status`

Before commit, the diff is exactly:

```
modified:   CMakeLists.txt
modified:   src/Core/Plugins/RenderFeatureCompositor.cpp
modified:   src/Core/Plugins/RenderFeatureCompositor.h
deleted:    src/Shaders/RenderFeatureBlendStub.comp
(untracked) src/Shaders/RenderFeatureBlend.comp
```

— **zero trace of either scratch plugin**, confirming Workflow Rule 10 and
the phase file's own Step 3.3 cleanup requirement.

## What this phase does NOT do (confirmed honored)

- Does not add any permanent, committed demo plugin — PHASE6's own job.
- Does not touch the Render Graph panel — PHASE7's own job.
- Does not change `IPluginRenderPassBuilder_v2`'s own 3 fixed operations in
  any way — only the BLEND step between plugins changed this phase, exactly
  as scoped.
- Every existing `_v1`-era log line, pass name, panel name, and warning
  string was confirmed byte-for-byte unchanged (verification steps 2/3
  above — same "2 loaded plugins implement IRenderFeatureModule_v1" text,
  same magenta baseline).

## Summary

`RenderFeatureCompositor` is now functionally complete, per the phase file's
own Step 1 goal: the real 5-mode blend shader replaced PHASE4's throwaway
stub with zero structural change elsewhere, and the `PostComposite`→`PreUI`
two-sub-stage ordering (PHASE0 Locked Design Decision #2) was proven correct
with a real, live, pixel-level, hand-computed-vs-actual comparison using 2
differently-configured, differently-blended, differently-staged throwaway
plugins — exactly matching the expected result with no discrepancy. PHASE6
can now proceed to add the permanent, committed demo plugins and the full
pixel-proof artifact on top of this completed foundation.
