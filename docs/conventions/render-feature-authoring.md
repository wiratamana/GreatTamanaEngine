# Render feature authoring (`gte::fx`, the feature kit)

A render feature is written against `src/Core/FeatureKit/FeatureKit.h`: Vulkan-free, handle based. Feature code
(everything under `src/Features/<Name>/` except `Editor/`) includes only the kit, `Math/`, `ECS/`,
`Core/Extent.h`, `Core/PixelFormat.h`, `Core/Logging.h` and `Game/Lighting/`. A record that two features share lives in
`Game/Lighting/` (`SunShadowReceiver.h`, `SunLightModulation.h`). A feature never includes another feature's header.
FeaturePurityCheck lists every file that shrinks and is empty. Shadow, SceneLighting and Atmosphere have no entry.

## Shell

| Piece | Purpose |
|---|---|
| `fx::Feature`, `fx::FeatureHost` | Base class and narrow engine facade (`Scene()`). |
| `fx::FeatureSetup` | `PreOpaque`, `Opaque`, `PostOpaque`, `PostTransparent`, `PostComposite`, `PreUI`, `PreOpaqueShared`, `DeclareSceneSlot`, `CountInViewStats(passName)` (counts that pass in the per-view draw statistics). Registrations are only collected. |
| `fx::Gate` | Pass names a registration answers to in the toggle panel (default: the registered name). `Gate::Always()` is for work that is not a render pass (a uniform write) and must never be silenced by a toggle. |
| `fx::View`, `fx::Frame` and typed frames | `PreOpaqueFrame`, `OpaqueFrame` (`SceneTarget()` writes color and depth), `PostOpaqueFrame` (scene color and depth), `CompositeFrame` (private target, extent), `SharedFrame`. |
| `fx::Texture` | Graph handle stamped with `FrameSerial(frame, view)`. Valid in every callback of that view in that frame, never later. A frame also accepts textures made by the Shared callback of the same frame. |
| `GTE_REGISTER_RENDER_FEATURE("Name", Class)` | Registers a built-in feature module. `fx::FeatureOf<T>(module)` reaches it from editor code and throws `std::logic_error` for a wrong type; `fx::FindFeature(module)` returns the `fx::Feature*` of any module (null if it is not a kit feature). |
| `Feature::DebugEndpoint()` | Optional `fx::IFeatureDebugEndpoint*`. It makes the feature readable and settable through `/feature_debug/*` (see `networking.md`). `FeatureDebug.h` has the pure value parsers `ParseDebugBool`, `ParseDebugUnsigned`, `ParseDebugFloat` (whole value, no trailing text, no `nan`/`inf`). `Set()` returns the new state in `fields`. Everything runs on the main thread. |
| `fx::OffscreenTarget` | A render view per view scope, imported every frame. `Acquire()` resizes on a new size, `Trim()` shrinks an unused target, `Extent(frame)` returns the size it really has (`{0, 0}` when none). |

`Setup()` is atomic: if it throws, or any registration is invalid (empty name, name over 63 bytes, duplicate name,
missing callback), nothing is registered. Callbacks of a destroyed module do nothing, because Core cannot unregister
a built-in pass.

The gate runs before the callback, so a disabled pass runs no side effect. Per-view callbacks are skipped for a view
the engine has no data for. `PreOpaqueShared` runs once per frame; a throwing callback is logged once and not retried
in that frame. Every verb also checks its own pass name and declares nothing while it is toggled off.

`Opaque` runs per view right after `RenderOpaque`, with an `OpaqueFrame`. `SceneTarget()` is the view's color and depth;
passes declared there write both (load, never clear) and carry the `Opaques` tag. A pass that moves draw work off the CPU
scene draw may take an entity from it only after every pass of its chain was declared (the claim is the last step, and a
chain that stops part way claims nothing). A frame that records a capture, and the shared scope, never claim.

## Verbs (on `fx::Frame`)

