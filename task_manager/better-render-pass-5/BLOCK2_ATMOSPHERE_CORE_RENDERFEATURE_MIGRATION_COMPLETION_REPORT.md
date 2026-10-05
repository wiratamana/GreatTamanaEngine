# BLOCK 2 — Atmosphere Core RenderFeature Migration — Completion Report

Source instruction: `2_ATMOSPHERE_CORE_RENDERFEATURE_MIGRATION.txt`.
Depends on: `1_SETUP_PREREQUISITES.txt` (Fix #1/Fix #2) — confirmed already shipped
in this branch before this work started (`ProjectRenderFeatureCallback` already
carries the 7-parameter shape with `ScenePassReadHandles`/`RenderFeatureCameraData`,
`RenderGraphBuilder::PassReadsTextureAsDepth()`/`ResolveImportedTextureSamplers()`
already exist).

## Honest final shape

**Two of Atmosphere's four passes migrated. Two remain permanent, documented
exceptions. Not "all four migrated."**

| Pass | Outcome |
|---|---|
| `AtmosphereSharedLut` | **Permanent exception.** Stays on `RegisterProjectRenderPassProvider()` (`ProviderScope::Once`) — no `RenderFeatureStage` models "once per frame, shared across every view". |
| `AtmosphereViewLut` | **Migrated** to `Core::AddPreOpaquePass()`. |
| `DrawSkyBackground` | **Permanent exception.** Writes directly into the view's own shared color+depth target — no `RenderFeatureStage` contract allows that. |
| `AtmosphereComposite` | **Migrated** to `Core::RegisterProjectRenderFeature()` (`PostComposite`, `AlphaOver`, priority `-1000`). |

## What shipped

### 1. `RenderGraphBuilder::KeepTextureOutput()` (Section 3a)
Added the plain-`TextureHandle` counterpart of `KeepVolumeTextureOutput()`/
`KeepBufferOutput()`/`KeepTextureArrayOutput()`, plus a read-only
`FinalTextureOutputs()` accessor and the one real merge site in `Core.cpp`
(right after `m_offscreenRenderPipeline.DeclareInto()` returns). A
PreOpaque/PostOpaque/PostTransparent callback has no access to
`RenderPassFrameContext::finalTextureOutputs`; this closes that gap for any
future caller, not just this migration.

### 2. Migration 1 — `AtmosphereViewLut` → `Core::AddPreOpaquePass()`
Priority `0` (no other real PreOpaque feature collides with it today — the
`ProjectAssemblyProbe` sample's own `PreOpaqueMarkerProducer` is also at
priority `0` but is a genuinely unrelated pass with no data dependency on
this one; a logged, stable, lexical tie-break is the documented, accepted
outcome of two unrelated same-priority entries, not a bug). Return value
asserted. Old raw provider registration removed — never ran both at once.

**A genuine ordering bug was found and fixed during this work, not described
in the source instructions:** `RenderPipeline::DeclareOnePhase()` invoked
every registered provider in one single flat pass, in registration order.
Core's own providers (including the new `"PreOpaqueFeatures"` provider that
invokes every `AddPreOpaquePass()` callback) are always registered *before*
any feature module's own providers, because a feature module is always
constructed after `Core`. Migrating `AtmosphereViewLut` onto
`AddPreOpaquePass()` would have made it run *before* the still-raw
`AtmosphereSharedLut` provider ever published its blackboard entry — every
single frame, forever — silently breaking the entire atmosphere system (the
callback would `Fetch()` nothing and return early, forever). This is not a
one-off mistake; it is structural, and would hit *any* future PreOpaque
migration with a `ProviderScope::Once` dependency published by the same
feature module. Fixed by splitting `DeclareOnePhase()`'s invocation into two
passes: every `ProviderScope::Once` provider first (registration order),
then every `ProviderScope::PerActiveView` provider — so a frame-shared
producer is always available to any per-view consumer in the same timing
phase, regardless of which module registered which provider first. Verified
live (see Verification below) and confirmed against the full existing Tier-1
suite (`RenderPipelineTests.cpp` has no test asserting invocation order
between a `Once` and a `PerActiveView` provider — only the final,
order-independent `stable_sort` of deferred passes, which this change does
not touch).

### 3. Migration 2 — `AtmosphereComposite` → `Core::RegisterProjectRenderFeature()`
`RenderFeatureStage::PostComposite`, `RenderFeatureBlendMode::AlphaOver`,
reserved priority `-1000` (far below any `ScreenPostProcessPassPriorityAssignment.h`
auto-assigned value). Return value asserted.

