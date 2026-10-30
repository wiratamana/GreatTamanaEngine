# Feature kit reference
> Look-up tables for `gte::fx`, the Vulkan-free kit that render features are written against.
> Verified against code: 2026-10-30

## TL;DR
- Include `src/Core/FeatureKit/FeatureKit.h`. It is the one umbrella header.
- A feature registers callbacks in `Setup()`. Each callback gets a typed `Frame` and declares passes with verbs.
- Textures and buffers are stamped with the frame and view. A copy kept from an older frame is refused.
- A verb whose pass is toggled off declares nothing and returns `false` or an inert chain.
- How to build a feature step by step: [Writing a render feature](../guides/writing-a-render-feature.md).

## Setup verbs (`fx::FeatureSetup`)

| Verb | Meaning |
|---|---|
| `PreOpaque / Opaque / PostOpaque / PostTransparent(name, priority, callback, gate)` | Per-view callback in that stage |
| `PostComposite / PreUI(name, priority, blend, callback, gate)` | Per-view callback with a private target blended in |
| `PreOpaqueShared(name, priority, callback, gate)` | Once per frame. Scope is Shared. Never view-gated |
| `DefaultSecondaryViewEnabled(bool)` | Registrations added after the call start off (or on) in the Scene view. Not for Shared or `Always()` ones |
| `ReadsChannelInEmptyViews(registration, owner, channel)` | The registration needs the channel even in a view with no surface (it shades empty space) |
| `DeclareSceneSlot(name, kind, preferredIndex)` | Declares a material-shader slot. Startup only. Returns an invalid slot on refusal |
| `Fail(message)` | Records an error. The whole `Setup()` then fails and nothing is registered |

Blend (`fx::Blend`): `Replace` writes over the view (ToneMapping, last). `AlphaOver`, `Additive`, `Multiply` are normal blends. `ScreenSpaceMask` is for mask-style effects (AO, shadow darkening, GI).

`Gate` is the list of pass names a registration answers to. `Gate::Always()` is never silenced. An empty gate uses the registered name.

## Frame types

| Type | Stage | Extra members |
|---|---|---|
| `PreOpaqueFrame` | `PreOpaque` | none |
| `OpaqueFrame` | `Opaque` | `SceneTarget()` color + depth (load, never clear), `SceneDepth()` |
| `PostOpaqueFrame` | `PostOpaque`, `PostTransparent` | `SceneColor()`, `SceneDepth()`, `ViewBackground()` |
| `CompositeFrame` | `PostComposite`, `PreUI` | `PrivateTarget()`, `Extent()`, plus `PostOpaqueFrame` members |
| `SharedFrame` | `PreOpaqueShared` | none. `View()` is empty and `Scope()` is `Shared` |

`fx::View` (`f.View()`) holds `scope`, `extent`, `eye`, `viewProjection`, `invViewProjection`, `invViewProjectionValid`.

## Frame verbs (on every frame)

| Verb | Returns | Notes |
|---|---|---|
| `PassEnabled("Pass")` | `bool` | False while the pass is toggled off. Ask before any side effect |
| `Compute(shader, "Pass")` | `ComputeChain` | One dispatch |
| `Fullscreen(shader, "Pass")` | `FullscreenChain` | One fullscreen draw |
| `SceneDraw("Pass")` | `SceneDrawChain` | A scene redraw: depth pre-pass, shadow map, outline |
| `ClusterDraw(state, lane)` | `ClusterDrawChain` | GPU-culled draw of cluster meshes. Inert in a Shared frame and during a capture |
| `FillBuffer("Pass", buf, value)` | `bool` | Clears `byteSize` rounded up to 4. False: nothing declared, so do not dispatch |
| `Blit("Pass", src, dst, filter)` | `bool` | Scaled copy. `BlitFilter::Linear` or `Nearest` |
| `Keep(texture)` | void | Keeps an output nobody reads |
| `ClearReadback`, `CopyReadbackToHost` | `bool` | Passes of the `GpuReadback` flow |
| `ExposeAsViewOutput(output, tex)` | void | Call only after the writing pass was declared |
| `PublishToScene(slot, tex)` | void | Feeds material shaders. See scene slots |
| `SetSceneUniforms(value)` | void | `T` trivially copyable, at most 512 bytes |
| `ShaderReady(computeShader)` | `bool` | Builds the shader now if needed. False while a failed build cools down |
| `FrameIndex()` | `uint64` | Same for both view scopes of one frame. Use it for "N frames ago" |
| `DeltaSeconds()` | `float` | Real seconds, clamped to 0.25. Not paused by playback pause. Never divide by it |
| `PrimaryCamera(fallbackAspect)` | `optional<View>` | Game camera, even when the Game view does not render this frame |
| `ViewCaptureArmed()` | `bool` | True in the Game view while its frame is captured. Takeovers are refused then |
| `ReportOnce`, `ReportWarningOnce` | void | Log once per distinct text |

