# PHASE4 — `RenderFeatureCompositor` Core: Ordering, Private Targets, Collision Detection

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it FIRST — especially Locked
Design Decisions #1, #2, #5, #9, #10). Also read `PHASE1`/`PHASE2`/`PHASE3`
completion reports before starting. This is the single largest, riskiest
phase in this campaign — if genuine ambiguity comes up while implementing,
use `ask_questions` rather than guessing; do not be shy about it.

## Step 1: The Goal

Ship the real, working `_v2` compositing pipeline: every loaded
`IRenderFeatureModule_v2` plugin gets its OWN private offscreen render
target, in explicit author-declared `stage`+`priority` order, with loud
collision detection — but with the actual PIXEL BLEND between plugins
TEMPORARILY hardcoded to a single, simple, provably-correct behavior
(Step 3.6 below explains exactly which one and why) so this phase's own
scope stays "prove the wiring/ordering/private-targets," leaving "prove
all 5 real blend modes" to PHASE5. By the end of this phase, ONE throwaway
test `_v2` plugin, loaded alongside the 4 existing demo plugins, proves the
whole pipeline compiles, loads, and renders correctly end-to-end.

## Step 2: The Situation

Confirmed by direct read, `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`'s
`AddAerialPerspectiveCompositePass()` (lines ~748-854) and its private
per-output-name state helper (`EnsureAerialPerspectiveCompositeViewInitialized()`,
lines ~725-746) — this is the EXACT, real, working precedent this phase's
own compositor mirrors:

- A persistent, named `RenderTexture`, one per distinct output name, created
  ONCE via `renderer.CreateRenderTexture(width, height,
  VK_FORMAT_R8G8B8A8_UNORM, outputTextureName, /*depthDebugName=*/nullptr,
  /*allowStorageImageAccess=*/true)`, resized (with a `vkDeviceWaitIdle()`
  stall — rare, resize-driven only) when the view's extent changes.
- Imported fresh into the graph EVERY frame via
  `builder.ImportTexture(outputTextureName, viewState.output->Target(),
  VK_IMAGE_LAYOUT_UNDEFINED)`.
- A `ComputeDescriptorSet`, allocated ONCE **per distinct view/output name**
  (`AerialPerspectiveCompositeViewState`/`SkyViewLutViewState`/
  `AerialPerspectiveVolumeViewState` each keep their OWN descriptor set,
  keyed by `outputTextureName`/`outputVolumeName` in a
  `std::unordered_map`) — never one descriptor set shared across two
  distinct views/outputs that might both be declared inside the SAME
  frame. `Rewrite()`-ed fresh every dispatch with
  `ComputeDescriptorWrite::CombinedImageSampler(binding, view, sampler)` /
  `ComputeDescriptorWrite::StorageImage(binding, view)`.
- A push-constant struct, `renderer.BeginGraphPassRecording(ctx.cmd,
  ctx.recordDraw); renderer.Dispatch(pipeline, descriptorSet.Native(),
  &pushConstants, sizeof(pushConstants), groupX, groupY, groupZ);
  renderer.EndGraphPassRecording();`, group counts from
  `ComputeGroupCount3D()` (`src/Renderer/ComputeDispatch.h`).
- The pass itself: `builder.AddRenderPass(name, rg::PassKind::Compute,
  viewScope, rg::RenderPassCategory::General, [setup: pass.ReadTexture(...),
  pass.WriteTexture(handle, rg::ResourceAccess::ComputeShaderWrite)],
  [execute: resolve handles via ctx.resolveTexture(), rewrite descriptor
  set, dispatch], rg::RenderPassDrawKind::DrawMesh,
  rg::RenderPassEvent::AfterTransparents)`.

**Two additional, load-bearing constraints, confirmed by direct read, that
Step 3.4's design below must satisfy:**

1. **A transient `RenderGraphBuilder::CreateTexture()` handle is NEVER
   storage-image-capable in this engine, full stop.**
   `RenderGraphResourcePool::AcquireTexture()`
   (`src/Renderer/RenderGraph/RenderGraphResourcePool.cpp`) creates every
   pooled/transient texture via `m_renderer->CreateRenderTexture(width,
   height, desc.format, debugName, nullptr)` — i.e. with
   `allowStorageImageAccess` left at `Renderer::CreateRenderTexture()`'s own
   default, `false`. `Renderer::CreateRenderTexture()`'s own doc comment
   (`Renderer.h`) states this explicitly, as a hard rule, not a
   coincidence: *"every storage-capable RenderTexture in this campaign is
   externally-owned/persistent (imported into a render graph via
   ImportTexture()) — never requested as a transient, render-graph-pooled
   resource."* `rg::TextureDesc` (`RenderGraphTypes.h`) has no
   storage-access field at all (`width`, `height`, `format`, `hasDepth`
   only), so there is no way to request one through `CreateTexture()`
   either. Consequently, a plugin's own private target, and every
   intermediate accumulator between two plugins/stages, MUST be a
   persistent, `renderer.CreateRenderTexture(..., allowStorageImageAccess=true)`-created
   `RenderTexture`, imported fresh every frame via `builder.ImportTexture()`
   — exactly like `AtmosphereLutRenderer`'s own per-view outputs above —
   and NEVER a `frame.builder.CreateTexture()` transient handle (Step 3.4
   below is written entirely in terms of `ImportTexture()`+persistent
   `RenderTexture`s for exactly this reason).
