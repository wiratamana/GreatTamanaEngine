# Render feature authoring (`gte::fx`, the feature kit)

A render feature is written against `src/Core/FeatureKit/FeatureKit.h`: Vulkan-free, handle based. Feature code
(everything under `src/Features/<Name>/` except `Editor/`) includes only the kit, `Math/`, `ECS/`,
`Core/Extent.h`, `Core/PixelFormat.h`, `Core/Logging.h` and `Game/Lighting/`. FeaturePurityCheck lists every file that
shrinks and is empty. Shadow, SceneLighting and Atmosphere have no entry.

## Shell

| Piece | Purpose |
|---|---|
| `fx::Feature`, `fx::FeatureHost` | Base class and narrow engine facade (`Scene()`). |
| `fx::FeatureSetup` | `PreOpaque`, `PostOpaque`, `PostTransparent`, `PostComposite`, `PreUI`, `PreOpaqueShared`, `DeclareSceneSlot`, `CountInViewStats(passName)` (counts that pass in the per-view draw statistics). Registrations are only collected. |
| `fx::Gate` | Pass names a registration answers to in the toggle panel (default: the registered name). `Gate::Always()` is for work that is not a render pass (a uniform write) and must never be silenced by a toggle. |
| `fx::View`, `fx::Frame` and typed frames | `PreOpaqueFrame`, `PostOpaqueFrame` (scene color and depth), `CompositeFrame` (private target, extent), `SharedFrame`. |
| `fx::Texture` | Graph handle stamped with `FrameSerial(frame, view)`. Valid in every callback of that view in that frame, never later. A frame also accepts textures made by the Shared callback of the same frame. |
| `GTE_REGISTER_RENDER_FEATURE("Name", Class)` | Registers a built-in feature module. `fx::FeatureOf<T>(module)` reaches it from editor code; `fx::FindFeature(module)` returns the `fx::Feature*` of any module (null if it is not a kit feature). |
| `Feature::DebugEndpoint()` | Optional `fx::IFeatureDebugEndpoint*`. It makes the feature readable and settable through `/feature_debug/*` (see `networking.md`). `FeatureDebug.h` has the pure value parsers `ParseDebugBool`, `ParseDebugInt`, `ParseDebugUnsigned`, `ParseDebugFloat` (whole value, no trailing text, no `nan`/`inf`). `Set()` returns the new state in `fields`. Everything runs on the main thread. |

`Setup()` is atomic: if it throws, or any registration is invalid (empty name, name over 63 bytes, duplicate name,
missing callback), nothing is registered. Callbacks of a destroyed module do nothing, because Core cannot unregister
a built-in pass.

The gate runs before the callback, so a disabled pass runs no side effect. Per-view callbacks are skipped for a view
the engine has no data for. `PreOpaqueShared` runs once per frame; a throwing callback is logged once and not retried
in that frame. Every verb also checks its own pass name and declares nothing while it is toggled off.

## Verbs (on `fx::Frame`)

