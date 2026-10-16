# Render feature authoring (`gte::fx`, the feature kit)

A render feature is written against `src/Core/FeatureKit/FeatureKit.h`: Vulkan-free, handle based. Feature code
(everything under `src/Features/<Name>/` except `Editor/`) includes only the kit, `Math/`, `ECS/`,
`Core/Extent.h`, `Core/PixelFormat.h`, `Core/Logging.h` and `Game/Lighting/`. FeaturePurityCheck lists every file that
shrinks and is empty. Shadow and Atmosphere have no entry.

## Shell

| Piece | Purpose |
|---|---|
| `fx::Feature`, `fx::FeatureHost` | Base class and narrow engine facade (`Scene()`). |
| `fx::FeatureSetup` | `PreOpaque`, `PostOpaque`, `PostTransparent`, `PostComposite`, `PreUI`, `PreOpaqueShared`, `DeclareSceneSlot`, `CountInViewStats(passName)` (counts that pass in the per-view draw statistics). Registrations are only collected. |
| `fx::Gate` | Pass names a registration answers to in the toggle panel (default: the registered name). |
| `fx::View`, `fx::Frame` and typed frames | `PreOpaqueFrame`, `PostOpaqueFrame` (scene color and depth), `CompositeFrame` (private target, extent), `SharedFrame`. |
| `fx::Texture` | Graph handle stamped with `FrameSerial(frame, view)`. Valid in every callback of that view in that frame, never later. A frame also accepts textures made by the Shared callback of the same frame. |
| `GTE_REGISTER_RENDER_FEATURE("Name", Class)` | Registers a built-in feature module. `fx::FeatureOf<T>(module)` reaches it from editor code. |

`Setup()` is atomic: if it throws, or any registration is invalid (empty name, name over 63 bytes, duplicate name,
missing callback), nothing is registered. Callbacks of a destroyed module do nothing, because Core cannot unregister
a built-in pass.

The gate runs before the callback, so a disabled pass runs no side effect. Per-view callbacks are skipped for a view
the engine has no data for. `PreOpaqueShared` runs once per frame; a throwing callback is logged once and not retried
in that frame. Every verb also checks its own pass name and declares nothing while it is toggled off.

## Verbs (on `fx::Frame`)

| Verb | Result |
|---|---|
| `Compute(shader, "Pass.Name")` | `ComputeChain`: `Params`, `Push`, `Bind`, `Dispatch(extent)`. |
| `Fullscreen(shader, "Pass.Name")` | `FullscreenChain`: `Bind`, `Push`, `Acknowledge`, `Target`, `DepthTest(texture, compare, write)`, `Draw()`. Scene depth is acknowledged for you. |
| `ViewBackground(shader, "Pass.Name")` (post-opaque frames) | A `FullscreenChain` already set to draw into the scene color where the depth test passes `Equal`, without writing depth. In the Game view the Frame Debugger can replay it. |
| `SceneDraw("Pass.Name")` | `SceneDrawChain`: `Shaders`, `Camera`, `ClearDepthTarget`, `ColorTarget`, `Record()`. |
| `Blit("Pass.Name", src, dst, filter)` | Scaled copy. |
| `ExposeAsViewOutput(viewOutput, texture)` | Marks the texture `viewOutput.Acquire()` returned as this view's output for the editor and HTTP. Call it only after the pass that writes it was declared. |
| `Keep(texture)` | Keeps an output that nothing reads. |
| `PublishToScene(slot, texture)`, `SetSceneUniforms(value)` | Feeds material shaders. The uniform block is at most 128 bytes (compile-time check). |

Pass names are literals (`rg::StaticName`); a runtime string does not compile. Shader variables are bound by name and
the access (read or write) comes from the SPIR-V. A chain that ends without its terminal call asserts in debug. A refused
chain declares nothing and logs once.

## Resources and data flow

| Type | Notes |
|---|---|
| `fx::PerView<T>` | One `T` per view scope. |
| `fx::PersistentTarget{base}` | Color texture that outlives the frame; name is `<base>.GameView` or `<base>.SceneView`, owner is the feature name. A resize applies one frame late. `Acquire` returns an invalid texture when refused. |
| `fx::OffscreenTarget{base}` | Render view (a depth map, or color plus depth), imported every frame. The size is fixed at creation: asking for another size logs one error and keeps the old target. |
| `fx::PersistentVolume{base, size, format}` | 3D texture (storage and sampled) per view scope, imported every frame and kept alive. Its contents carry over only if a pass rewrites them. |
| `fx::ViewOutput{suffix}` | Engine-owned texture per view named `GameView<suffix>` / `SceneView<suffix>`, resized on demand (the GPU is waited for first). It is what the editor panel and the HTTP endpoints show once exposed with `ExposeAsViewOutput`. |
| `fx::GpuParams<T>` | CPU-written parameter block; its size is fixed by the first use. |
| `fx::ComputeShader{"Name"}`, `fx::FullscreenShader{"Name"}` | Load `shaders/<Name>.comp.spv`, or `.vert` and `.frag`. Built lazily; a failed build retries on a cooldown and never throws. |
| `fx::SceneShaderSet::DepthOnly("Base")` | Per-vertex-layout depth pipelines; registered pipelines are removed on destruction. |
| `fx::Channel<T>{"name"}` | Per-view hand-off between callbacks of one feature. The kit prefixes the feature name, so the key is `<Feature>.<name>`; do not repeat the feature in the channel name. A value published by a `PreOpaqueShared` callback is visible to every view of that frame. |
| `fx::SceneSlot` | Slot in the scene-services set, declared in `Setup()` with `DeclareSceneSlot(name, kind, index)`. |

## Worked examples

`src/Features/Shadow/ShadowFeature.cpp` (about 110 lines): `Setup()` declares the scene slot and three passes. The depth
pass draws the scene from the sun with `SceneDraw` into an `OffscreenTarget` and publishes the map through a `Channel`,
the scene slot and the scene uniforms. The mask pass is a `Compute` over a `PersistentTarget`. The composite pass is a
`Fullscreen` into the private target.

`src/Features/Atmosphere/AtmosphereFeature.cpp`: four registrations.

- `AtmosphereSharedLut` (`PreOpaqueShared`): transmittance and multi-scattering LUTs as shared `PersistentTarget`s,
  published through a `Channel` that every view reads.
- `AtmosphereViewLut` (`PreOpaque`): per view a sky-view LUT (`PersistentTarget`) and an aerial perspective
  `PersistentVolume` (128x128x32, RGBA16F), written by `Compute`.
- `DrawSkyBackground` (`PostOpaque`): `ViewBackground` into the view color where depth is untouched.
- `AtmosphereComposite` (`PostComposite`): a `Compute` into a `ViewOutput`, `ExposeAsViewOutput`, then a `Blit` to the
  private target.

## Adding a verb

If a feature needs something the kit lacks, add a verb to the kit, in its own small `.cpp`. Never hand a feature a raw
handle or a `Native()` escape.

For the human: add a summary link to this page and to `render-pass-api.md` in `AGENTS.md` (AI agents must not edit
that file).
