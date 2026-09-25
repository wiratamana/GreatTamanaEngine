# PHASE5 — Real Blend-Mode Compute Shader + `PostComposite`→`PreUI` Sub-Stage Ordering

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it FIRST). Also read
`PHASE4_COMPLETION_REPORT.md` before starting — this phase directly extends
`RenderFeatureCompositor`, it does not restructure it.

## Step 1: The Goal

Replace PHASE4's throwaway `RenderFeatureBlendStub.comp`/`m_blendStubPipeline`
with the REAL, permanent, 5-mode `RenderFeatureBlend.comp` uber blend shader
(Replace / AlphaOver / Additive / Multiply / ScreenSpaceMask — PHASE0 Locked
Design Decision #5), and prove the `PostComposite`-then-`PreUI` two-sub-stage
ordering (PHASE0 Locked Design Decision #2) genuinely runs in the right
sequence with 2 differently-configured throwaway test plugins. By the end
of this phase, `RenderFeatureCompositor` is functionally complete; PHASE6
only adds the permanent, committed demo plugins and the real proof
artifact.

## Step 2: The Situation

`RenderFeatureCompositor::ContributeRenderGraphPasses()` (PHASE4, Step 3.4)
already builds the combined `PostComposite`-then-`PreUI` ordered list and
already declares one blend compute pass per plugin, currently dispatching
`RenderFeatureBlendStub.comp`'s trivial "alpha>0 ? src : dst" rule
regardless of `descriptor.blendMode`. `GtePluginRenderFeatureDescriptor::
blendMode` (PHASE1) is already populated and available at this call site —
it is simply not read yet.

## Step 3: The Plan

### Step 3.1 — New shader: `src/Shaders/RenderFeatureBlend.comp` (replaces `RenderFeatureBlendStub.comp`, which this phase DELETES)

```glsl
#version 450

// editor-core-separation-6 campaign, PHASE5
// (PHASE5_BLEND_MODE_COMPUTE_SHADER_AND_PREUI_STAGE.md) - the real,
// permanent, 5-mode blend uber-shader, replacing PHASE4's own throwaway
// RenderFeatureBlendStub.comp. binding 0 = dstIn (the accumulator/scene
// color BEFORE this plugin, i.e. RenderFeatureCompositor's own
// `currentInput`); binding 1 = srcIn (this plugin's own PRIVATE target,
// straight, non-premultiplied alpha); binding 2 = destinationImage
// (rgba8, write-only - RenderFeatureCompositor's own `outputTarget`,
// NEVER the same physical resource as binding 0, mirroring this codebase's
// existing "a compute pass never reads and writes the exact SAME storage
// image in the same dispatch" convention, ComputeBlurValidation.h).
layout(local_size_x = 16, local_size_y = 16) in;

layout(push_constant) uniform PushConstants {
    // .x = blendMode (RenderFeatureBlendMode, as a float cast to int).
    vec4 blendModeAndPad;
} pc;

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
    int mode = int(pc.blendModeAndPad.x);

    vec3 result;
    if (mode == 0) {            // Replace
        result = src.rgb;
    } else if (mode == 1) {     // AlphaOver ("src over dst")
        result = mix(dst.rgb, src.rgb, src.a);
    } else if (mode == 2) {     // Additive
        result = dst.rgb + src.rgb * src.a;
    } else if (mode == 3) {     // Multiply
        result = mix(dst.rgb, dst.rgb * src.rgb, src.a);
    } else {                    // ScreenSpaceMask - dst blended only where src.a > 0
        result = src.a > 0.0 ? src.rgb : dst.rgb;
    }

    imageStore(destinationImage, texel, vec4(result, 1.0));
}
```

Register via `gte_add_shader(GreatTamanaEditor src/Shaders/RenderFeatureBlend.comp)`
in the root `CMakeLists.txt`; DELETE the `gte_add_shader(GreatTamanaEditor
src/Shaders/RenderFeatureBlendStub.comp)` line PHASE4 added, and delete the
`src/Shaders/RenderFeatureBlendStub.comp` file itself.

The C++-side push-constant struct mirroring the GLSL block above (1 `vec4`,
16 bytes) — replaces whatever throwaway push-constant type PHASE4's own
`RenderFeatureBlendStub.comp` dispatch used, declared in
`src/Core/Plugins/RenderFeatureCompositor.h` alongside
`RenderFeatureOpsPushConstants` (PHASE4 Step 3.1):

```cpp
struct RenderFeatureBlendPushConstants {
    float blendModeAndPad[4] = {}; // .x = RenderFeatureBlendMode, as a float cast to int in-shader
};
```

### Step 3.2 — `RenderFeatureCompositor` changes

- Remove `m_blendStubPipeline`/its descriptor-set-layout entirely; add
  `std::optional<ComputePipeline> m_blendPipeline` + its own descriptor-set
  layout (2 combined-image-samplers + 1 storage image — mirrors
  `AtmosphereLutRenderer`'s own aerial-perspective-composite layout shape,
  binding count and kinds match Step 3.1's shader exactly), created lazily
  via a new `EnsureBlendPipelineInitialized(Renderer&)`, mirroring
  `EnsureOpsInitialized()`'s own existing pattern from PHASE4.
- In `ContributeRenderGraphPasses()`'s per-plugin loop (PHASE4 Step 3.4,
  point 5's blend-pass declaration): the push-constant now carries
  `entry.descriptor.blendMode` (cast to `float` for `blendModeAndPad.x`,
  matching the shader's own `int(pc.blendModeAndPad.x)` cast) instead of
  nothing; the descriptor-set rewrite binds `currentInput` (binding 0),
  `privateTarget` (binding 1), `outputTarget` (binding 2, storage image) —
  confirm exact binding-to-role mapping matches Step 3.1's shader source
  precisely (binding 0 = dst/prior, binding 1 = src/this-plugin, binding 2
  = output) before wiring the `ComputeDescriptorWrite` calls.
- No other structural change to the ordering/collision-detection/private-
  target logic PHASE4 already built — this phase only swaps WHAT the blend
  pass computes, not HOW plugins are discovered, sorted, or targeted. In
  particular, every descriptor set stays keyed exactly the way PHASE4
  built it — one `blendDescriptorSet` per (plugin, view) `BlendStageState`
  entry (accumulator role) plus one per-view `BlendStageState` entry (seed
  role), never a single shared instance — this phase only changes WHICH
  pipeline those existing, already-correctly-scoped descriptor sets are
  rewritten/dispatched against.
- PHASE4's per-view "seed" dispatch (`RenderFeatureCompositor::
  ContributeRenderGraphPasses()`, point 4 — copies the view's current
  composited image into that view's own seed target before the per-plugin
  loop starts) keeps dispatching against `m_blendPipeline` (renamed from
  `m_blendStubPipeline`) with its push constant's `blendModeAndPad.x`
  explicitly set to `0` (`RenderFeatureBlendMode::Replace`) — a plain copy,
  regardless of any individual plugin's own declared `blendMode` — using
  its own dedicated seed `BlendStageState::blendDescriptorSet`, never any
  plugin's.

### Step 3.3 — Two throwaway test plugins proving `PostComposite`→`PreUI` ordering, for THIS PHASE'S verification only (not committed — mirrors PHASE4's own scratch-probe discipline)

1. `plugins/_scratch_render_feature_v2_stage_a/` — `stage = PostComposite,
   priority = 0, blendMode = Replace`, `AddSolidFillPass("StageA_Fill", 1.0f,
   0.0f, 0.0f, 1.0f)` (solid RED, fully opaque, fully covers the frame).
2. `plugins/_scratch_render_feature_v2_stage_b/` — `stage = PreUI, priority
   = 0, blendMode = AlphaOver`, `AddRadialVignettePass("StageB_Vignette",
   0.5f, 0.5f, 0.1f, 0.6f, 0.0f, 0.0f, 1.0f, 1.0f)` (a BLUE vignette,
   `AlphaOver`, centered, falling off toward the edges).

**Expected, hand-computed result** (confirm via a live `GET /get_game_view`
screenshot and `load_image`): the CENTER of the frame should read solid
BLUE (opaque red from stage A, `AlphaOver`-blended with fully-opaque blue
from stage B's vignette center, `src.a == 1.0` there → `mix(dst, src, 1.0)
== src == blue`); the EDGES/corners should read solid RED (stage A's fill,
unmodified — stage B's vignette alpha has fallen to 0.0 there, so
`mix(dst, src, 0.0) == dst == red`); a soft red-to-blue gradient ring in
between. This proves BOTH stages ran, in the right order (blue drew AFTER,
on top of, red), and proves the real `AlphaOver` blend math is genuinely
being computed (not just "last write wins" — a wrong or reversed stage
order would show the OPPOSITE center/edge coloring, which is exactly why
this specific 2-plugin, 2-color, 2-blend-mode combination was chosen —
mirrors the Proposal's own Section 3.7 "two genuinely different,
distinguishable outputs" requirement, done here at PHASE5's own smaller
scale before PHASE6's full committed proof).

Register both temporarily in `CMakeLists.txt`, verify, then DELETE both
plugin folders and their `CMakeLists.txt` lines before this phase's own
final commit (`git_status` must show zero trace, Workflow Rule 10).

### Verification

1. Incremental build: `cmake --build build`.
2. Live smoke test with both scratch stage-A/stage-B plugins loaded:
   `run_app_background`, `GET /get_game_view`, `load_image` (or inspect the
   returned bytes directly) — confirm the exact blue-center, red-edges
   pattern described in Step 3.3's own hand-computed expectation (state
   clearly in the completion report whether the actual captured image
   matched that expectation before declaring success — if it does NOT
   match, this is a real bug to root-cause via `ask_questions`, never
   silently accepted as "close enough").
3. Delete both scratch plugins, rebuild, confirm `GET /get_game_view`
   reverts to the pre-existing magenta `_v1` baseline.
4. `git_status` — confirm zero trace of either scratch plugin in the final
   diff.

### What this phase does NOT do

- Does not add any permanent, committed demo plugin (PHASE6).
- Does not touch the Render Graph panel (PHASE7).
- Does not change `IPluginRenderPassBuilder_v2`'s own 3 fixed operations in
  any way — only the BLEND step between plugins changes in this phase.

### Completion

Write `PHASE5_COMPLETION_REPORT.md` (the exact hand-computed-vs-actual pixel
comparison, screenshot evidence), then `git_add` + `git_commit`.
