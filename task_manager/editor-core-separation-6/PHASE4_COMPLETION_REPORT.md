# PHASE4 — `RenderFeatureCompositor` Core: Ordering, Private Targets, Collision Detection — COMPLETION REPORT

**Status: DONE.** Implemented per `PHASE4_RENDER_FEATURE_COMPOSITOR_CORE_AND_ORDERING.md`'s
own Step 3.1–3.8, with a small number of confirmed, necessary, additive
deviations (see "Deviations from the plan" below) found while implementing
Step 3.4/3.5's own real Vulkan resource-binding requirements.

## What changed

### 1. New shaders

- **`src/Shaders/RenderFeatureOps.comp`** — the "uber" compute shader
  implementing all 3 `IPluginRenderPassBuilder_v2` drawing operations
  (Solid Fill / Radial Vignette / Color Grade), `opCode`-selected via a
  64-byte push-constant block, `binding = 0` = a single read-write storage
  image (`privateTarget`). Content matches the phase file's own Step 3.1
  code block verbatim.
- **`src/Shaders/RenderFeatureBlendStub.comp`** — THIS PHASE's own temporary,
  Replace-equivalent blend/seed dispatch (binding 0/1 = `dstIn`/`srcIn`
  combined image samplers, binding 2 = `destinationImage` write-only storage
  image) — deliberately identical binding shape to PHASE5's own future real
  `RenderFeatureBlend.comp`, per the phase file's own Step 3.6. Content
  matches verbatim.
- Both registered in the root `CMakeLists.txt` via
  `gte_add_shader(GreatTamanaEditor ...)`, immediately after
  `VolumeTexturePreview.comp`'s own existing entry.

### 2. New files — `src/Core/Plugins/RenderFeatureNamePool.h`