Also on `Frame`: `Owns(texture)`, `Stamp(handle)`, `ChannelHasEmptyViewReaders(channel)`, `CullSceneDraw(vp, padding)`, `LastSceneDrawCullCounts()`. The cluster queries are `ClusterCandidates()`, `ClusterCandidateBounds()`, `ClusterCandidateEntities()`, `ClusterCandidateUnion()`, `ClusterRejections()`, `ClusterPathSupported()`.

## Chains

Every chain ends with a terminal call. A refused call makes the terminal declare nothing. Forgetting the terminal asserts in debug.

| Chain | Calls | Terminal |
|---|---|---|
| `ComputeChain` | `Bind(name, tex)`, `Bind(name, fx::Depth(tex))`, `BindBuffer(name, buf)`, `Push(T)`, `Params(gpuParams, T)`, `Params(binding, gpuParams, T)`, `BindOutput(name, readback)` | `Dispatch(extent[, depth])` |
| `FullscreenChain` | `Bind`, `Push`, `Acknowledge(depth)`, `Target(tex[, clear])`, `DepthTest(tex, compare, write)` | `Draw()` |
| `SceneDrawChain` | `Shaders(set)`, `Camera(vp)`, `ClearDepthTarget(tex, v)`, `ColorTarget(tex)`, `CullToFrustum(padding)`, `Exclude(set)` | `Record()` |
| `ClusterDrawChain` | `Instances`, `Camera`, `CullCamera`, `Culling`, `TwoPhase(hiz, depth)`, `ReuseSurvivors()`, `Target` or `DepthOnlyTarget`, `IdTint`, `Stats` | `Plan()` (optional), `Record()` |

- Variables are bound by name. Names must be literals. Access (read or write) comes from the shader.
- `Fullscreen` acknowledges scene depth by default. `DepthCompare` is `Less`, `LessEqual`, `Equal` or `Always`.
- `FullscreenShader{base, blend}` takes `Opaque`, `AlphaBlend` or `Additive`.
- `CullToFrustum` skips draws whose bounds miss the camera. Meshes without bounds are never skipped.
- `Record()` of a cluster chain also frees idle buffers. Call it every frame. A callback that skips the chain calls `state.TickIdle(frame, false)`.
- A cluster chain that is toggled off, lacks device support or is refused declares nothing and claims no entity. More in [GPU-driven rendering](../architecture/gpu-driven-rendering.md).

## Resource types

| Type | Lifetime | Key facts |
|---|---|---|
| `PerView<T>` | Feature | One `T` per view scope. `For(scope)` |
| `PersistentTarget{base}` | Persists | Color texture per view. Graph name `<base>.GameView` or `<base>.SceneView`. A resize applies one frame late |
| `PersistentVolume{base, size, format}` | Persists | 3D texture per view. Size fixed. Contents do not carry over. See [Volumetric resources](../conventions/volumetric-resources.md) |
| `PersistentBuffer{base, use}` | Persists | Per view. `Acquire(f, bytes)`, `Upload`, `Release`, `Capacity`, `Generation`. Contents undefined after create or grow |
| `OffscreenTarget{base}` | Persists | A render view per scope: `DepthOnly{res}` or `ColorDepth{w,h,format}`. `Acquire`, `Trim`, `Extent` |
| `TransientTarget{base}` | One frame | From the graph pool. Pool recycles by exact size and format. Prefer `PersistentTarget` for view-sized data |
| `ViewOutput{suffix}` | Persists | Engine-owned texture per view, shown by panels and HTTP. See view output |
| `GpuParams<T>` | Feature | CPU-written parameter block. Size fixed by first use |
| `GpuReadback<T>` | Feature | Small GPU value the CPU reads 2 frames later. See below |
| `HizPyramid{base}` | Feature (atlas lasts one frame) | Farthest-depth pyramid of the scene depth. `Plan`, `Build` |
| `ClusterDrawState{base, shape}` | Feature | Buffers and names of a cluster draw. Destroy only while the GPU is idle |
| `SceneShaderSet::DepthOnly("Base")` | Feature | Depth pipelines per vertex layout. Needs `Base<suffix>.vert` and `Base.frag` |
| `ComputeShader{"Name"}` | Feature | Loads `shaders/Name.comp.spv`. Built lazily. A failed build retries on a cooldown, never throws |
| `FullscreenShader{"Name"}` | Feature | Loads `Name.vert` and `Name.frag` |
| `Channel<T>{"name"}` | Frame | Hand-off between callbacks. See channels |