| Verb | Result |
|---|---|
| `Compute(shader, "Pass.Name")` | `ComputeChain`: `Params`, `Push`, `Bind`, `BindBuffer(name, GpuBufferRef)`, `BindOutput(name, readback)`, `Dispatch(extent)`. |
| `Fullscreen(shader, "Pass.Name")` | `FullscreenChain`: `Bind`, `Push`, `Acknowledge`, `Target`, `DepthTest(texture, compare, write)`, `Draw()`. Scene depth is acknowledged for you. |
| `ViewBackground(shader, "Pass.Name")` (post-opaque frames) | A `FullscreenChain` already set to draw into the scene color where the depth test passes `Equal`, without writing depth. In the Game view the Frame Debugger can replay it. |
| `SceneDraw("Pass.Name")` | `SceneDrawChain`: `Shaders`, `Camera`, `ClearDepthTarget`, `ColorTarget`, `CullToFrustum(padding)` (skips draws whose world bounds miss the camera's view-projection; meshes without bounds are never skipped), `Record()`. |
| `FillBuffer("Pass.Name", buffer, value)` | Clears the requested size of a buffer (rounded up to 4 bytes) with a 32-bit pattern. A host-visible buffer needs `BufferUse::TransferDst`. A false result means nothing was declared: do not dispatch. |
| `Blit("Pass.Name", src, dst, filter)` | Scaled copy. |
| `ExposeAsViewOutput(viewOutput, texture)` | Marks the texture `viewOutput.Acquire()` returned as this view's output for the editor and HTTP. Call it only after the pass that writes it was declared. |
| `Keep(texture)` | Keeps an output that nothing reads. |
| `PublishToScene(slot, texture)`, `SetSceneUniforms(value)` | Feeds material shaders. The uniform block is at most 512 bytes (compile-time check). |
| `FrameIndex()` | Frame counter, the same for both view scopes of one frame. Use it for "N frames ago", never time. |
| `DeltaSeconds()` | Real frame time in seconds (`Time::UnscaledDeltaTime()`), clamped to 0.25 s; 0 without an engine context. Independent of pause. Never divide by it. Turn a per-second rate into a per-frame step with `1 - exp(-dt / tau)`. |
| `ShaderReady(computeShader)` | True when the shader is built (builds it now if needed). Check it before declaring passes that only make sense together with a dispatch. |

Pass names are literals (`rg::StaticName`); a runtime string does not compile. Shader variables are bound by name and
the access (read or write) comes from the SPIR-V. A chain that ends without its terminal call asserts in debug. A refused
chain declares nothing and logs once.

Scene reads: every sample goes through a named binding, so the kit declares it. A post-stage feature never needs a fake
read of the scene color or depth, and a pass that does not sample them must not bind them. The compositor's "did not declare
ReadTexture()" check (`PostOpaque`, `PostTransparent`, `PostComposite` depth) runs only for raw project callbacks, never for
kit features. The `RenderFeatureCompositor` log category must stay empty in Debug and Release.

## Resources and data flow

| Type | Notes |
|---|---|
| `fx::PerView<T>` | One `T` per view scope. |
| `fx::PersistentTarget{base}` | Color texture that outlives the frame; name is `<base>.GameView` or `<base>.SceneView`, owner is the feature name. A resize applies one frame late. `Acquire` returns an invalid texture when refused. |
| `fx::OffscreenTarget{base}` | Render view (a depth map, or color plus depth), imported every frame. `Acquire` with another size resizes it (the GPU goes idle once); a failed create or resize backs off (120 frames, doubling) and a different size retries at once. A resize is refused while the view runs pipelined. A change between depth-only and color logs one error. `Trim(frame)` shrinks an existing target to 4x4 to free memory; the next `Acquire` of the full size grows it back. |
| `fx::PersistentVolume{base, size, format}` | 3D texture (storage and sampled) per view scope, imported every frame and kept alive. Its contents carry over only if a pass rewrites them. |
| `fx::ViewOutput{suffix}` | Engine-owned texture per view named `GameView<suffix>` / `SceneView<suffix>`, resized on demand (the GPU is waited for first). It is what the editor panel and the HTTP endpoints show once exposed with `ExposeAsViewOutput`. A later exposure of the same view replaces an earlier one. |
| `fx::GpuParams<T>` | CPU-written parameter block; its size is fixed by the first use. |
| `fx::PersistentBuffer{base, use}` | Growable GPU buffer per view scope, named `<base>.GameView` / `<base>.SceneView`. `Acquire(frame, bytes)` imports it once per frame and keeps it alive; the allocation is a power of two (min 256 B, max 128 MiB) and its contents are undefined after a create or grow. `Upload()` needs `HostWrite`. Ask `f.PassEnabled()` before `Acquire()`. Destroy it only while the GPU is idle. |
| `fx::GpuBufferRef` | Graph buffer handle stamped with the frame and view, like `fx::Texture`. |
| `fx::GpuReadback<T>` | Small GPU-written value (`T` trivially copyable, multiple of 4 bytes, at most 256) the CPU reads two frames later. Ring of 3 cells per view. Declare `f.ClearReadback(name, r, fill)`, a `Compute(...).BindOutput("var", r)` pass, then `f.CopyReadbackToHost(name, r)`. `r.Latest(f)` returns the value recorded two frames ago and its frame, or nothing (first frames, a pass toggled off, a refused allocation). Declare all three passes together or none. |
| `fx::ComputeShader{"Name"}`, `fx::FullscreenShader{"Name"}` | Load `shaders/<Name>.comp.spv`, or `.vert` and `.frag`. Built lazily; a failed build retries on a cooldown and never throws. |
| `fx::SceneShaderSet::DepthOnly("Base")` | Per-vertex-layout depth pipelines; registered pipelines are removed on destruction. |
| `fx::Channel<T>{"name"}` | Per-view hand-off between callbacks of one feature. The kit prefixes the feature name, so the key is `<Feature>.<name>`; do not repeat the feature in the channel name. A value published by a `PreOpaqueShared` callback is visible to every view of that frame. |
| `fx::SceneSlot` | Slot in the scene-services set, declared in `Setup()` with `DeclareSceneSlot(name, kind, index)`. |

## Buffers

`BindBuffer` binds a graph buffer by shader variable name; the graph orders it, barriers it and keeps it alive.

1. Mark every storage block `readonly` or `writeonly` when it is only read or only written. A block with no qualifier is
   read AND written (needed for atomics) and costs one more barrier.
2. Give every block an instance name. The instance name is the binding name (`} counter;` -> `"counter"`).
3. Binding is whole-buffer. `.length()` is the allocation, not your element count. Pass the count in a push constant.
4. Contents are undefined after the first `Acquire()` and after a grow. Write what you read, or `FillBuffer()` first.
   `FillBuffer` clears the size you asked for (rounded up to 4 bytes), not the padding behind it.
5. `Acquire()` once per callback per buffer; call `f.PassEnabled("Your.Pass")` BEFORE it. A second `Acquire()` in the
   same callback returns the same buffer and refuses a larger size.
6. Never read a buffer early and write it later in one frame. Use two `PersistentBuffer` objects and alternate them
   by `f.FrameIndex() & 1` (like the history textures of the AO feature).
7. A buffer written in a later stage cannot be read in an earlier stage of the same frame. A post-stage feature needs no
   scene color or depth read for its own passes (see "Scene reads" above).
8. `FillBuffer` of a host-visible buffer needs `BufferUse::TransferDst` in its flags.
9. GPU-written, GPU-read buffers should be device-local (no `HostWrite` / `HostRead`). Use `HostWrite` only for small
   CPU-written tables; the GPU reads host-visible memory more slowly.
10. Give every `Compute` and `FillBuffer` call its own literal pass name. The same name twice in one view and frame drops
    the second one (and asserts in debug builds).
11. A dispatch needs at most 65535 work groups per axis. For more elements dispatch over two axes
    (`Extent2D{ width, rows }`) and index with `y * width + x`.
12. If `FillBuffer` returns false, return without dispatching: the clear pass can be toggled off while the dispatch pass
    is on, and an atomic counter would then add onto stale data.

```cpp
fx::PersistentBuffer m_items{ "Count.Items", fx::BufferUse::Storage | fx::BufferUse::HostWrite };
fx::PersistentBuffer m_counter{ "Count.Counter", fx::BufferUse::Storage };
fx::ComputeShader m_shader{ "CountItems" };

void CountPass(fx::PreOpaqueFrame& f)
{
    if (!f.PassEnabled("Count.Dispatch") || !f.ShaderReady(m_shader)) {
        return;
    }
    const fx::GpuBufferRef items = m_items.Acquire(f, m_itemBytes);
    const fx::GpuBufferRef counter = m_counter.Acquire(f, sizeof(std::uint32_t));
    if (!items.IsValid() || !counter.IsValid()) {
        return;
    }
    if (!m_items.Upload(f, m_itemData.data(), m_itemBytes) || !f.FillBuffer("Count.Clear", counter, 0u)) {
        return;
    }
    f.Compute(m_shader, "Count.Dispatch")
        .BindBuffer("items", items)
        .BindBuffer("counter", counter)
        .Push(m_itemCount)
        .Dispatch(Extent2D{ m_itemCount, 1 });
}
```

The counter is a graph output (`Acquire` keeps it), so the fill and the dispatch are never culled, even when no other
pass reads the counter this frame.

## Worked examples

`src/Features/Shadow/ShadowFeature.cpp`: cascaded shadow maps (see `shadow-mapping.md`). `Setup()` declares four
registrations. `Shadow.Cascades` (`PreOpaque`) fits up to four cascades and draws each with `SceneDraw` (plus
`CullToFrustum`) into its own `OffscreenTarget`, resized when the setting changes and trimmed when unused; it publishes
the maps through a `Channel`. `Shadow.DepthRange` (`PostOpaque`) measures the visible depth range into an
`fx::GpuReadback`. `Shadow.Mask` (`PostOpaque`) is a `Compute` over `PersistentTarget`s. `Shadow.Composite`
(`PostComposite`) is a `Fullscreen` into the private target. `ShadowDebugEndpoint` exposes the settings over HTTP.

`src/Features/SceneLighting/SceneLightingFeature.cpp`: one `PreOpaque` registration with `Gate::Always()` that writes the
sun, the ambient and the cascade shadow data (read from the Shadow `Receiver` channel with `fx::ChannelReader`) into the
scene uniform block (`SetSceneUniforms`) for every view. It also reads the `Atmosphere.SunLight` channel and scales the sun
and ambient with it. It declares no pass.

`src/Features/Atmosphere/AtmosphereFeature.cpp`: six registrations.

- `AtmosphereSharedLut` (`PreOpaqueShared`): transmittance and multi-scattering LUTs as shared `PersistentTarget`s,
  published through a `Channel` that every view reads.
- `AtmosphereViewLut` (`PreOpaque`): per view a sky-view LUT (`PersistentTarget`), written by `Compute`.
- `Atmosphere.SunLight` (`PreOpaque`, `Gate::Always`): publishes the `SunLightModulation` channel for the scene lighting
  and the light shafts.
- `AtmosphereAerialVolume` (`PostOpaque`, priority 20): reads the shared LUTs and the Shadow `Receiver` channel
  (`fx::ChannelReader`), writes a per-view aerial perspective `PersistentVolume` (128x128x32, RGBA16F) and a shaft delta
  `PersistentTarget` with one `Compute`, and publishes both on the `AerialVolume` channel with a keep list.
- `DrawSkyBackground` (`PostOpaque`): `ViewBackground` into the view color where depth is untouched.
- `AtmosphereComposite` (`PostComposite`): a `Compute` into a `ViewOutput` that reads the `AerialVolume` channel,
  `ExposeAsViewOutput`, then a `Blit` to the private target.

`src/Features/ToneMapping/ToneMappingFeature.cpp`: the final `PostComposite` feature (priority +1000, `fx::Blend::Replace`),
the display transform of the frame. `Setup()` registers a `PreOpaque` frame stamp (`Gate::Always()`) and the composite.
The composite reads `f.SceneColor()` (the running linear HDR result of the earlier features), runs one `Compute`
(`ToneMapping.comp`: exposure, saturation, curve, sRGB encode, dither) into a `ViewOutput` (RGBA8_UNorm,
`GameViewToneMapped` / `SceneViewToneMapped`), calls `ExposeAsViewOutput` and `Blit`s the result into the private target.
`Replace` makes the compositor write it over the view target. The last exposure of a view wins. If a pass is toggled off,
nothing is declared and the view keeps raw linear color. The CPU reference is `src/Math/ToneMapCurves.h`; a test keeps its
constants equal to the shader text. Use this feature as the template for a screen-space effect that must run after all others.

`DeclareSceneSlot` first production user: the Shadow feature declares four `Depth2D` slots with preferred indices 0..3 and
publishes each cascade map with `PublishToScene`. Material shaders read them at `layout(set = 1, binding = c)`. The kit sets the
depth flag from the slot kind; never set it by hand.

## Adding a verb

If a feature needs something the kit lacks, add a verb to the kit, in its own small `.cpp`. Never hand a feature a raw
handle or a `Native()` escape.

For the human: add a summary link to this page and to `render-pass-api.md` in `AGENTS.md` (AI agents must not edit
that file).
