# Volumetric resources
> How a render feature uses a real 3D GPU texture (a volume): fill it with compute, read it later. For feature authors.
> Verified against code: 2026-10-30

## TL;DR
- A volume is an `fx::PersistentVolume` member of the feature. One 3D texture per view scope.
- Its size and format are fixed at construction. A zero axis is refused.
- It is imported with an undefined layout every frame. Its contents do not carry over. A pass must rewrite all of it.
- The kit has no transient volume and no volume pool. Do not add one without a real need.
- Two features use volumes today: Atmosphere (aerial volume) and VolumetricLight (three froxel volumes).

## Rules
1. Declare the volume as a feature member, usually inside a `fx::PerView<State>` struct.
2. Call `f.PassEnabled("Pass")` first. A disabled pass must not create, import or keep anything.
3. Call `Acquire(f)` once per callback. It returns an invalid texture on refusal. Then return.
4. Bind the volume by shader variable name. The shader declares `writeonly image3D`.
5. Rewrite the whole volume every frame. For history, use a 2D ping-pong `PersistentTarget` (see below).
6. Pick the dispatch shape by data dependency, not by habit.
7. Include `VolumetricFroxelMath.glsl` for slice-to-depth math. Never keep a private copy.

```cpp
fx::PersistentVolume m_volume{ "ExampleVolume", fx::VolumeSize{ 64, 64, 32 }, PixelFormat::RGBA16_Float };

if (!f.PassEnabled("ExampleVolume.Fill")) { return; }
const fx::Texture volume = m_volume.Acquire(f);
if (!volume.IsValid()) { return; }
```

## Dispatch shape

| Shape | Use when | Dispatch | Example |
|---|---|---|---|
| Per cell `(x, y, z)` | Every cell is independent | `.Dispatch(Extent2D{w, h}, depth)` | `src/Shaders/VolumetricFroxelExamplePerCellFill.comp` (local size 4x4x4). VolumetricLight Inject and Temporal |
| Per column `(x, y)`, loop over Z | A slice depends on earlier slices of the same column | `.Dispatch(Extent2D{w, h})` | `src/Shaders/VolumetricFroxelExamplePerColumnFill.comp` (4x4x1). Atmosphere aerial volume, VolumetricLight Integrate |

- Rule of thumb: if a slice depends on any other slice, use per column.
- The wrong shape gives no error and no validation warning. You only get wrong values.
- `Dispatch` takes the element count per axis and rounds up to whole groups. The shader must still bounds-check against `imageSize()`.

```cpp
f.Compute(m_fillShader, "ExampleVolume.Fill")
    .Bind("outputImage", volume)
    .Dispatch(Extent2D{ n.width, n.height }, n.depth);
```

## Do and Don't

| Do | Don't |
|---|---|
| Acquire after `PassEnabled` | Acquire first and check the toggle later |
| Rewrite every texel each frame | Expect last frame's content in the volume |
| Keep history in a 2D atlas `PersistentTarget`, two of them, alternated by `FrameIndex() & 1` | Try to carry content in a `PersistentVolume` |
| Use one `Image3D` scene slot at most | Treat `Image3D` slots like `Image2D` slots |
| Share slice math from `src/Shaders/VolumetricFroxelMath.glsl` | Copy the formula into a feature shader |

## How to make the result readable

| Way | Use when | How |
|---|---|---|
| Channel plus a later pass | Your own pass samples or composites the volume | `Channel<T>.Publish(f, value, {volume})` after the dispatch was declared. A later callback calls `Fetch(f)` and uses `Bind(...)` |
| `Image3D` scene slot | Every material shader must sample it | `DeclareSceneSlot(name, SceneSlotKind::Image3D, index)` in `Setup()`, then `f.PublishToScene(slot, volume)` |

- Atmosphere publishes the volume on its `AerialVolume` channel. The composite pass binds it.
- VolumetricLight publishes `Result` (the integrated volume, max distance, depth exponent) for its composite pass.
- Publish only after the dispatch was declared. Put the volume in the keep list.
- A published channel that another toggleable pass reads needs a risk rule. See [Render honesty](render-honesty.md).

## Checks
- `ctest --test-dir build -R "FeaturePurityCheck|HeaderPurity"` for kit-only code.
- The volume test pins the example shaders and the slice formula: `tests/Renderer/RenderGraph/VolumeTextureExampleFeatureTests.cpp`.
- `/list_textures` lists a volume with `kind` `texture3d`. `/get_texture` renders a preview of it. See [HTTP cheat sheet](../debugging/http-cheat-sheet.md).

## Gotchas
- A pass whose only output is a volume is culled unless its handle is kept. `PersistentVolume::Acquire()` keeps it for you.
- A volume imported directly with `ImportVolumeTexture()` needs `KeepTextureOutput(handle)`. Without it the pass is dropped silently every frame.
- `Image3D` has one shared dummy for every `Image3D` slot. A second slot shares it with no error. No shipped feature registers an `Image3D` slot.
- A failed creation is logged once under `FeatureKit` and retried on a cooldown. Check the log if a volume stays invalid.
- The volume's graph name is `<base>.GameView` or `<base>.SceneView` in the editor.

## Related
- [Feature kit reference](../reference/feature-kit.md) — `PersistentVolume`, channels, scene slots
- [Writing a render feature](../guides/writing-a-render-feature.md) — feature steps
- [Atmosphere](../reference/render-features/atmosphere.md) — aerial perspective volume
- [Volumetric light](../reference/render-features/volumetric-light.md) — froxel volumes with temporal history
- [Render honesty](render-honesty.md) — rules for published channels
