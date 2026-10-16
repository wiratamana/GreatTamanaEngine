# Volumetric Resources (`VolumeTexture` Recipe)

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

How to build a feature on a real 3D (`VK_IMAGE_TYPE_3D`) GPU grid: a compute pass
fills it, a later pass samples it. The examples use a fictional `ExampleVolumeFeature`
written with the feature kit ([`render-feature-authoring.md`](render-feature-authoring.md)).
[`atmosphere-scattering.md`](atmosphere-scattering.md) describes the one shipped
feature that uses a volume (the aerial perspective volume). A change to how a
volume is created, imported or written must update both pages.

## 1. Ownership and lifetime

A volume is owned by `fx::PersistentVolume` (`src/Core/FeatureKit/PersistentVolume.h/.cpp`),
a member of the feature (usually inside a `fx::PerView<State>` struct):

```cpp
fx::PersistentVolume m_volume{ "ExampleVolume", fx::VolumeSize{ 64, 64, 32 }, PixelFormat::RGBA16_Float };
```

- The size and format are fixed at construction. A zero axis is refused (logged once).
- It keeps one `VolumeTexture` per view scope, created lazily on the first `Acquire()`.
  A failed creation is logged once and retried on a cooldown.
- It is persistent and owned by the feature. The render graph never pools or
  allocates a volume. There is no transient volume. Do not build one without a
  real need.

## 2. Acquire every frame

Call `Acquire(f)` from the callback that declares the pass. It imports the volume
into this frame's graph with an undefined layout and keeps the handle alive:

```cpp
if (!f.PassEnabled("ExampleVolume.Fill")) {
    return; // a disabled pass must not create or import anything
}
const fx::Texture volume = m_volume.Acquire(f);
if (!volume.IsValid()) {
    return;
}
```

Because the layout is undefined, the contents do not carry over between frames.
A pass must rewrite the whole volume every frame. Carrying content from the
previous frame is not supported by `PersistentVolume`.

## 3. The compute pass: two dispatch shapes

Bind the volume by shader variable name. `Dispatch(Extent2D size, depth)` takes
the number of elements per axis. The engine rounds up to whole groups. The shader
must bounds-check against `imageSize()`.

### (a) One invocation per cell `(x, y, z)`

Use it when every cell is computed independently. Example shader:
`src/Shaders/VolumetricFroxelExamplePerCellFill.comp`
(`local_size 4x4x4`, binding `outputImage`).

```cpp
const fx::VolumeSize n = kVolumeSize;
f.Compute(m_fillShader, "ExampleVolume.Fill")
    .Bind("outputImage", volume)
    .Dispatch(Extent2D{ n.width, n.height }, n.depth);
```

### (b) One invocation per `(x, y)` column, looping over Z in the shader

Use it when a slice depends on the previous slice of the same column. This is
the Atmosphere aerial perspective volume
(`src/Features/Atmosphere/Shaders/AtmosphereAerialPerspectiveVolume.comp`,
`local_size 8x8x1`). The feature dispatches X and Y only:

```cpp
f.Compute(m_aerialVolumeShader, "AtmosphereAerialPerspectiveVolumePass")
    .Params(m_sharedParams, shared->parameters)
    .Bind("destinationVolume", volume)
    .Dispatch(kAerialDispatchSize);
```

Feature-free example: `src/Shaders/VolumetricFroxelExamplePerColumnFill.comp`
(`local_size 4x4x1`, binding `outputImage`, push constants `maxDistanceKm` and
`depthExponent`).

**Selection rule**: if a slice's value depends on any other slice, use (b). If all
cells are independent, use (a). Picking the wrong one gives no error and no
validation warning, only wrong values.

## 4. Culling: keep the written volume

A pass whose only output is a volume is culled by the graph unless its volume
handle is kept. `PersistentVolume::Acquire()` already calls `KeepTextureOutput()`
on the handle, so kit features need nothing more. Code that imports a volume with
`RenderGraphBuilder::ImportVolumeTexture()` directly must call
`KeepTextureOutput(handle)` itself, or the pass is dropped silently every frame.

## 5. Making the result consumable

### (a) A `SceneSlotKind::Image3D` scene slot

Use it when every opaque-shaded material should sample the volume from a fixed
slot. Declare the slot in `Setup()` with `FeatureSetup::DeclareSceneSlot(name, SceneSlotKind::Image3D, preferredIndex)`
and publish with `f.PublishToScene(slot, volume)`
(`src/Core/FeatureKit/SceneSlot.h`, `src/Renderer/SceneServicesDescriptorSet.h`).

Caveat: `Image2D` has one dummy texture per slot index. `Image3D` has ONE shared
dummy (`m_dummyImage3DTexture` in `SceneServicesDescriptorSet`) for every
`Image3D` slot. It is sound only while at most one `Image3D` slot exists. A second
`Image3D` slot silently shares the same dummy and no error is logged. No shipped
feature registers an `Image3D` slot today.

### (b) A channel plus a later pass

Publish the volume with a `fx::Channel<T>` (for example
`AtmosphereViewLuts` in `AtmosphereFeature.h`) and read it in a later callback
(`PostOpaque` or `PostComposite`) as a normal `Bind(...)` of a fullscreen or
compute pass. Use it when the feature needs its own sampling or compositing step
against scene depth. Atmosphere does this: `Composite()` binds
`aerialPerspectiveVolume` in `AtmosphereAerialPerspectiveCompositePass`.
For engine-level publishing without the kit, see
[`scene-resource-publishing.md`](scene-resource-publishing.md).

## 6. The slice-depth mapping utility

The slice index to view depth mapping used by dispatch shape (b) is a feature-free GLSL
utility: `src/Shaders/VolumetricFroxelMath.glsl` (`FroxelSliceToViewDepth()`,
`ViewDepthToFroxelSlice()`). It is `#include`d by `AtmosphereCommon.glsl` and by
`VolumetricFroxelExamplePerColumnFill.comp`. A shader that needs the formula must
`#include` the shared file, never keep its own copy.

## 7. What NOT to do

- Do not build a `VolumeTexture` pool. Volumes are import-only.
- Do not move feature-specific physics into a shared utility if it is not
  resource-shape-agnostic.
- Do not present either dispatch shape as the only recipe. Pick the one the data
  dependency requires.
- Do not treat `Image3D` scene slots as symmetric to `Image2D` (single shared
  dummy, section 5a).
- Do not call `Acquire()` before checking `f.PassEnabled(...)`. A disabled pass must
  not create, import or keep anything.
