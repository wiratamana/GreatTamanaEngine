# Volumetric Resources (`VolumeTexture` Recipe)

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

This page documents the GENERIC recipe for building any future
`VolumeTexture`-backed feature — a real 3D (`VK_IMAGE_TYPE_3D`) GPU-resident
grid, filled by a compute pass, and sampled later. It is written against a
minimal fictional example feature (`ExampleVolumeFeature`), never the real
Atmosphere code, so a reader never has to mentally subtract unrelated physics
to find the reusable shape underneath — every real code excerpt below is
copy-pasted verbatim from this campaign's own compiling scratch examples
(`tests/Renderer/RenderGraph/VolumeTextureExampleFeatureTests.cpp` and its two
companion shaders), which exist for exactly this purpose.
[`docs/conventions/atmosphere-scattering.md`](atmosphere-scattering.md)
remains the authority for the one real shipped feature's own specific
behavior, tuning, and history — its "genuine THIRD kind" section documents the
exact same mechanism from that one feature's point of view. **A future change
to the underlying mechanism itself** (how a `VolumeTexture` is created,
imported, or written) **must update both pages in the same change.**

## 1. Ownership and lifetime

`GpuResourceFactory::CreateVolumeTexture()` / `Renderer::CreateVolumeTexture()`
(`src/Renderer/Renderer.h`) is called **once**, at the owning feature's own
construction time — verbatim from the per-cell example test:

```cpp
VolumeTexture volume = fixture.GetRenderer().CreateVolumeTexture(
    static_cast<int>(kSize), static_cast<int>(kSize), static_cast<int>(kSize), VK_FORMAT_R16G16B16A16_SFLOAT, kVolumeName);
```