**Design decision taken (the instruction file contained two self-contradicting
directions here — see "Deviation from the instructions" below): kept the
compute shader and its push-constant math 100% unchanged.** The Aerial
Perspective Composite shader keeps writing the exact same, already-blended,
always-`alpha=1` pixel values into its own persistent
`"GameViewComposited"`/`"SceneViewComposited"` output, exactly as before this
migration. The new feature callback adds exactly one small, additive step: a
`builder.AddBlitPass()` plain image copy from that named output into the
generic compositor's own `privateTarget` for this frame, tagged
`RenderPassEvent::AfterEverything`. Since this feature is pinned to run first
(lowest priority) among `PostComposite` features, the generic
`RenderFeatureBlend.comp` `AlphaOver` dispatch (`result = mix(dst, src,
src.a)` with `src.a = 1`) reduces to `result = src` for this entry — bit-for-bit
equivalent to today's direct write. Confirmed live: the Render Graph panel
shows `"AtmosphereComposite.CopyToPrivateTarget"` (Blit, reads
`"GameViewComposited"`, writes `"ProjectFeatureSlot15_Game_Private"`, tagged
`AfterEverything`) immediately followed by `"ProjectFeatureSlot15_Game_Blend"`
reading both the seed and the private target.

**A second, real signature fix was required and is not mentioned in the
source instructions:** `AtmosphereLutRenderer::AddAerialPerspectiveCompositePass()`
(and its `AtmospherePassSequence.h` wrapper, `AddAtmosphereCompositePass()`)
used to accept **one** `sourceColorHandle` for *both* its color-aspect
`ReadTexture()` declaration *and* its depth-aspect `ReadTexture()`
declaration — correct only because, pre-migration, `viewData->colorTarget`
served both roles identically. After migration, `ScenePassReadHandles`
deliberately separates `colorHandle` (the compositor's own running "screen so
far" copy) from `depthHandle` (the view's own real target, since the private
seed copy has no depth sub-resource at all). Declaring the depth-aspect read
against the wrong handle would have barriered the wrong physical resource —
a silent, hard-to-diagnose wrong-frame bug, exactly the class of bug Fix #1
of the SETUP file exists to prevent. Both functions now take a separate
`sourceDepthHandle` parameter for the depth-aspect `ReadTexture()` call only;
`ctx.resolveTexture()`/the actual bound samplers are unaffected (depth was
always bound via the raw, caller-supplied `sourceDepthView`/`sourceDepthSampler`,
never through the render graph's resolve path).

The internal `AtmosphereAerialPerspectiveCompositePass` compute pass's own
`RenderPassEvent` tag changed from `AfterTransparents` to `AfterEverything`,
as required by `RegisterProjectRenderFeature()`'s contract — confirmed live,
visible in the Render Graph panel.

`AtmosphereComposite` no longer publishes `Core::ViewCompositedOutputEntry` —
it is now one of `RenderFeatureCompositor`'s own entries, so the generic
blend chain seeds itself from the view's raw pre-composite target directly
(`Core::FindPluginRenderFeatureTarget()`'s own fallback), exactly as intended.

### 4. Section 4b — `FindPassesNotTaggedAfterEverything()`
Added next to `ProjectRenderFeatureCallback`, mirroring
`FindPassesNotTaggedPreOpaque()`'s exact pure-function shape. Wired into
`RenderFeatureCompositor::ContributeRenderGraphPasses()` right after the
existing depth-read safety net (same `before`/`after` snapshot). Five new
Tier-1 tests in
`tests/Core/Plugins/ProjectRenderFeatureCallbackAfterEverythingTaggingTests.cpp`.

## Deviation from the instructions (and why)

Section 4's own "THE ONE REQUIRED SHADER REWRITE" paragraph (un-premultiplied
alpha, writing directly into `privateTarget`) directly contradicts Section
4a's own, later, more carefully-reasoned "NAMED OUTPUT PRESERVATION" decision
(keep the shader unchanged, blit a plain copy into `privateTarget` instead).
Applying the shader rewrite as literally described would have required the
shader to stop writing real, final pixel colors into `"GameViewComposited"` —
breaking the screenshot bridge, the Editor's own Game/Scene panels, and the
purity validator, which is exactly what Section 4a exists to prevent. The
operator was asked directly (`ask_questions`) and delegated the decision; I
chose Section 4a's direction (keep the shader unchanged, plain blit) because
it is mathematically provably equivalent to today's output for the one real
case that exists in this codebase (Atmosphere, pinned to run first), carries
zero risk to the three real, live consumers of `"GameViewComposited"`, and
matches Section 4a's own explicit "smallest blast radius, do not re-litigate"
reasoning. The un-premultiplied-alpha rewrite was not implemented.

## Files touched
- `src/Renderer/RenderGraph/RenderPipeline.h` — `DeclareOnePhase()` two-pass fix (Once-scope first).
- `src/Renderer/RenderGraph/RenderGraphBuilder.h/.cpp` — `KeepTextureOutput()`/`FinalTextureOutputs()`.
- `src/Core/Core.cpp` — merge site for `b.FinalTextureOutputs()`.
- `src/Core/Plugins/ProjectRenderFeatureCallback.h` — `FindPassesNotTaggedAfterEverything()`.
- `src/Core/Plugins/RenderFeatureCompositor.cpp` — wired the new tag check.
- `src/Features/Atmosphere/AtmosphereFeature.cpp` — both migrations.
- `src/Features/Atmosphere/AtmosphereLutRenderer.h/.cpp` — `sourceDepthHandle` split, `AfterEverything` tag.
- `src/Features/Atmosphere/AtmospherePassSequence.h/.cpp` — `AddAtmosphereCompositePass()` now takes raw color/depth handles+samplers instead of `RenderTexture&`.
- `tests/Core/Plugins/ProjectRenderFeatureCallbackAfterEverythingTaggingTests.cpp` — new, 5 tests.
- `tests/CMakeLists.txt` — registers the new test file.

## Verification performed
1. **Full build** (`cmake --build build`) — `gte_core`, `gte_editor`,
   `GreatTamanaEditor.exe`, all four `Projects/*` sample assemblies, and
   `GreatTamanaEngineTests.exe` all compile clean, zero errors/warnings
   introduced.
2. **Full existing automated suite**: `ctest -C Debug` — **2330/2330 tests
   pass** (the ~190 "Skipped" entries are pre-existing Tier-2 GPU-device
   tests, skipped on this headless machine before this change too — same
   skip set as baseline). Zero regressions.
3. **New Tier-1 tests** (`FindPassesNotTaggedAfterEverythingTest`, 5 cases) —
   pass.
4. **Live engine verification** (`GreatTamanaEditor.exe` launched, queried via
   the network bridge):
   - `GET /get_game_view` and `GET /get_texture?texture_name=GameView` /
     `?texture_name=GameViewComposited` all return 200 with real, decodable
     PNGs — `"GameView"` and `"GameViewComposited"` are still two genuinely
     separate, independently resolvable resources.
   - `GET /render_graph` shows, live, in the real running frame:
     `AtmosphereTransmittanceLutPass`/`AtmosphereMultiScatteringLutPass`
     (Once-scope, unmigrated SharedLut) running and publishing successfully;
     `AtmosphereSkyViewLutPass`/`AtmosphereAerialPerspectiveVolumePass`
     (migrated ViewLut, via `AddPreOpaquePass()`) successfully consuming that
     published data — proving the `DeclareOnePhase()` ordering fix actually
     works, not just compiles; `AtmosphereComposite.CopyToPrivateTarget`
     (`Blit`, reads `GameViewComposited`, writes
     `ProjectFeatureSlot15_Game_Private`, tagged `AfterEverything`) followed
     by `ProjectFeatureSlot15_Game_Blend` reading both the seed and the
     private target — the full generic blend chain engaged correctly.
   - The same `GET /render_graph` response's `render_features` array:
     `{"name":"AtmosphereComposite","stage":"PostComposite","blend_mode":"AlphaOver","priority":-1000,"is_project_feature":true}`
     and `{"name":"AtmosphereViewLut","stage":"PreOpaque","priority":0,"is_project_feature":true}`.
   - `GET /get_logs?min_level=warning` — **zero warnings or errors** across
     ~5000 live frames (confirms no assert fired, no depth-read safety net
     violation, no `AfterEverything` tag violation, no priority-collision
     warning crash).

## Honest open items (not required by this migration, flagged for a future session)
- `AtmosphereSharedLut`/`DrawSkyBackground` remain permanent, intentional
  exceptions — this is the correct, final shape per the source instructions,
  not a shortfall.
- The `RenderPipeline::DeclareOnePhase()` two-pass ordering fix is a generic
  engine-level correctness fix, not scoped narrowly to Atmosphere — any
  future feature module registering both a `ProviderScope::Once` producer
  and a `ProviderScope::PerActiveView` consumer of its own data benefits from
  it automatically.
- Found, but did **not** touch: several files under `src/Features/Atmosphere/`,
  `src/Editor/`, and a few test files already had unrelated, uncommitted,
  pre-existing local modifications in this working tree before this task
  started (atmosphere math/type changes unrelated to this migration). They
  were deliberately left untouched and unstaged — they are not part of this
  commit.