2. **One `ComputeDescriptorSet` object must never be `Rewrite()`-ed and
   dispatched against more than once per frame for two DIFFERENT physical
   resources.** Vulkan descriptor-set contents are consumed by the GPU at
   the point each bound dispatch actually executes, not snapshotted at
   `vkCmdBindDescriptorSets`-record time; with no
   `VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT` anywhere in this
   engine (confirmed: no hit for `UPDATE_AFTER_BIND` anywhere under
   `src/Renderer/`), re-`Rewrite()`-ing and re-dispatching the SAME
   descriptor set object for a second plugin (or a second view) within the
   same frame's command buffer, before the first dispatch's use of it has
   even been submitted, would silently make BOTH dispatches execute
   against whichever binding was written LAST — this is exactly why
   `AtmosphereLutRenderer` gives `SkyViewLutViewState`/
   `AerialPerspectiveVolumeViewState`/`AerialPerspectiveCompositeViewState`
   each their OWN `ComputeDescriptorSet`, keyed per view/output name,
   rather than one shared instance reused across Game View and Scene View
   within the same frame. `GpuResourceFactory::AllocateComputeDescriptorSet()`'s
   own doc comment additionally confirms a set allocated from
   `m_computeDescriptorPool` is "NEVER individually freed" — so the
   opposite approach (allocating a brand-new descriptor set on every single
   dispatch, every frame, forever) is also wrong; it would exhaust that
   fixed-size pool. The correct, established pattern is: allocate ONE
   descriptor set per distinct (plugin, view) — or (view) alone, for the
   seed step below — ONCE, the first time it is needed, and reuse +
   `Rewrite()` that SAME object every subsequent frame (Step 3.4 below
   keys every descriptor set this way — per-(plugin, view) — mirroring
   `AtmosphereLutRenderer`'s own precedent one level further).

Confirmed, `src/Core/Core.cpp` — `GpuDrivenBatchNamePool` (anonymous
namespace, lines ~143-189): the exact, real, existing precedent for
"interning stable `const char*` names for a variable set of dynamically-
discovered things, once, never re-derived or reallocated across frames" —
a `std::deque<std::string> m_storage` (never invalidates an already-handed-
out `c_str()` pointer on further insertion) plus a small lookup-or-create
API. This phase's own per-plugin/per-view/per-role name interning mirrors
this exact shape, and is sound exactly BECAUSE it produces the kind of
stable, static-storage-duration-for-the-rest-of-the-process `const char*`
both `RenderGraphBuilder::CreateTexture()`'s own doc comment and
`RenderGraphBuilder::ImportTexture()`'s own doc comment require (`name`
"must be a string literal (or otherwise static-storage-duration) const
char*"). This design is sound regardless of which render-graph builder
method ultimately consumes an interned name — every private/accumulator/
seed target below is imported via `builder.ImportTexture()`, which needs
exactly the same stability guarantee.

Confirmed, `src/Renderer/RenderGraph/RenderGraphTypes.h`: `TextureDesc`'s
exact, full shape is:

```cpp
struct TextureDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED; // VK_FORMAT_UNDEFINED == match Renderer::ColorFormat()
    bool hasDepth = false;
};
```

(No field for storage-image access exists — see the first new evidence
point above for why this matters: `TextureDesc`/`CreateTexture()` are
simply the wrong tool for this phase's private/accumulator targets.)

Confirmed, PHASE0's Locked Design Decision #10: the VERY LAST write of the
WHOLE per-view compositing pipeline (the last plugin, in the last stage
that ran, this frame) must write directly back into the SAME `pluginTarget`
handle the legacy `_v1` path already writes into and every existing
downstream consumer (`GET /get_game_view`, the swapchain Present blit)
already reads — never a brand-new, separately-tracked "final image" handle.

Confirmed, `search_in_dir` for `RenderViewId::Named(` across `src/`: exactly
two named views exist anywhere in this engine today, `"Game"`
(`RenderPassViewData.h`, `Core.cpp`) and `"Scene"` (same two files) — no
third. Step 3.4 below relies on this being exactly `{"Game", "Scene"}`, not
a guess to "confirm later."

Confirmed, `src/Core/Core.h`/`Core.cpp`: `Core::FindViewData(rg::RenderViewId)
const noexcept` is declared AFTER the `private:` label (line 245) at line
287 — it is a **private** method. This matters for Step 3.5/PHASE2's shared
accessor below (see that step's note on `Core::FindPluginRenderFeatureTarget()`).

## Step 3: The Plan

### Step 3.1 — New shader: `src/Shaders/RenderFeatureOps.comp`

One "uber" compute shader implementing all 3 fixed drawing operations
(PHASE0 Locked Design Decision #5), `opCode` push-constant selects the
formula, writing the FULL result (every pixel, always) into this plugin's
own private storage image — mirrors `AtmosphereAerialPerspectiveComposite.comp`'s
own `layout(local_size_x = 16, local_size_y = 16) in;` / `imageStore(...)`
shape exactly.

```glsl
#version 450

// editor-core-separation-6 campaign, PHASE4
// (PHASE4_RENDER_FEATURE_COMPOSITOR_CORE_AND_ORDERING.md) - the 3 fixed
// IPluginRenderPassBuilder_v2 drawing operations (RenderFeatureDescriptor.h
// / IPluginRenderPassBuilder_v2.h, PHASE1), each ALWAYS writing a value at
// EVERY pixel of this plugin's own PRIVATE target (never a partial-coverage
// write - no separate "clear to transparent first" pass is needed, see
// PHASE1's own IPluginRenderPassBuilder_v2.h doc comment).
//
// opCode 0 = Solid Fill: constant color at every pixel.
// opCode 1 = Radial Vignette: normalized center/inner/outer radius falloff;
//            alpha naturally reaches 0 outside outerRadius.
// opCode 2 = Color Grade: brightness/contrast/saturation/tint applied to
//            whatever is ALREADY in this plugin's own private target before
//            this dispatch (seeded fully transparent/black - a color grade
//            with nothing underneath it is a fixed, well-defined neutral
//            gray-ish result; a _v2 plugin wanting a REAL grade-of-the-
//            scene effect combines this with AddSolidFillPass in the same
//            AddRenderGraphPasses() call, in that order, since ops within
//            one plugin's own private target compose top-to-bottom in
//            call order - document this plainly for plugin authors, PHASE1's
//            own IPluginRenderPassBuilder_v2.h doc comment references this).
layout(local_size_x = 16, local_size_y = 16) in;

layout(push_constant) uniform PushConstants {
    // .x = opCode (as a float, cast to int in-shader - keeps ONE uniform
    // push-constant block shape for all 3 ops, matching this campaign's
    // "one uber shader" decision, PHASE0_MASTER_STRATEGY.md).
    vec4 opCodeAndPad;
    vec4 colorRgba;           // solid fill color / vignette color / tint color
    vec4 centerAndRadius;     // vignette: centerX, centerY, innerRadius, outerRadius
    vec4 gradeParams;         // color grade: brightness, contrast, saturation, tintStrength
} pc;

layout(binding = 0, rgba8) uniform image2D privateTarget;

vec3 ApplyColorGrade(vec3 color, float brightness, float contrast, float saturation, vec3 tint, float tintStrength)
{
    vec3 graded = color * brightness;
    graded = (graded - 0.5) * contrast + 0.5;
    float luma = dot(graded, vec3(0.2126, 0.7152, 0.0722));
    graded = mix(vec3(luma), graded, saturation);
    graded = mix(graded, tint, tintStrength);
    return graded;
}

void main()
{
    ivec2 size = imageSize(privateTarget);
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    if (texel.x >= size.x || texel.y >= size.y) {
        return;
    }

    int opCode = int(pc.opCodeAndPad.x);
    vec2 uv = (vec2(texel) + vec2(0.5)) / vec2(size);

    if (opCode == 0) {
        imageStore(privateTarget, texel, pc.colorRgba);
    } else if (opCode == 1) {
        vec2 aspectCorrectedUv = uv;
        aspectCorrectedUv.x *= float(size.x) / float(size.y);
        vec2 aspectCorrectedCenter = pc.centerAndRadius.xy;
        aspectCorrectedCenter.x *= float(size.x) / float(size.y);
        float dist = distance(aspectCorrectedUv, aspectCorrectedCenter);
        float innerR = pc.centerAndRadius.z;
        float outerR = pc.centerAndRadius.w;
        float falloff = 1.0 - clamp((dist - innerR) / max(outerR - innerR, 1e-5), 0.0, 1.0);
        imageStore(privateTarget, texel, vec4(pc.colorRgba.rgb, pc.colorRgba.a * falloff));
    } else {
        vec4 existing = imageLoad(privateTarget, texel);
        vec3 graded = ApplyColorGrade(existing.rgb, pc.gradeParams.x, pc.gradeParams.y, pc.gradeParams.z,
            pc.colorRgba.rgb, pc.gradeParams.w);
        imageStore(privateTarget, texel, vec4(graded, existing.a));
    }
}
```

The C++-side push-constant struct mirroring the GLSL `PushConstants` block
above byte-for-byte (4 `vec4`s, 64 bytes total, no padding) — declared in
`src/Core/Plugins/RenderFeatureCompositor.h` (used by both
`RenderFeatureCompositor::DispatchOps()` and `PluginRenderPassBuilderAdapter_v2`,
Step 3.2 below), mirroring `AerialPerspectiveCompositePushConstants`'s own
plain-`float[]`-members shape exactly (`AtmosphereLutRenderer.h`):

```cpp
struct RenderFeatureOpsPushConstants {
    float opCodeAndPad[4] = {};       // .x = opCode (0=SolidFill, 1=RadialVignette, 2=ColorGrade)
    float colorRgba[4] = {};          // solid fill color / vignette color / tint color
    float centerAndRadius[4] = {};    // vignette: centerX, centerY, innerRadius, outerRadius
    float gradeParams[4] = {};        // color grade: brightness, contrast, saturation, tintStrength
};
```

Register in the root `CMakeLists.txt`: `gte_add_shader(GreatTamanaEditor
src/Shaders/RenderFeatureOps.comp)` — find the exact existing `.comp` entry
style via `search_in_dir` for `.comp` in `CMakeLists.txt` and insert
alongside them.

### Step 3.2 — New files: `src/Core/Plugins/PluginRenderPassBuilderAdapter_v2.h` + `.cpp`

Implements `IPluginRenderPassBuilder_v2` (PHASE1). Constructed fresh per
plugin-per-view-per-frame (mirrors `PluginRenderPassBuilderAdapter`'s own
existing per-call construction shape exactly:
`PluginRenderPassBuilderAdapter(rg::RenderGraphBuilder&, rg::TextureHandle)`),
holding:

```cpp
class PluginRenderPassBuilderAdapter_v2 final : public IPluginRenderPassBuilder_v2 {
public:
    PluginRenderPassBuilderAdapter_v2(rg::RenderGraphBuilder& builder, rg::TextureHandle privateTarget,
        RenderFeatureCompositor& compositor, const char* privateTargetStateKey) noexcept;

    void AddSolidFillPass(const char* debugName, float r, float g, float b, float a) override;
    void AddRadialVignettePass(const char* debugName, float centerX, float centerY, float innerRadius,
        float outerRadius, float r, float g, float b, float a) override;
    void AddColorGradePass(const char* debugName, float brightness, float contrast, float saturation,
        float tintR, float tintG, float tintB, float tintStrength) override;

private:
    rg::RenderGraphBuilder& m_builder;
    rg::TextureHandle m_privateTarget;
    RenderFeatureCompositor& m_compositor;
    const char* m_privateTargetStateKey; // the SAME interned name RenderFeatureNamePool handed out for
                                         // this (plugin, view) pair's private-target slot - see Step 3.4.
};
```

Every one of the 3 methods above does the SAME thing, differing only in
which `opCode` (0/1/2) and which push-constant fields it fills: it calls
ONE shared helper the compositor exposes for exactly this purpose,
`RenderFeatureCompositor::DispatchOps(rg::RenderGraphBuilder& builder,
rg::TextureHandle privateTarget, const char* stateKey, const char*
debugName, const RenderFeatureOpsPushConstants& pushConstants)`, which:

1. Calls `EnsureOpsInitialized(m_renderer)` (lazy, once, guarded by
   `m_opsPipeline.has_value()` — mirrors
   `AtmosphereLutRenderer::EnsureAerialPerspectiveCompositeInitialized()`'s
   own exact pattern) to build `m_opsPipeline`/`m_opsDescriptorSetLayout`
   the first time ANY plugin's ops call happens this session.
2. Looks up (or lazily allocates, first use, via
   `renderer.AllocateComputeDescriptorSet(m_opsDescriptorSetLayout)`) the
   ONE `ComputeDescriptorSet` belonging to `stateKey` inside
   `m_privateTargetStates` (Step 3.4) — this is the fix for the descriptor-
   set-reuse hazard documented in Step 2: every distinct (plugin, view)
   pair gets its OWN descriptor set, allocated once and reused/`Rewrite()`-ed
   every subsequent frame, never shared with any other plugin's or any
   other view's own ops dispatch.
3. Declares `builder.AddRenderPass(debugName, rg::PassKind::Compute,
   rg::ViewScope::Shared, rg::RenderPassCategory::General, [setup:
   pass.WriteTexture(privateTarget, rg::ResourceAccess::ComputeShaderWrite)],
   [execute: renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
   descriptorSet.Rewrite(device, {ComputeDescriptorWrite::StorageImage(0,
   ctx.resolveTexture(privateTarget).view)}); renderer.Dispatch(*m_opsPipeline,
   descriptorSet.Native(), &pushConstants, sizeof(pushConstants),
   ComputeGroupCount3D({width, height, 1}, {16, 16, 1})...);
   renderer.EndGraphPassRecording();], rg::RenderPassDrawKind::DrawMesh,
   rg::RenderPassEvent::AfterEverything)`. `ViewScope::Shared` is correct
   here even though the physical target differs per view, exactly like
   every existing per-view Atmosphere pass above uses its own distinct
   name/target rather than needing `ViewScope::GameView`/`SceneView` to
   disambiguate (the render graph only needs `ViewScope` for the LEGACY
   `TranslateLegacyViewScope()` bridge — see `RenderPassViewData.h` — which
   this NEW `_v2` pass has no dependency on at all).
4. `debugName` becomes this pass's own name (mirrors
   `PluginRenderPassBuilderAdapter::AddFullscreenClearPass`'s own use of its
   `debugName` parameter directly as the `AddRenderPass()` name).

Because the SAME `ComputeDescriptorSet` is reused for every op call a
SINGLE plugin makes within its own `AddRenderGraphPasses()` invocation this
frame (e.g. `AddSolidFillPass` then `AddColorGradePass` against the SAME
private target), re-`Rewrite()`-ing it before each of those calls is
harmless (the binding — `privateTarget`'s own image view — never actually
changes between them); it is only sharing this SAME object ACROSS two
DIFFERENT plugins or views that is unsafe (Step 2).

### Step 3.3 — New file: `src/Core/Plugins/RenderFeatureNamePool.h`

A small, direct, header-only adaptation of `Core.cpp`'s own
`GpuDrivenBatchNamePool` (Step 2 evidence) — interns stable
`const char*` names for `"<PluginName>_<ViewName>_Private"` (per-plugin,
per-view private target), `"<PluginName>_<ViewName>_Accum"` (per-plugin,
per-view accumulator-after-this-plugin), AND `"RenderFeatureCompositor_<ViewName>_Seed"`
(one per VIEW only, shared by whichever combined plugin list runs for that
view this frame — see Step 3.4's new seeding step), keyed by `(plugin
descriptor name, view name)` pairs (or just `view name` for the Seed
role), populated once at `RenderFeatureCompositor::OnPluginsLoaded()`
time for BOTH known views (`"Game"` and `"Scene"` — confirmed exactly
these two, Step 2) — the set of loaded plugins AND the set of views are
both fixed for the process's entire remaining lifetime (`PluginHost` never
unloads before process exit, hot reload is a permanent non-goal; no third
named view is ever introduced without an engine-wide `RenderViewId::Named()`
change) — never re-interned per-frame.

### Step 3.4 — New files: `src/Core/Plugins/RenderFeatureCompositor.h` + `.cpp`

The second real `IPluginCapabilityOrchestrator` implementation. **Same
circular-include hazard `LegacyRenderFeatureOrchestrator.h` documents
(PHASE2 Step 3.2) applies here too, for the identical reason** (`Core.h`
will hold a `std::unique_ptr<IPluginCapabilityOrchestrator>` vector that
constructs a `RenderFeatureCompositor` inside `Core.cpp`, so
`RenderFeatureCompositor.h` must NOT `#include "../Core.h"`): forward-declare
`class Core;` at the top of `RenderFeatureCompositor.h` (inside `namespace
gte { ... }`, mirroring `LegacyRenderFeatureOrchestrator.h`'s own identical
forward declaration exactly) and `#include "../Core.h"` only in
`RenderFeatureCompositor.cpp`, where the real `Core&`/`Renderer&` method
calls actually happen:

```cpp
class RenderFeatureCompositor final : public IPluginCapabilityOrchestrator {
public:
    explicit RenderFeatureCompositor(Core& core, Renderer& renderer);

    void OnPluginsLoaded(const std::vector<IPluginModule*>& modules) override;
    void ContributeRenderGraphPasses(
        const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out) override;

    // Called by PluginRenderPassBuilderAdapter_v2 (Step 3.2) - see that
    // step's own description of what this does.
    void DispatchOps(rg::RenderGraphBuilder& builder, rg::TextureHandle privateTarget, const char* stateKey,
        const char* debugName, const RenderFeatureOpsPushConstants& pushConstants);

private:
    struct Entry {
        IRenderFeatureModule_v2* module = nullptr;
        GtePluginRenderFeatureDescriptor descriptor{};
    };

    // Bundles exactly what one (plugin, view) private-target slot needs -
    // mirrors AtmosphereLutRenderer's own AerialPerspectiveCompositeViewState
    // shape (a persistent output RenderTexture PLUS its own dedicated
    // ComputeDescriptorSet, never shared with any other slot - see Step 2's
    // second new evidence point for exactly why the descriptor set must be
    // per-slot, not shared).
    struct PrivateTargetState {
        ComputeDescriptorSet opsDescriptorSet; // allocated lazily, first use.
        std::optional<RenderTexture> texture;  // created lazily, first use; resized in place on extent change.
    };

    // Bundles one blend-stage physical slot: either a per-(plugin, view)
    // ACCUMULATOR (the output of plugin i's blend, i < N-1, that becomes
    // plugin i+1's own "currentInput"), or the per-VIEW SEED (see below).
    // Same "persistent RenderTexture + its own dedicated ComputeDescriptorSet"
    // shape as PrivateTargetState, for the identical reason.
    struct BlendStageState {
        ComputeDescriptorSet blendDescriptorSet;
        std::optional<RenderTexture> texture;
    };

    RenderTexture& EnsureTargetInitialized(std::unordered_map<std::string, PrivateTargetState>::mapped_type& state,
        const char* internedName, VkExtent2D extent);
    // (Or one shared private helper taking either map's mapped_type by a small
    // common base/pair of fields - implementation detail; the important,
    // load-bearing CONTRACT is: create via renderer.CreateRenderTexture(width,
    // height, VK_FORMAT_R8G8B8A8_UNORM, internedName, /*depthDebugName=*/nullptr,
    // /*allowStorageImageAccess=*/true) the first time `internedName` is seen;
    // on every later call, compare `texture->Extent()` against the requested
    // `extent` and, if different, `vkDeviceWaitIdle(device)` then
    // `texture->Resize(width, height)` - EXACTLY mirroring
    // AtmosphereLutRenderer::AddAerialPerspectiveCompositePass()'s own
    // resize-on-extent-change block. This resize case is real and will be hit
    // in practice: the Game/Scene View panels are user-resizable.

    void EnsureOpsInitialized(Renderer& renderer);
    void EnsureBlendStubInitialized(Renderer& renderer); // Step 3.6; PHASE5 replaces this with EnsureBlendPipelineInitialized().

    Core& m_core;
    Renderer& m_renderer;
    std::vector<Entry> m_postComposite;  // sorted by priority ascending
    std::vector<Entry> m_preUi;          // sorted by priority ascending
    RenderFeatureNamePool m_namePool;

    std::optional<ComputePipeline> m_opsPipeline;
    VkDescriptorSetLayout m_opsDescriptorSetLayout = VK_NULL_HANDLE;
    std::optional<ComputePipeline> m_blendStubPipeline; // Step 3.6 - PHASE5 renames/replaces this.
    VkDescriptorSetLayout m_blendStubDescriptorSetLayout = VK_NULL_HANDLE;

    // Keyed by RenderFeatureNamePool's own interned "<Plugin>_<View>_Private" names.
    std::unordered_map<std::string, PrivateTargetState> m_privateTargetStates;
    // Keyed by RenderFeatureNamePool's own interned "<Plugin>_<View>_Accum" names
    // AND "RenderFeatureCompositor_<View>_Seed" names (same map, both roles -
    // they never collide, since interned names always carry either "_Accum"
    // or "_Seed" as an unambiguous suffix).
    std::unordered_map<std::string, BlendStageState> m_blendStageStates;
};
```

**`OnPluginsLoaded(modules)`:**

1. For each module answering `QueryCapability(kIRenderFeatureModule_v2_Name)`:
   call `GetRenderFeatureDescriptor()` once, snapshot it.
2. If `descriptor.stage` is `PreOpaque`/`PostOpaque`/`PostTransparent`
   (PHASE0 Locked Design Decision #1): log
   `GTE_LOG_WARNING("RenderFeatureCompositor", "<name> declared
   RenderFeatureStage::<stage> which is not wired in this engine build -
   this feature will not run any frame. See
   task_manager/editor-core-separation-6/PHASE0_MASTER_STRATEGY.md's
   Locked Design Decision #1.")`, and SKIP this module entirely (never add
   it to `m_postComposite`/`m_preUi`).
3. Otherwise append to `m_postComposite` or `m_preUi` per `descriptor.stage`.
4. Sort each of `m_postComposite`/`m_preUi` by `descriptor.priority`
   ascending. **Collision detection** (PHASE0 Locked Design Decision,
   Proposal Section 3.4 step 3): after sorting, scan for adjacent equal
   `priority` values within the SAME vector; for every such pair, log
   `GTE_LOG_WARNING("RenderFeatureCompositor", "<nameA> and <nameB> both
   declared priority <N> in stage <stage> - this is ambiguous; falling back
   to a stable, lexical name tie-break. Assign each plugin a distinct
   priority to remove this warning.")`, then re-sort that colliding pair by
   `std::strcmp(descriptor.name, descriptor.name)` (lexical) as the
   documented, stable, deterministic tie-break — never crash, never leave
   the order to `std::sort`'s own unspecified-for-equal-keys behavior.
5. Populate `m_namePool` for every surviving entry now (both stages), for
   BOTH known view names, `"Game"` and `"Scene"` (Step 2 confirms these are
   the only two `RenderViewId::Named()` values anywhere in this engine) —
   including the per-view `"RenderFeatureCompositor_<ViewName>_Seed"` name,
   even though at this point no `RenderTexture`/descriptor set exists yet
   for any of them (those are all created lazily, first use, in
   `ContributeRenderGraphPasses()` below, since that is the first point a
   live `Renderer&`/real view extent is available for this purpose in this
   flow — `Renderer& m_renderer` itself IS already available at
   `OnPluginsLoaded()` time, but the current view's real pixel extent is
   not, since no frame has been declared yet).

**`ContributeRenderGraphPasses(frame, out)`:**

1. `const std::optional<Core::PluginRenderFeatureTargetInfo> resolved =
   m_core.FindPluginRenderFeatureTarget(frame);` — see Step 3.5 below for
   this shared accessor's exact signature (it returns both the view's
   resolved target handle AND its current pixel extent, since this
   compositor — unlike `LegacyRenderFeatureOrchestrator` — needs the extent
   to size every persistent private/accumulator/seed `RenderTexture`
   below). If `!resolved.has_value()`, return (mirrors the existing
   `viewData == nullptr` guard).
2. Build ONE combined, ordered list: every `m_postComposite` entry (in
   sorted order), THEN every `m_preUi` entry (in sorted order) — this is
   the concrete realization of PHASE0's Locked Design Decision #2 (both
   "stages" are really one ordered sequence at this engine's one real hook
   point).
3. If the combined list is empty, do nothing (mirrors the existing
   `anyPluginFeatureRanThisView` guard — `LegacyRenderFeatureOrchestrator`
   already independently guards its own `_v1` case; this is this
   orchestrator's OWN separate, equally-necessary guard for the `_v2` case).
4. **Seed the chain** (this closes the same-physical-image read+write
   hazard noted in Step 2): look up/lazily-create this VIEW's own
   `BlendStageState` for `"RenderFeatureCompositor_<ViewName>_Seed"`
   (`EnsureTargetInitialized`, sized to `resolved->extent`), import it via
   `frame.builder.ImportTexture(seedName, seedTexture.Target(),
   VK_IMAGE_LAYOUT_UNDEFINED)`, then declare ONE compute pass that copies
   `resolved->target`'s CURRENT contents into it: dispatch
   `m_blendStubPipeline` (Step 3.6) with `srcIn = resolved->target` (bound
   as a combined-image-sampler, `ShaderRead`), `dstIn` bound to the SAME
   view/sampler (unused by a pure copy, but the layout needs a valid
   binding), `destinationImage = seedHandle` (storage write). Use the
   SEED'S OWN dedicated `BlendStageState::blendDescriptorSet` — never any
   plugin's own. `currentInput = seedHandle` (**never** `resolved->target`
   directly) from this point on. This one extra dispatch guarantees that,
   for the LAST entry in the combined list (`i == N - 1`, whose own
   `outputHandle` is `resolved->target` itself per Locked Design Decision
   #10), `currentInput` is NEVER also `resolved->target` — closing a real
   GPU hazard ("a compute pass reads AND writes the exact same storage
   image in one dispatch," forbidden by this engine's own convention, see
   PHASE5's own `RenderFeatureBlend.comp` doc comment) that Step 3.7's own
   verification (a combined list of exactly one plugin, `N == 1`) exercises
   directly: without this seeding step, `currentInput` and `outputHandle`
   would be the exact SAME handle in the exact SAME dispatch whenever
   `N == 1`.
5. For `i` in `0 .. N-1` (`N` = combined list size):
   - `entry = combinedList[i]`.
   - Look up (or lazily create/resize, `EnsureTargetInitialized`) this
     entry's OWN `PrivateTargetState` for `m_namePool.PrivateName(entry,
     viewName)`, sized to `resolved->extent`; `privateTarget =
     frame.builder.ImportTexture(privateName, privateTexture.Target(),
     VK_IMAGE_LAYOUT_UNDEFINED)` (always imported as `UNDEFINED` — this
     plugin's own ops dispatch(es) always fully overwrite every texel,
     mirroring every existing compute-write-target import in this engine).
   - `PluginRenderPassBuilderAdapter_v2 adapter(frame.builder, privateTarget,
     *this, privateName);`
   - `entry.module->AddRenderGraphPasses(adapter);`
   - `outputTarget`: if `i == N - 1`, `resolved->target` (Locked Design
     Decision #10 — the LAST plugin's blend writes back into the ORIGINAL
     handle); otherwise look up/create this entry's own `BlendStageState`
     for `m_namePool.AccumName(entry, viewName)` and import it the same way
     private targets are imported above.
   - Declare and dispatch ONE blend compute pass, using THIS entry's own
     dedicated `blendDescriptorSet` (never the seed's, never another
     entry's) — Step 3.6 below describes THIS phase's own temporary,
     simplified blend body; PHASE5 replaces its body with the real
     multi-mode `RenderFeatureBlend.comp` dispatch, changing NO
     caller-visible shape here: `pass.ReadTexture(currentInput, ShaderRead);
     pass.ReadTexture(privateTarget, ShaderRead); pass.WriteTexture(outputTarget,
     ComputeShaderWrite);` (note `currentInput != outputTarget` always
     holds now, by construction, for every `i` including `i == 0 == N - 1`
     — see point 4 above).
   - `currentInput = outputTarget;`
6. `frame.finalTextureOutputs.push_back(resolved->target);` — mirrors the
   legacy orchestrator's own identical final step; safe to call even if
   `LegacyRenderFeatureOrchestrator` ALSO pushed the exact same handle this
   same frame (a harmless duplicate entry in that vector — confirm via
   `read_file` on wherever `finalTextureOutputs` is consumed that a
   duplicate handle is genuinely harmless, e.g. deduplicated or simply
   iterated; use `ask_questions` if this looks unsafe once the real
   consumer code is in front of you).

**Caveat worth documenting (not fixed by this phase — a pre-existing, defensive-only
edge case inherited from `_v1`):** `resolved->target` can, in the
"`AtmosphereComposite` did not publish anything this frame" defensive
fallback case (see `Core::FindPluginRenderFeatureTarget()`, Step 3.5),
resolve to the RAW `viewData->colorTarget` rather than the composited
output — and that raw handle's own underlying `RenderTexture` is NOT
guaranteed to have been created with `allowStorageImageAccess = true` (it
is normally only ever written via `WriteColorAttachment()`, a graphics
clear, never a compute storage write). Writing the last plugin's blend
directly into it via `ComputeShaderWrite`/`imageStore` would then be
invalid Vulkan usage. This mirrors an already-accepted, "should never
normally happen" defensive branch the existing `_v1` path also silently
assumes never triggers in practice; call this out explicitly in the
completion report rather than silently hoping it never fires, but do not
otherwise design around it in this phase (mirrors this same fallback's
existing, pre-`_v2`, accepted risk level).

### Step 3.5 — Register it; the shared `Core::FindPluginRenderFeatureTarget()` accessor

`Core::RegisterBuiltinCapabilityOrchestrators()` gains a third line:

```cpp
m_capabilityOrchestrators.push_back(std::make_unique<RenderFeatureCompositor>(*this, m_renderer));
```

(Confirm `m_renderer`'s exact member name/type in `Core.h` — it is used
elsewhere in `Core.cpp`, e.g. `m_renderer` is already referenced by
`AddAtmosphereCompositePass(frame.builder, m_renderer, ...)` — reuse that
exact same member, never construct a second `Renderer`.)

**The shared `Core::FindPluginRenderFeatureTarget()` accessor (defined in
full by PHASE2 Step 3.3):** `Core::FindViewData()` is **private**
(confirmed, Step 2 above), so this accessor calls it INTERNALLY and
returns `std::nullopt` when the view isn't found — no caller outside
`Core.cpp`, including this compositor, ever touches `FindViewData()`
directly. This compositor also needs the view's current pixel extent (for
sizing every persistent private/accumulator/seed `RenderTexture` above),
which `LegacyRenderFeatureOrchestrator` (PHASE2) never needed — so the
accessor's return type bundles both the resolved target handle AND the
extent in one small struct, used identically by both orchestrators (each
simply ignores the field it doesn't need). **Reminder (see PHASE2 Step 3.3
for the authoritative version): `PluginRenderFeatureTargetInfo` is a PUBLIC
NESTED type of `class Core` itself, not a free-standing `namespace gte`
struct** — both declarations below belong inside `class Core { public: ...
};`'s own existing public section:

```cpp
struct PluginRenderFeatureTargetInfo {
    rg::TextureHandle target;
    VkExtent2D extent{};
};

// Core.h (public or a narrow friend-free accessor - no friend declaration
// needed anywhere, since this method itself calls the private
// FindViewData() from INSIDE Core, exactly like every other Core.cpp method
// already does).
std::optional<PluginRenderFeatureTargetInfo> FindPluginRenderFeatureTarget(
    const rg::RenderPassFrameContext& frame) const;
```

```cpp
// Core.cpp - ENTIRE body is exactly the same lines the current
// "PluginRenderFeatures" provider already computes today, plus the extent:
std::optional<Core::PluginRenderFeatureTargetInfo> Core::FindPluginRenderFeatureTarget(
    const rg::RenderPassFrameContext& frame) const
{
    const RenderPassViewData* viewData = FindViewData(frame.currentView);
    if (viewData == nullptr) {
        return std::nullopt;
    }
    const bool isGameView = (frame.currentView == rg::RenderViewId::Named("Game"));
    const rg::RenderPassId compositedKey = isGameView ? kGameCompositedOutputKey : kSceneCompositedOutputKey;
    PluginRenderFeatureTargetInfo info;
    info.target = frame.blackboard.Fetch<rg::TextureHandle>(compositedKey).value_or(viewData->colorTarget);
    info.extent = viewData->renderTexture != nullptr ? viewData->renderTexture->Extent() : VkExtent2D{};
    return info;
}
```

`LegacyRenderFeatureOrchestrator::ContributeRenderGraphPasses()` (PHASE2)
uses only `.target` and ignores `.extent`; its own body resolves via this
accessor, `return;`-s on `!has_value()`, otherwise loops
`m_pluginHost`'s (reached via `m_core.GetPluginHost()`, PHASE3's own
confirmed existing accessor) loaded modules exactly as today, using
`resolved->target` wherever the code used `pluginTarget` (see PHASE2
Step 3.2 for that class's own full body).

### Step 3.6 — THIS PHASE'S temporary, simplified blend (Replace-only), replaced in full by PHASE5

To keep this phase's own scope bounded to "prove ordering/private-targets/
collision-detection genuinely work," the blend compute pass this phase adds
does NOT yet read `descriptor.blendMode` at all — it always performs a
straightforward `Replace`-equivalent operation. Its bindings are
DELIBERATELY IDENTICAL in shape to PHASE5's own real, permanent
`RenderFeatureBlend.comp` (binding 0 = `dstIn`, a `sampler2D`; binding 1 =
`srcIn`, a `sampler2D`; binding 2 = `destinationImage`, a write-only
`rgba8` storage image) specifically so PHASE5's own claim — "replacing
Step 3.6's stub, changing NO caller-visible shape here" — is literally true
and requires zero descriptor-set-layout/binding-kind changes, only a
`.comp` file swap and a mode push-constant that this stub simply never
reads:

```glsl
#version 450

// editor-core-separation-6 campaign, PHASE4 - THROWAWAY, replaced in full
// by PHASE5's own RenderFeatureBlend.comp (same binding shape, on purpose -
// see PHASE4's own Step 3.6). "If this plugin's own private pixel has any
// alpha, use it as-is, otherwise pass the prior input through unchanged" -
// NOT a real alpha blend, deliberately, so this phase's own correctness is
// trivial to verify by eye with exactly one throwaway _v2 test plugin whose
// own single op fully covers the frame. ALSO used, in this exact same
// Replace-only form, for RenderFeatureCompositor's own per-view "seed"
// dispatch (Step 3.4, point 4) - srcIn = the view's real composited image,
// dstIn = unused, destinationImage = the seed target.
layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0) uniform sampler2D dstIn;
layout(binding = 1) uniform sampler2D srcIn;
layout(binding = 2, rgba8) uniform writeonly image2D destinationImage;

void main()
{
    ivec2 size = imageSize(destinationImage);
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    if (texel.x >= size.x || texel.y >= size.y) {
        return;
    }

    vec2 uv = (vec2(texel) + vec2(0.5)) / vec2(size);
    vec4 dst = texture(dstIn, uv);
    vec4 src = texture(srcIn, uv);
    imageStore(destinationImage, texel, src.a > 0.0 ? src : dst);
}
```

Name this shader file `src/Shaders/RenderFeatureBlendStub.comp` and this
phase's own temporary pipeline `m_blendStubPipeline` — PHASE5 DELETES this
stub file and `RenderFeatureCompositor`'s own `m_blendStubPipeline` member
entirely, replacing both with the real `RenderFeatureBlend.comp` uber
shader supporting all 5 `RenderFeatureBlendMode` values, reusing the SAME
per-(plugin, view)/per-view-seed `BlendStageState`/`PrivateTargetState`
descriptor-set instances this phase already allocates (PHASE5 only swaps
WHAT is dispatched against them, never introduces a new sharing pattern).
Register `gte_add_shader(GreatTamanaEditor src/Shaders/RenderFeatureBlendStub.comp)`
in the root `CMakeLists.txt` alongside `RenderFeatureOps.comp` (Step 3.1).
Document this stub's throwaway nature with an explicit code comment at its
own declaration site so nobody mistakes it for a real, permanent blend
implementation.

### Step 3.7 — One throwaway `_v2` test plugin, for THIS PHASE'S verification only

Create a temporary, NOT-committed-to-the-final-campaign-state plugin folder
(e.g. `plugins/_scratch_render_feature_v2_probe/`), with its own
`CMakeLists.txt` copied/adapted from `plugins/demo_render_feature/
CMakeLists.txt` (the closest real template — same `add_library(... SHARED
...)`/`target_link_libraries(... gte_plugin_abi)`/output-directory
shape every existing demo plugin already uses; `read_file` that file
first) implementing `IRenderFeatureModule_v2`, declaring `stage =
PostComposite, priority = 0, blendMode = Replace` (unused by the Step 3.6
stub, but still required to fill the descriptor), calling
`AddSolidFillPass("Probe_Fill", 0.0f, 1.0f, 0.0f, 1.0f)` (solid GREEN —
deliberately distinct from the existing `_v1` demo plugins' solid magenta,
so a live screenshot instantly shows whether THIS pipeline is the one
drawing). Register it temporarily in the root `CMakeLists.txt`
(`if(GTE_ENABLE_PLUGINS) add_subdirectory(plugins/
_scratch_render_feature_v2_probe) endif()`, alongside the existing 4
`add_subdirectory(plugins/...)` lines — `search_in_dir` for
`add_subdirectory(plugins/` to find them), verify (a plain `cmake --build
build` after adding this line triggers CMake's own automatic
reconfigure/regeneration, since Ninja's generated build already tracks
`CMakeLists.txt` itself as a build-system input — no separate manual
reconfigure step is needed), then DELETE the plugin folder (including its
own `CMakeLists.txt`) and the `add_subdirectory(...)` line entirely before
this phase's own final commit (`git_status` must show zero trace of it,
per Workflow Rule 10; a subsequent `cmake --build build` after deleting it
again triggers automatic reconfiguration and simply stops building it —
confirm the probe's compiled `.dll`/`.pdb` leftovers under `build/`, if
any, are also gone or already git-ignored before the final `git_status`
check) — PHASE6 adds the REAL, permanent, committed `_v2` demo plugins;
this phase's own probe exists only to prove the compositor compiles and
draws correctly before that phase's own larger, 2-plugin proof.

### Step 3.8 — CMake wiring for the new `gte_core` files

Add the following NEW files to `gte_core`'s own source-file list in the
root `CMakeLists.txt` — mirror exactly how `PluginRenderPassBuilderAdapter.h`/
`.cpp` and `PluginRenderFeatureDiagnostics.h`/`.cpp` are already listed
there (`search_in_dir` for `PluginRenderFeatureDiagnostics.cpp` in
`CMakeLists.txt` to find the exact list/style, then insert these
alongside them, same relative style):

- `src/Core/Plugins/PluginRenderPassBuilderAdapter_v2.h`
- `src/Core/Plugins/PluginRenderPassBuilderAdapter_v2.cpp`
- `src/Core/Plugins/RenderFeatureNamePool.h` (header-only — still needs a
  source-list entry so it shows up in generated-project file listings,
  mirroring how every other header-only-but-listed file under
  `src/Core/Plugins/` is handled; confirm via `search_in_dir` for an
  existing header-only precedent's exact listing style before adding this
  one — if none exists, a plain header-only `.h` entry with no matching
  `.cpp` is still valid in this repo's CMake source lists).
- `src/Core/Plugins/RenderFeatureCompositor.h`
- `src/Core/Plugins/RenderFeatureCompositor.cpp`

Two new shader files also need `gte_add_shader(GreatTamanaEditor ...)`
registration (Step 3.1 and Step 3.6 above each already call this out at
their own point of introduction — `RenderFeatureOps.comp` and
`RenderFeatureBlendStub.comp` — this step exists so neither is missed
during a real implementation pass; there is no third shader file this
phase adds).

### Verification

1. Incremental build: `cmake --build build`.
2. Live smoke test with the throwaway probe plugin loaded: `run_app_background`,
   `GET /get_game_view` — confirm solid GREEN is now visible (this phase's
   own compositor pipeline is what produced it, not the pre-existing
   magenta `_v1` path — both are loaded simultaneously, PHASE0 Locked
   Design Decision #9, so seeing green proves `_v2`'s own last-write
   genuinely reached the same final handle). `GET /get_logs?limit=100` —
   confirm no unexpected warning (a single, unique `priority`/`stage`, so
   no collision/unwired-stage warning should fire for this probe) AND no
   Vulkan validation-layer error text about descriptor sets or storage-image
   usage (this would be the concrete, observable symptom of a descriptor-set-
   sharing or non-storage-transient-texture mistake — see Step 2's two
   load-bearing constraints). `stop_app_background` afterward.
3. Delete the probe plugin, rebuild, confirm `GET /get_game_view` reverts to
   the pre-existing magenta baseline (proves this phase's own new code
   path is correctly inert with zero `_v2` plugins loaded).
4. `git_status` — confirm the probe plugin leaves NO trace in the final
   diff.

### What this phase does NOT do

- Does not implement real, multi-mode blending (`RenderFeatureBlend.comp`,
  all 5 modes) — PHASE5.
- Does not wire the `PreUi`-after-`PostComposite` two-sub-stage ordering
  test with 2+ real plugins — PHASE5 (this phase's own single throwaway
  probe only exercises the `N == 1` case, which Step 3.4's seeding step
  makes a genuinely safe, correctly-verified case rather than a hidden GPU
  hazard).
- Does not add any permanent, committed demo plugin — PHASE6.
- Does not touch the Render Graph panel — PHASE7.

### Completion

Write `PHASE4_COMPLETION_REPORT.md` (exact `GET /get_game_view` screenshot
evidence via `load_image`/`gte_send_request`, confirming solid green with
the probe loaded and magenta reverting once removed), then `git_add` +
`git_commit`.