State the single most counter-intuitive fact plainly: a `VolumeTexture` is
**persistent**, owned entirely by whichever system created it — it is
**never** pooled or allocated by the render graph itself, unlike a plain
`RenderGraphBuilder::CreateTexture()` transient resource. There is deliberately
no pooled/transient counterpart to `ImportVolumeTexture()` (see
`atmosphere-scattering.md`'s own identical statement) — do not build one
speculatively; only a genuine future need (more than one distinct volume with
varying sizes across frames) justifies it.

## 2. Importing every frame — both real cases

Every frame, from inside a pass-declaring callback, the owning feature
re-imports its persistent `VolumeTexture` into that frame's graph —
`RenderGraphBuilder::ImportVolumeTexture(name, externalVolumeTarget,
currentLayout)`:

```cpp
const VolumeTextureHandle handle = b.ImportVolumeTexture(kVolumeName, volume.Target(), VK_IMAGE_LAYOUT_UNDEFINED);
```

- **Case (a), full rewrite every frame (the one shipped feature's real case,
  and the only case either of this campaign's own tests exercise — shown
  above).** Pass `VK_IMAGE_LAYOUT_UNDEFINED`. As of this writing, the ONLY
  real `ImportVolumeTexture()` call site driving a shipped feature
  (`src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`, line 597) also always
  passes `VK_IMAGE_LAYOUT_UNDEFINED` — there is no shipped feature and no
  automated test exercising a non-`UNDEFINED` import for this resource kind.
- **Case (b), partial/carried update (a plausible future need).** Pass the
  real previous layout instead. The underlying mechanism threads a real
  previous layout into the SAME barrier-planning path an imported plain 2D
  resource already uses — this is an engine-wide simplification, not
  something weaker for `VolumeTexture` specifically — so it is **expected to
  work but unverified** for this resource kind; say so plainly to anyone
  relying on it. The nearest real precedent for "a feature tracking a
  resource's own previous layout across frames" is
  `src/Renderer/RenderGraph/RenderGraphPersistentResourceCache.h`'s
  `lastKnownLayout`/`RecordFinalLayout()` — but that cache is 2D-`TextureDesc`
  only (no `VolumeTexture` support today) and is a full eviction/resize/
  epoch-tracked cache, not a small drop-in piece of per-feature state; it is
  NOT comparable to something as cosmetic as a debug name surviving a resize
  — tracking a real `VkImageLayout` is a different, load-bearing concern. A
  feature that needs case (b) today must track its own previous
  `VkImageLayout` as a plain member variable, updated at the end of each
  frame, and should consider itself the first real user of this exact path.

## 3. The compute pass — both valid dispatch shapes

`PassBuilder::WriteVolumeTexture(handle, access)` declares the write. There
are exactly two valid dispatch shapes, each proven by one of
`tests/Renderer/RenderGraph/VolumeTextureExampleFeatureTests.cpp`'s two real,
headless-GPU-executed tests:

### (a) One invocation per cell `(x, y, z)`

Correct when every cell's value can be computed completely independently of
every other cell — execution order never matters. The minimal worked example,
`src/Shaders/VolumetricFroxelExamplePerCellFill.comp`:

```glsl
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

layout(binding = 0, rgba16f) uniform writeonly image3D outputImage;

void main()
{
    ivec3 cell = ivec3(gl_GlobalInvocationID);
    ivec3 size = imageSize(outputImage);
    if (cell.x >= size.x || cell.y >= size.y || cell.z >= size.z) {
        return;
    }
    imageStore(outputImage, cell, vec4(float(cell.x), float(cell.y), float(cell.z), 1.0));
}
```

dispatched with the Z extent included in the dispatch itself:

```cpp
cmd.DispatchOverSize(kSize, kSize, kSize);
```

### (b) One invocation per `(x, y)` column, looping every Z slice internally

Correct whenever a slice's value genuinely depends on the previous slice's
running state along the same column. This is the real shipped feature's own
  case: `src/Features/Atmosphere/Shaders/AtmosphereAerialPerspectiveVolume.comp`
(`local_size_x=8, local_size_y=8, local_size_z=1`), dispatched by
`AtmosphereLutRenderer::AddAerialPerspectiveVolumePass()`
(`src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`, lines 574-656) via
`cmd.DispatchOverSize(width, height, 1)` — Z group count fixed at 1 — because
each slice's accumulated in-scattering depends on the slice before it. The
campaign's own minimal, feature-free illustration of the same shape,
`src/Shaders/VolumetricFroxelExamplePerColumnFill.comp`:

```glsl
#include "VolumetricFroxelMath.glsl"

layout(local_size_x = 4, local_size_y = 4, local_size_z = 1) in;

layout(binding = 0, rgba16f) uniform writeonly image3D outputImage;

layout(push_constant) uniform PushConstants {
    float maxDistanceKm;
    float depthExponent;
} pc;

void main()
{
    ivec3 size = imageSize(outputImage);
    ivec2 column = ivec2(gl_GlobalInvocationID.xy);
    if (column.x >= size.x || column.y >= size.y) {
        return;
    }

    float sliceCount = float(size.z);
    float runningTotal = 0.0;
    for (int slice = 0; slice < size.z; ++slice) {
        float viewDepthKm = FroxelSliceToViewDepth(float(slice), sliceCount, pc.maxDistanceKm, pc.depthExponent);
        runningTotal += viewDepthKm;
        imageStore(outputImage, ivec3(column, slice), vec4(runningTotal, runningTotal, runningTotal, 1.0));
    }
}
```

dispatched with the Z extent fixed at 1:

```cpp
cmd.DispatchOverSize(kWidth, kHeight, 1);
```

**The one-sentence selection rule**: if a given slice's fill value depends on
any other slice's value, you must use shape (b); if every cell's value is
fully independent, use shape (a) — picking the wrong one for a given
algorithm produces no build error and no validation-layer warning, only
silently wrong per-cell results.

## 3b. Keeping the pass alive — the mandatory `KeepVolumeTextureOutput()` call

**This is the single most common way a first-time `VolumeTexture` feature
silently fails — read this even if you skim everything else.** A
`VolumeTextureHandle` can **never** be a `finalOutputs` root — that vector is
texture-only, shared by every `RenderGraph::Execute()` call site in the
engine. A pass whose only write is a `VolumeTexture` must have its handle
passed to `RenderGraphBuilder::KeepVolumeTextureOutput(handle)` explicitly, or
`RenderGraphCompiler::Compile()`'s backward-reachability culling drops the
whole pass silently, every single frame — no build error, no validation-layer
warning, just a volume that never gets filled.

This is a real, verified trap, not a theoretical one:
`AddAerialPerspectiveVolumePass()` itself (`AtmosphereLutRenderer.cpp`, lines
574-656) does NOT call `KeepVolumeTextureOutput()` anywhere in its own body —
its caller does, separately, at `src/Core/Core.cpp` line 848:

```cpp
frame.builder.KeepVolumeTextureOutput(viewLuts.aerialPerspectiveVolumeHandle);
```

Both of `VolumeTextureExampleFeatureTests.cpp`'s own tests declare a pass
whose only write is a `VolumeTexture`, and both had to add this exact call,
right after declaring the pass, for their own numeric assertions to pass —
verbatim from the per-cell test's own `build` lambda:

```cpp
fixture.RunSynchronousFrame([&](RenderGraphBuilder& b) -> std::vector<TextureHandle> {
    const VolumeTextureHandle handle = b.ImportVolumeTexture(kVolumeName, volume.Target(), VK_IMAGE_LAYOUT_UNDEFINED);
    b.AddRenderPass(
        "VolumeTextureExamplePerCellFillPass", PassKind::Compute,
        [handle](RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteVolumeTexture(handle, ResourceAccess::ComputeShaderWrite);
        },
        [&](PassContext& ctx) {
            const PassContext::ResolvedVolumeTexture dest = ctx.resolveVolumeTexture(handle);
            descriptorSet.Rewrite(device, { ComputeDescriptorWrite::StorageImage(0, dest.view) });

            auto cmd = ctx.Cmd();
            cmd.BindComputePipeline(pipeline);
            cmd.BindDescriptorSet(descriptorSet.Native());
            cmd.DispatchOverSize(kSize, kSize, kSize);
        });
    // Mandatory - a pass whose only write is a VolumeTextureHandle is
    // silently culled without this call.
    b.KeepVolumeTextureOutput(handle);
    return {};
});
```

## 4. Making the result consumable — two documented paths

### (a) A `SceneServiceResourceKind::Image3D` slot

The right choice when every opaque-shaded object should sample this volume
uniformly via a stable, registered slot index
(`src/Renderer/SceneServicesDescriptorSet.h`). This slot kind is fully wired
at the type/descriptor-set level (a real dummy `VolumeTexture` fallback,
correct per-slot resource-kind dispatch) — but as of this writing no real
feature has ever registered an `Image3D`-kind slot; only the dummy fallback
path has ever run in practice.

State the load-bearing caveat plainly, confirmed by reading
`SceneServicesDescriptorSet` directly: unlike `Image2D` (one real dummy
texture PER slot index, in a per-index container,
`m_dummyImage2DTextures`), the `Image3D` dummy fallback
(`SceneServicesDescriptorSet.h`, line 158's `m_dummyImage3DTexture`,
constructed unconditionally at `SceneServicesDescriptorSet.cpp` line 241) is a
**single shared instance** used for EVERY `Image3D`-kind slot — sound only
while at most one such slot exists. A second, independent feature registering
an `Image3D`-kind slot must not assume its own distinct dummy — today it
silently shares the exact same physical dummy resource (and whatever single
"neutral" value it holds) with every other `Image3D`-kind slot, with no error
or warning of any kind. The two resource kinds are NOT symmetric in this
respect.

### (b) `NamedSceneResourceKey()` + a PostOpaque/PostTransparent pass

Publish the handle via
[`docs/conventions/scene-resource-publishing.md`](scene-resource-publishing.md)'s
`rg::NamedSceneResourceKey()` for a later PostOpaque/PostTransparent pass to
sample/composite separately, as its own full-screen pass. This is the right
choice for a feature needing its OWN sampling/compositing step against scene
depth, rather than a value every material shader samples directly — exactly
the shape `AtmosphereLutRenderer::AddAerialPerspectiveCompositePass()` already
uses for the shipped feature's own volume.

## 5. The slice-depth mapping utility

The slice-index ↔ view-depth mapping used by dispatch shape (b) above is
**not** itself part of the core `VolumeTexture` mechanism — it is a
separately extracted, shared, feature-free utility:

- `src/Renderer/VolumetricFroxelMath.h`/`.cpp` — the C++ CPU oracle
  (`FroxelSliceToViewDepth()`/`ViewDepthToFroxelSlice()`), with its own unit
  test, `tests/Renderer/VolumetricFroxelMathTests.cpp`.
- `src/Shaders/VolumetricFroxelMath.glsl` — the GLSL mirror, byte-identical
  formula, same parameter names/order in both languages. `#include`d by both
  `src/Features/Atmosphere/Shaders/AtmosphereCommon.glsl` (the shipped Atmosphere feature's own
  include, replacing what used to be an inline copy) and
  `src/Shaders/VolumetricFroxelExamplePerColumnFill.comp` (this page's own
  per-column example above) — real proof this file is genuinely reusable by a
  second, independent shader, not merely relocated for tidiness.

This is a pure, byte-identical relocation of a pre-existing formula, not new
math — do not extract only the C++ side of a shared formula and call it
"reusable"; a future shader cannot call a C++ header, so both languages are
required whenever a genuinely resource-agnostic formula like this one is
pulled out of a feature-specific file.

## 6. Verification / test-harness nuance

`tests/Fakes/HeadlessRenderGraphFixture.h` already drives a full, real,
headless-GPU `RenderGraph::Execute()` frame end-to-end — this is not a gap.
The real, narrower gap this page's own two reference tests
(`VolumeTextureExampleFeatureTest.PerCellParallelFillWritesEveryCellsOwnCoordinates`/
`VolumeTextureExampleFeatureTest.PerColumnSequentialFillMatchesTheCpuOracleRunningTotal`,
`tests/Renderer/RenderGraph/VolumeTextureExampleFeatureTests.cpp`) close is
the first real example of a `VolumeTexture`-writing compute pass declared
through that exact fixture, dispatched for real, and read back via
`Renderer::CaptureImagePixels()` (extended with an arbitrary-Z-slice readback
capability — `zOffset`/`depth` trailing, defaulted parameters) and asserted
numerically against a CPU-computed expected value. On a machine whose Vulkan
driver/loader lacks `VK_EXT_headless_surface`, both tests report `SKIPPED`
rather than a live pass — the same honest, pre-existing, environment-gated
limitation every other `HeadlessRenderGraphFixture`-based test in this
codebase already has.

## 7. What NOT to do

- Do not build a new "`VolumeTexture` pool" — it is deliberately import-only,
  by design; this page documents the existing mechanism, it does not change
  it.
- Do not force-generalize feature-specific physics into a shared utility "for
  tidiness" if it genuinely is not resource-shape-agnostic.
- Do not extract only the C++ side of a shared formula and call it
  "reusable" — both the CPU oracle and the GLSL mirror are required.
- Do not let a relocated GLSL formula end up duplicated instead of shared —
  the consuming file must `#include` it, never keep an independent copy.
- Do not present either compute-dispatch shape as the one-and-only correct
  recipe — pick the one the real data dependency actually demands.
- Do not present the `Image3D` scene-service-slot path as fully symmetric to
  `Image2D` — the single-shared-dummy caveat in section 4(a) above is real.
- Do not omit the `KeepVolumeTextureOutput()` requirement (section 3b) or
  treat it as a footnote — it is the single most common way a first-time
  `VolumeTexture` feature silently fails.