A small, header-only adaptation of `Core.cpp`'s own
`GpuDrivenBatchNamePool` (Step 3.3) — interns stable, whole-process-lifetime
`const char*` names for every per-(plugin, view) private/accumulator/blend-
pass slot, plus the per-view seed/seed-copy slot, keyed by the exact same
string used both as the map key AND the render-graph resource/pass name
(mirroring `AtmosphereLutRenderer`'s own `std::unordered_map<std::string,
ViewState>` keyed directly by `outputTextureName` convention — no separate
"key" vs. "interned name" concept was needed).

### 3. New files — `src/Core/Plugins/PluginRenderPassBuilderAdapter_v2.h`/`.cpp`

Implements `IPluginRenderPassBuilder_v2`. Constructed fresh per plugin, per
view, per frame by `RenderFeatureCompositor::ContributeRenderGraphPasses()`.
All 3 methods (`AddSolidFillPass`/`AddRadialVignettePass`/`AddColorGradePass`)
forward into `RenderFeatureCompositor::DispatchOps()`, filling in the
`opCode` and the relevant `RenderFeatureOpsPushConstants` fields, exactly
per the phase file's own Step 3.2.

### 4. New files — `src/Core/Plugins/RenderFeatureCompositor.h`/`.cpp`

The third real `IPluginCapabilityOrchestrator` implementation, and the
permanent home of the real `_v2` compositing pipeline:

- **`OnPluginsLoaded()`** — discovers every loaded `IRenderFeatureModule_v2`,
  snapshots its `GtePluginRenderFeatureDescriptor` once, refuses (loudly, via
  `GTE_LOG_WARNING`, never invoked afterward) any module declaring
  `PreOpaque`/`PostOpaque`/`PostTransparent` (Locked Design Decision #1),
  sorts each of `m_postComposite`/`m_preUi` by `priority` ascending with a
  documented, stable, lexical (`std::strcmp`) tie-break for a same-priority
  collision within a contiguous run (logging one warning per adjacent
  colliding pair in that run), then populates `RenderFeatureNamePool` for
  every surviving entry, for both known views (`"Game"`/`"Scene"`).
- **`ContributeRenderGraphPasses()`** — builds the combined
  (`PostComposite` then `PreUI`) ordered list (Locked Design Decision #2);
  resolves the view's real target+extent+sampler via
  `Core::FindPluginRenderFeatureTarget()`; **seeds the chain** with one
  compute dispatch copying the view's current composited image into a
  per-view seed target (closing the same-physical-image read+write hazard
  the `N == 1` case would otherwise hit — see "Deviations" below for the
  one real gap this closed that the plan's own pseudocode didn't fully
  spell out); then, for each entry in the combined list, imports/creates its
  own private target, hands it a fresh `PluginRenderPassBuilderAdapter_v2`,
  calls `AddRenderGraphPasses()`, and declares one blend-compositing compute
  pass writing either the next accumulator (for every entry except the
  last) or — for the LAST entry — directly into the view's own real,
  final handle (Locked Design Decision #10).
- **`DispatchOps()`** — the real `RenderFeatureOps.comp` dispatch, keyed by
  the interned private-target name (a programmer error, guarded by
  `assert()`, if called for a name `ContributeRenderGraphPasses()` did not
  already create this frame).
- Every declared pass uses `rg::RenderPassEvent::AfterEverything` (mirroring
  `PluginRenderPassBuilderAdapter`'s own `_v1` precedent and its documented
  reasoning about write-only passes needing their own ordering tier, since
  they have no real data dependency forcing them after later-tier
  production passes).

### 5. `Core.h`/`Core.cpp` wiring

- `RegisterBuiltinCapabilityOrchestrators()` gained its third line:
  `m_capabilityOrchestrators.push_back(std::make_unique<RenderFeatureCompositor>(*this, m_renderer));`.
- `Core::PluginRenderFeatureTargetInfo` gained a new field, `VkSampler
  sampler = VK_NULL_HANDLE;` (see "Deviations" below for why this was
  needed and how it's resolved).
- `#include "Plugins/RenderFeatureCompositor.h"` added alongside the
  existing `LegacyRenderFeatureOrchestrator.h`/`EditorPanelCapabilityOrchestrator.h`
  includes.

### 6. `CMakeLists.txt`

Added the 5 new `src/Core/Plugins/` files (`RenderFeatureNamePool.h`,
`PluginRenderPassBuilderAdapter_v2.h`/`.cpp`, `RenderFeatureCompositor.h`/`.cpp`)
to `gte_core`'s source list, and the 2 new `gte_add_shader(...)` calls, per
Step 3.8.

## Deviations from the plan

**Two confirmed, necessary, additive deviations, both found while filling in
real Vulkan-binding details the plan's own pseudocode left slightly
underspecified — neither changes any Locked Design Decision, any observable
`_v1` behavior, or this phase's own stated scope:**

1. **`Core::FindPluginRenderFeatureTarget()` changed from `const` to
   non-`const`, and `Core::PluginRenderFeatureTargetInfo` gained a new
   `VkSampler sampler` field.** The plan's own Step 3.6 shader
   (`RenderFeatureBlendStub.comp`) declares `dstIn`/`srcIn` as
   `sampler2D` (combined image samplers) — for the one-time-per-view
   **seed** dispatch, `srcIn = resolved->target` (the view's real,
   already-imported composited image this same frame). An imported
   `TextureHandle`'s own `PassContext::resolveTexture()` never carries a
   sampler (confirmed by direct read of `RenderGraph.cpp`: `tex.sampler =
   VK_NULL_HANDLE; // TextureImportInfo carries no sampler of its own.`),
   so a real `VkSampler` for `resolved->target` had to come from somewhere
   external — mirroring `AtmosphereLutRenderer::
   AddAerialPerspectiveCompositePass()`'s own established
   `sourceColorSampler` externally-resolved-parameter precedent exactly.
   `FindPluginRenderFeatureTarget()` now additionally resolves this sampler
   from `AtmosphereLutRenderer::CompositedOutput()`'s own persistent
   `RenderTexture` (the same one `"AtmosphereComposite"` just registered
   `composited` under this exact frame), falling back to
   `viewData->renderTexture->Sampler()` in the same defensive case
   `.target` itself already falls back to `viewData->colorTarget`.
   `AtmosphereLutRenderer::CompositedOutput()` is itself a non-`const`
   method, which is why the accessor's own constness had to change too —
   confirmed via `search_in_dir` that the one real call site
   (`LegacyRenderFeatureOrchestrator`) already holds a non-`const` `Core&`,
   so this is a safe, zero-impact widening. Every existing consumer
   (`LegacyRenderFeatureOrchestrator`) simply ignores the new field, exactly
   like it already ignores `.extent`.
2. **Every one of `PrivateTargetState`/`BlendStageState`'s own dedicated
   `ComputeDescriptorSet` is allocated once, at state-creation time**
   (mirroring `AtmosphereLutRenderer::EnsureAerialPerspectiveCompositeViewInitialized()`'s
   own "allocate the descriptor set together with the texture, at first-use
   time" shape), rather than the plan's own Step 3.2 wording ("looks up (or
   lazily allocates, first use, ...)" phrased as if this happened inside
   `DispatchOps()` itself) — a purely organizational refinement with
   identical runtime behavior: the descriptor set is still allocated
   exactly once, the first time each (plugin, view) slot is seen, and
   reused/`Rewrite()`-ed every subsequent frame.

**Confirmed honored, not a deviation:** the caveat the plan's own Step 3.4
explicitly flagged ("resolved->target can... resolve to the RAW
`viewData->colorTarget`... NOT guaranteed to have been created with
`allowStorageImageAccess = true`") remains a real, pre-existing,
un-designed-around defensive edge case, exactly as instructed — this
phase's own verification never exercised that fallback branch (the
Atmosphere Composite pass ran normally every frame during testing, so
`resolved->target` was always `"GameViewComposited"`/`"SceneViewComposited"`,
both created with `allowStorageImageAccess = true`).

## Verification evidence

### 1. Incremental build

`cmake --build build` (Ninja/MinGW). **13/14 steps succeeded** on the first
attempt with zero compile errors (only the two new `.comp` shader compiles
and the expected pre-existing `MingwRuntime.cmake`/KTX-version warnings in
`stderr`):

```
[1/14] Building CXX object CMakeFiles/gte_core.dir/src/Core/Plugins/PluginRenderPassBuilderAdapter_v2.cpp.obj
[2/14] Building CXX object CMakeFiles/gte_core.dir/src/Core/Plugins/LegacyRenderFeatureOrchestrator.cpp.obj
[3/14] Building CXX object CMakeFiles/gte_core.dir/src/Core/Plugins/RenderFeatureCompositor.cpp.obj
[4/14] Building CXX object CMakeFiles/gte_core.dir/src/Core/Core.cpp.obj
...
[7/14] Linking CXX static library libgte_core.a
[8/14] Linking CXX static library libgte_editor.a
[9/14]  Compiling shader src/Shaders/RenderFeatureOps.comp -> .../build/shaders/RenderFeatureOps.comp.spv
[10/14] Compiling shader src/Shaders/RenderFeatureBlendStub.comp -> .../build/shaders/RenderFeatureBlendStub.comp.spv
[12/14] Linking CXX executable GreatTamanaEditor.exe; ...
[13/14] Linking CXX executable tests\GreatTamanaEngineTests.exe; ...
```

### 2. Live smoke test — throwaway probe plugin loaded (Step 3.7)

Created a temporary `plugins/_scratch_render_feature_v2_probe/` (implementing
`IRenderFeatureModule_v2`, `stage = PostComposite`, `priority = 0`,
`blendMode = Replace`, calling `AddSolidFillPass(..., 0,1,0,1)` — solid
GREEN), registered temporarily in the root `CMakeLists.txt`, rebuilt
(2/2 steps, clean compile), and ran `build\GreatTamanaEditor.exe` (PID 7948).

**`GET /get_logs?limit=100`** — the probe plugin loaded correctly alongside
all 4 pre-existing demo plugins, with **no new/unexpected warning** (no
unwired-stage warning, no priority-collision warning — the probe declares a
single, unique priority):

```
[Info]    PluginHost: "Loaded plugin 'ScratchRenderFeatureV2Probe' v0.0.0 from ...\plugins\_scratch_render_feature_v2_probe.dll"
[Warning] PluginHost: "2 loaded plugins implement IRenderFeatureModule_v1 - only the LAST-registered one's render output will be visible this frame..." (pre-existing `_v1` warning, unchanged)
```

**`GET /get_game_view`** — returned a solid **GREEN** 2367-byte PNG (visually
confirmed via `load_image`) — proving this phase's own new `_v2` compositor
pipeline, not the pre-existing `_v1` magenta path, produced the final pixel
(both are loaded simultaneously, Locked Design Decision #9, so seeing green
proves `_v2`'s own last-write genuinely reached the same final handle).

**`GET /get_swapchain`** — confirmed BOTH the "Scene" panel and the "Game"
panel show solid green simultaneously (the compositor runs
`ProviderScope::PerActiveView`, so both views were exercised, not just Game
View) — no crash, no Vulkan validation-layer error text anywhere in the
captured logs (the concrete, observable symptom a descriptor-set-sharing or
non-storage-transient-texture mistake would have produced, per the phase
file's own Step 2 warnings).

`stop_app_background` called at the end of the check (PID 7948).

### 3. Probe removed — baseline reverts

Deleted `plugins/_scratch_render_feature_v2_probe/` (folder + its own
`CMakeLists.txt`) and its `add_subdirectory(...)` line, deleted the stale
`build/plugins/_scratch_render_feature_v2_probe.dll`/build folder, re-ran
`cmake --build build` (clean reconfigure, `ninja: no work to do` — no
leftover target), then re-ran `build\GreatTamanaEditor.exe` (PID 2824):

**`GET /get_game_view`** — returned solid **MAGENTA** again (2368-byte PNG),
confirming this phase's own new `_v2` code path is correctly and completely
inert with zero `_v2` plugins loaded — the pre-existing `_v1` baseline is
untouched.

**`GET /get_logs?limit=100`** — byte-for-byte the same pre-existing 4-plugin
load sequence/warning text as every prior phase's own recorded baseline (no
`ScratchRenderFeatureV2Probe` line, no new warning).

`stop_app_background` called at the end of the check (PID 2824).

### 4. `git_status`

Before commit, the diff is exactly:

```
modified:   CMakeLists.txt
modified:   src/Core/Core.cpp
modified:   src/Core/Core.h
new file:   src/Core/Plugins/PluginRenderPassBuilderAdapter_v2.cpp
new file:   src/Core/Plugins/PluginRenderPassBuilderAdapter_v2.h
new file:   src/Core/Plugins/RenderFeatureCompositor.cpp
new file:   src/Core/Plugins/RenderFeatureCompositor.h
new file:   src/Core/Plugins/RenderFeatureNamePool.h
new file:   src/Shaders/RenderFeatureBlendStub.comp
new file:   src/Shaders/RenderFeatureOps.comp
```

— **zero trace of the throwaway probe plugin**, confirming Workflow Rule 10
and the phase file's own Step 3.7 cleanup requirement.

## What this phase does NOT do (confirmed honored)

- Does not implement real, multi-mode blending (`RenderFeatureBlend.comp`,
  all 5 `RenderFeatureBlendMode` values) — the blend/seed dispatch stays
  hardcoded to the Replace-equivalent stub — PHASE5's own job.
- Does not wire the `PreUI`-after-`PostComposite` two-sub-stage ordering
  test with 2+ real plugins — this phase's own single throwaway probe only
  exercised the `N == 1` case (the seeding step makes this a genuinely safe,
  verified case rather than a hidden GPU hazard, exactly as designed).
- Does not add any permanent, committed demo plugin — PHASE6.
- Does not touch the Render Graph panel — PHASE7.
- Every existing `_v1`-era log line, pass name, panel name, and warning
  string was confirmed byte-for-byte unchanged (verification steps 2/3
  above).