- Textures, buffers and shaders are feature members. They are neither copyable nor movable.
- Failed creation of a kit resource is logged once and retried on a cooldown. `BuildRetryGate` starts at 120 frames, doubles, and stops growing at 1800.
- `PersistentVolume`, `PersistentBuffer`, `OffscreenTarget`, `ViewOutput` and `ClusterDrawState` must be destroyed only while the GPU is idle.

### `GpuReadback<T>` flow

| Step | Call |
|---|---|
| 1 | `f.ClearReadback("Pass.Clear", r, 0u)` |
| 2 | `f.Compute(...).BindOutput("result", r).Dispatch(extent)` |
| 3 | `f.CopyReadbackToHost("Pass.Copy", r)` |
| 4 | `r.Latest(f)` gives `{value, frame}` recorded 2 frames ago, or nothing |

`T` is trivially copyable, a multiple of 4 bytes, at most 256 bytes. The ring has 3 cells per view. Declare all three passes together or none.

## Channels

| Item | Meaning |
|---|---|
| `Channel<T>{"Map"}` | Publish and fetch inside one feature. Key is `<Feature>.Map`. Do not repeat the feature name |
| `ChannelReader<T>{"Owner", "Map"}` | Read another feature's channel. Both sides use the same C++ type |
| `Publish(f, value, {keepTextures})` | Stores the value for this view. Kept textures stay alive even if nobody reads them |
| `Fetch(f)` | `optional<T>`. Empty if nothing was published this frame, or the type differs (logged once) |

- A value is per view. Game view data is not seen by the Scene view.
- A value published by a `PreOpaqueShared` callback is visible to every view.
- Channels live on the render-pass blackboard. It is cleared every frame.
- `Publish` and `Fetch` outside a render callback do nothing and log once.
- Two channel names of one feature with the same key hash assert in debug and log once in release.
- A disabled pass never runs its callback, so it publishes nothing. If a toggleable pass publishes data that another toggleable pass reads, add a risk rule. See [Render honesty](../conventions/render-honesty.md).
- Use a channel for data. Use a scene slot only when a material shader must read it.

## Scene slots and uniforms

| Item | Value |
|---|---|
| Slot count | 8 (`kSceneSlotCount`). Shader binding is `layout(set = 1, binding = N)` |
| Kinds | `Image2D`, `Depth2D`, `Image3D` |
| Depth flag | Derived from the kind. Never set by hand: a wrong flag can crash the driver |
| Uniform block | `SetSceneUniforms(T)`. Binding 8. At most 512 bytes. Checked at compile time |
| Shipped use | Shadow declares 4 `Depth2D` slots, indices 0..3. Slots 4..7 are free |
| `Image3D` limit | One shared dummy for all `Image3D` slots. Sound for one `Image3D` slot only. No feature uses one |

`preferredIndex` is the shader contract. Keep it stable once shaders read it.

## View background

`PostOpaqueFrame::ViewBackground(shader, "Pass")` returns a `FullscreenChain` already set to:
- target = `SceneColor()`, loaded, not cleared;
- depth test on `SceneDepth()`, `Equal`, no depth write.

Add `Bind` and `Push`, then `Draw()`. Use it for a sky.

| Rule | Detail |
|---|---|
| Frame Debugger replay | A successful `Draw()` in the Game view publishes a replay under `Core.ViewBackgroundReplayCallback`. `Core::BuildFrame` hands it to the Frame Debugger |
| Scene view | Not published. The Frame Debugger replays the Game view only |
| Toggled off | Declares nothing and publishes nothing, so no stale replay exists |
| Textures | Graph handles. The replay reads them, so the graph orders it after their producers |