| Verb | Result |
|---|---|
| `Compute(shader, "Pass.Name")` | `ComputeChain`: `Params`, `Push`, `Bind`, `BindOutput(name, readback)`, `Dispatch(extent)`. |
| `Fullscreen(shader, "Pass.Name")` | `FullscreenChain`: `Bind`, `Push`, `Acknowledge`, `Target`, `DepthTest(texture, compare, write)`, `Draw()`. Scene depth is acknowledged for you. |
| `ViewBackground(shader, "Pass.Name")` (post-opaque frames) | A `FullscreenChain` already set to draw into the scene color where the depth test passes `Equal`, without writing depth. In the Game view the Frame Debugger can replay it. |
| `SceneDraw("Pass.Name")` | `SceneDrawChain`: `Shaders`, `Camera`, `ClearDepthTarget`, `ColorTarget`, `CullToFrustum(padding)` (skips draws whose world bounds miss the camera's view-projection; meshes without bounds are never skipped), `Record()`. |
| `Blit("Pass.Name", src, dst, filter)` | Scaled copy. |
| `ExposeAsViewOutput(viewOutput, texture)` | Marks the texture `viewOutput.Acquire()` returned as this view's output for the editor and HTTP. Call it only after the pass that writes it was declared. |
| `Keep(texture)` | Keeps an output that nothing reads. |
| `PublishToScene(slot, texture)`, `SetSceneUniforms(value)` | Feeds material shaders. The uniform block is at most 128 bytes (compile-time check). |
| `FrameIndex()` | Frame counter, the same for both view scopes of one frame. Use it for "N frames ago", never time. |
| `DeltaSeconds()` | Real frame time in seconds (`Time::UnscaledDeltaTime()`), clamped to 0.25 s; 0 without an engine context. Independent of pause. Never divide by it. Turn a per-second rate into a per-frame step with `1 - exp(-dt / tau)`. |
| `ShaderReady(computeShader)` | True when the shader is built (builds it now if needed). Check it before declaring passes that only make sense together with a dispatch. |

Pass names are literals (`rg::StaticName`); a runtime string does not compile. Shader variables are bound by name and
the access (read or write) comes from the SPIR-V. A chain that ends without its terminal call asserts in debug. A refused
chain declares nothing and logs once.

## Resources and data flow

| Type | Notes |
|---|---|
| `fx::PerView<T>` | One `T` per view scope. |
| `fx::PersistentTarget{base}` | Color texture that outlives the frame; name is `<base>.GameView` or `<base>.SceneView`, owner is the feature name. A resize applies one frame late. `Acquire` returns an invalid texture when refused. |
| `fx::OffscreenTarget{base}` | Render view (a depth map, or color plus depth), imported every frame. `Acquire` with another size resizes it (the GPU goes idle once); a failed create or resize backs off (120 frames, doubling) and a different size retries at once. A resize is refused while the view runs pipelined. A change between depth-only and color logs one error. `Trim(frame)` shrinks an existing target to 4x4 to free memory; the next `Acquire` of the full size grows it back. |
| `fx::PersistentVolume{base, size, format}` | 3D texture (storage and sampled) per view scope, imported every frame and kept alive. Its contents carry over only if a pass rewrites them. |
| `fx::ViewOutput{suffix}` | Engine-owned texture per view named `GameView<suffix>` / `SceneView<suffix>`, resized on demand (the GPU is waited for first). It is what the editor panel and the HTTP endpoints show once exposed with `ExposeAsViewOutput`. |
| `fx::GpuParams<T>` | CPU-written parameter block; its size is fixed by the first use. |
| `fx::GpuReadback<T>` | Small GPU-written value (`T` trivially copyable, multiple of 4 bytes, at most 256) the CPU reads two frames later. Ring of 3 cells per view. Declare `f.ClearReadback(name, r, fill)`, a `Compute(...).BindOutput("var", r)` pass, then `f.CopyReadbackToHost(name, r)`. `r.Latest(f)` returns the value recorded two frames ago and its frame, or nothing (first frames, a pass toggled off, a refused allocation). Declare all three passes together or none. |
| `fx::ComputeShader{"Name"}`, `fx::FullscreenShader{"Name"}` | Load `shaders/<Name>.comp.spv`, or `.vert` and `.frag`. Built lazily; a failed build retries on a cooldown and never throws. |
| `fx::SceneShaderSet::DepthOnly("Base")` | Per-vertex-layout depth pipelines; registered pipelines are removed on destruction. |
| `fx::Channel<T>{"name"}` | Per-view hand-off between callbacks of one feature. The kit prefixes the feature name, so the key is `<Feature>.<name>`; do not repeat the feature in the channel name. A value published by a `PreOpaqueShared` callback is visible to every view of that frame. |
| `fx::SceneSlot` | Slot in the scene-services set, declared in `Setup()` with `DeclareSceneSlot(name, kind, index)`. |

## Worked examples

`src/Features/Shadow/ShadowFeature.cpp`: cascaded shadow maps (see `shadow-mapping.md`). `Setup()` declares four
registrations. `Shadow.Cascades` (`PreOpaque`) fits up to four cascades and draws each with `SceneDraw` (plus
`CullToFrustum`) into its own `OffscreenTarget`, resized when the setting changes and trimmed when unused; it publishes
the maps through a `Channel`. `Shadow.DepthRange` (`PostOpaque`) measures the visible depth range into an
`fx::GpuReadback`. `Shadow.Mask` (`PostOpaque`) is a `Compute` over `PersistentTarget`s. `Shadow.Composite`
(`PostComposite`) is a `Fullscreen` into the private target. `ShadowDebugEndpoint` exposes the settings over HTTP.

`src/Features/SceneLighting/SceneLightingFeature.cpp`: one `PreOpaque` registration with `Gate::Always()` that writes the
sun into the scene uniform block (`SetSceneUniforms`) for every view. It declares no pass.

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