## View output

A feature texture that the editor panel and HTTP (`/get_game_view`, `/get_texture`) must read needs an engine-owned texture in shader-read state at frame end. `ViewOutput` does that.

| Call | Meaning |
|---|---|
| `ViewOutput{suffix}` | Texture name per scope: `GameView<suffix>`, `SceneView<suffix>` |
| `Acquire(f, extent, format)` | Makes or resizes this view's texture (storage-capable), imports it, keeps it alive. A resize waits for the GPU. Invalid in a Shared callback and for a zero size |
| `f.ExposeAsViewOutput(out, tex)` | Marks `tex` as this view's output. `tex` must be what `Acquire` returned in this callback |

- The last exposure of a view in a frame wins, with no error. ToneMapping runs last, so its `GameViewToneMapped` and `SceneViewToneMapped` (RGBA8) become the view's final picture.
- Only the final exposure is handed to the editor layer. A view with no exposure shows the raw view.
- The texture name is the HTTP contract. The suffix keeps names stable.

## PostComposite chain

For each view, the compositor copies the view target into a seed. Then, for each enabled entry in priority order:
1. It gives the entry a private RGBA16F target and runs its callback.
2. The callback sees the running result as `SceneColor()`.
3. One blend pass mixes the private target into the running result.

The last blend writes the real view target. A callback that declares nothing is skipped. Each entry costs about two full-size RGBA16F targets per view. Today's order by priority: Ssgi -1600, Gtao -1500, Atmosphere -1000, Shadow -500, VolumetricLight -250, ToneMapping +1000.

## Buffer rules

| # | Rule |
|---|---|
| 1 | Mark every storage block `readonly` or `writeonly` if it only reads or writes. No qualifier means read and write, and costs one more barrier |
| 2 | Give every block an instance name. The instance name is the binding name (`} counter;` binds `"counter"`) |
| 3 | Binding is whole-buffer. `.length()` is the allocation, not your count. Pass the count in a push constant |
| 4 | Contents are undefined after the first `Acquire()` and after a grow. Write what you read, or `FillBuffer()` first |
| 5 | `Acquire()` once per callback per buffer, after `f.PassEnabled()`. A second `Acquire()` returns the same buffer and refuses a larger size |
| 6 | Never read a buffer early and write it later in one frame. Use two buffers and alternate with `FrameIndex() & 1` |
| 7 | A buffer written in a later stage cannot be read in an earlier stage of the same frame |
| 8 | `FillBuffer` of a host-visible buffer needs `BufferUse::TransferDst` |
| 9 | GPU-written, GPU-read buffers should be device-local (no `HostWrite` or `HostRead`). Use `HostWrite` only for small CPU tables |
| 10 | Each `Compute` and `FillBuffer` call needs its own literal pass name per view and frame. A repeat is dropped (assert in debug) |
| 11 | A dispatch has at most 65535 groups per axis. For more, dispatch 2D and index with `y * width + x` |
| 12 | If `FillBuffer` returns false, return without dispatching. The clear pass can be off while the dispatch pass is on |

## Helpers

| Header | Content |
|---|---|
| `EnumTable.h` | `EnumName`, `ParseEnum`, `EnumKnown` for debug endpoints |
| `FeatureDebug.h` | `IFeatureDebugEndpoint`, `ParseDebugBool/Unsigned/Float` |
| `SnapshotAge.h` | `SnapshotIsStale(frame, newest)`: true after 2 frames without a render |
| `ScreenSpaceView.h` | `DeriveCameraBasis`, `TemporalHistory`, `HistoryUsable`, `TemporalResolveParamsGpu`, `ScaledExtent`. Shared by Gtao and Ssgi |
| `SharedOncePerFrame.h`, `OncePerFrame.h` | Once-per-frame guards |

## Related
- [Writing a render feature](../guides/writing-a-render-feature.md) — steps to add a feature
- [Render features](render-features/README.md) — the shipped features and their stages
- [Render graph](../architecture/render-graph.md) — pass kinds, toggles, recording context
- [Render honesty](../conventions/render-honesty.md) — toggle and side-channel rules
- [Volumetric resources](../conventions/volumetric-resources.md) — 3D textures
- [GPU-driven rendering](../architecture/gpu-driven-rendering.md) — the cluster draw
