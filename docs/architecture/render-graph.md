# Render graph
> The `gte::rg` API: how passes, shaders, buffers and recording work. For anyone who writes or changes render code.
> Verified against code: 2026-10-30

## TL;DR
- Every frame, code declares passes into an `rg::RenderGraphBuilder`. The graph orders, culls, syncs and records them.
- Pass code never names Vulkan. It binds resources by name and ends each chain with a terminal call.
- The graph owns descriptor sets, layouts, pipelines and barriers.
- `rg::OneShot` runs the same chains outside the frame loop. `rg::GpuBuffer` is the RAII device buffer.
- Code outside `src/Renderer/` holds a Vulkan-free `rg::RecordingContext`, never a command buffer.

## Diagram
```text
 provider / feature callback
        | declare (Compute, Fullscreen, Mesh, Transfer, Blit ...)
        v
 RenderGraphBuilder  --  textures, buffers, passes, toggles
        | Compile(): order by data dependency, cull, lifetimes
        v
 Barrier planner (sync2)  +  ResourcePool  +  descriptor pools
        | record
        v
 RecordingContext -> CommandBuffer -> GPU      (raw handle: src/Renderer only)
```

## How it works
1. A provider or feature callback declares passes. Each pass lists what it reads and writes.
2. `Compile()` sorts passes by data dependency. Ties go by `RenderPassEvent`, then by declaration order.
3. It culls every pass that reaches no root. Roots are final outputs, `Keep*Output()` sets and persistent textures.
4. It computes the lifetime of each resource. Pooled textures and buffers come from `RenderGraphResourcePool`.
5. The barrier planner adds sync2 barriers. Descriptor sets come from the renderer's compute pool set. It adds a pool when needed.
6. Execute lambdas run in order and record into the command buffer.

A disagreement between event and real dependency is logged (assert in debug). The event is a tag. It must agree with the real dependencies.

## Concepts
| Word | Meaning |
|---|---|
| `ViewScope` | `Shared`, `Primary`, `Secondary`. Primary is the Game view. Secondary is the Scene view. |
| `RenderPassEvent` | Order tag: `BeforeEverything` 0, `PreOpaques` 1000, `Opaques` 2000, `AfterOpaques` 2500, `Transparents` 3000, `AfterTransparents` 4000, `AfterEverything` 9000 |
| `PassKind` | Low-level kind: `Graphics`, `Compute`, `Blit`. The pass verbs below build these. |
| Pass name | A string literal (`rg::StaticName`). `Interned()` is the one explicit escape. |
| Toggle | On/off state per pass name in `RenderPassToggleRegistry`. See [Render honesty](../conventions/render-honesty.md). |
| Blackboard | Per-frame key/value store. Providers publish and fetch data between passes. Cleared each frame. |
| Persistent resource | Survives frames. Owned by the feature, not by the pool. |
| Pool eviction | Pooled textures and buffers idle for `kPoolStaleFrames` (120) frames are freed. Their debug snapshots go too. |

## Pass verbs
Each verb is a pure record plus a chain. The chain must end with a terminal call. If you forget it, debug builds assert and release builds log once.

| Verb | Chain | Notes |
|---|---|---|
| Compute | `ComputePass(...).Bind().Params().Dispatch()` | Max 65535 work groups per axis. Larger is refused. Use 2D dispatch. |
| Fullscreen | `FullscreenPass(...).Bind().Push().Target()/Targets({...}).DepthTest().Draw(replay)` | `Targets({})` is MRT. `Draw(FullscreenReplay*)` also fills a replay record. |
| Mesh | `MeshPass(...).Color()/Depth().Draw(fn)` | Scene draws use `gte::SceneDrawPass` in `src/Game`, a thin wrapper. |
| Indirect draw | `MakeIndirectDrawBody` | Validated setup/execute pair |
| Transfer | `MakeTransferBody` | Fill, CopyBuffer, Readback, CopyTexture, Clear. Textures need `TransferSrc` / `TransferDst`. |
| Blit | `AddBlitPass` with `BlitFilter` (`Linear`, `Nearest`) | Raw image copy. Texture needs transfer usage. |

Attachment formats come from the declared textures (`DescOf`). Never use `ColorPixelFormat()` for them.

## Shaders and binding
- `rg::Shader` loads `shaders/<base>.comp.spv`, or `<base>.vert.spv` and `.frag.spv`. Paths resolve against the exe directory only.
- It reflects names, types, access, stages and the push block once. It owns layouts and pipelines (RAII).
- Build: `glslc` must be on `PATH`. Output goes to `build/shaders/` next to the exe.
- A failed shader build never throws into the frame. It logs one warning, retries on a growing cooldown, and logs once on recovery.

Bind by name: `.Bind("sourceColor", tex)`. Access comes from the GLSL qualifiers. `readonly` and `writeonly` are respected. No qualifier means read-write, which costs an extra barrier.

| Error (loud, lists valid names) | Cause |
|---|---|
| Unknown name | The name is not in the shader |
| Bound twice / never supplied | Binding missing or duplicated |
| Wrong type | Resource kind does not match the descriptor |
| Push size mismatch | C++ struct size differs from the push block |
| Depth bound without `rg::Depth()` | Depth textures must be sampled through `rg::Depth()` |

- Descriptor types: combined image sampler, storage image, storage buffer, uniform buffer.
- A storage buffer binds as `BufferObjectHandle` (external, not tracked) or `rg::BufferHandle` (graph buffer: tracked, whole-buffer only).
- `TextureDesc::hasColor` is false only for depth-only textures. A view target (color plus depth) is a color texture and can also be sampled through `rg::Depth()`.

## Buffers and descriptors
`rg::GpuBuffer` owns a device buffer. It is made from `rg::DeviceRef`, which `Renderer::Device()` hands out. There is no global renderer lookup.

| Item | Rule |
|---|---|
| `GpuBufferUse` | `Storage`, `Uniform`, `Indirect`, `Vertex`, `TransferSrc`, `TransferDst`, `HostWrite`, `HostRead` |
| `Upload` / `Download` | Need `HostWrite` / `HostRead`. A bad range or wrong use returns false and logs once. |
| `CreateWithData` | Device-local buffer filled by a staging upload |
| `builder.Import(gpuBuffer)` | Makes a graph buffer. Import once per frame and bind the returned handle. |
| `gte::Buffer` | Stays inside `src/Renderer/` |
| Descriptor sets | One set per pass name, view scope, layout and instance. Unused sets are freed after 600 frames. A warning logs above 75% pool use. Stats show in the Memory panel. |
| Failed set allocation | Logged once. The key backs off. The pass gets an invalid set. |

## One-shot execution
`rg::OneShot os(renderer)` imports textures, builds chains on `os.Builder()`, and `os.Run()` submits and waits. Use it for editor previews and one-time work. Never per frame.

- It supports compute and transfer passes. A pass with attachments, or a blit, is skipped with one logged error.
- Each import has a `FinalState`: `Restore`, `ShaderRead` or `Keep`.
- `ImportReadOnly(texture, state)` states the state of a texture whose tracked state is unknown.
- Tracked states are written back only after the wait succeeded.
- It has no GPU timing, no snapshots and no persistent resources. Snapshots would show up in `/list_textures`.
- Descriptor sets are allocated per run and freed after the wait.

## Recording context
`rg::RecordingContext` is a Vulkan-free handle. It holds an opaque native pointer, the `Renderer*`, and the current `PassContext*`.

| Member | Purpose |
|---|---|
| `Commands()` | Returns a `CommandBuffer`. Inside a pass it carries the pass draw stats and the Frame Debugger sink. |
| `PlatformHandle()` | Opaque pointer for the ImGui Vulkan glue. Only `Editor/ImGuiEditorLayer.cpp` may call it. |
| `operator bool` | True when a command buffer is held |

- `PassContext::Recording()` and `Renderer::BeginOffscreenRecording().Context()` make one.
- Only `src/Renderer/` opens the raw handle, through `rg::RecordingAccess`.
- A context is valid only inside the callback that got it. Never store it.
- Drawing through `Commands()` needs an open rendering scope. `rg::SceneRecording` opens it for scene draws.

## Formats and targets
| Item | Rule |
|---|---|
| `ColorFormat()` | Swapchain format, negotiated at runtime. Only ImGui, the final blit and the mesh preview use it. |
| `ScenePixelFormat()` | `RGBA16_Float`. View targets, direct primary target, scene pipelines, scene grid, compositor targets. |
| `CreateRenderTexture(format = Undefined)` | Means swapchain format. `Core::CreateRenderView` turns `Undefined` into the scene format. |
| `DepthFormat()` | Picked from the device once. Every pipeline and every render target uses it. |
| `FrameRecorder::RecordFrame` | Asserts (debug) that color and depth formats match the target. Do not weaken it. |
| Barrier state | Read `CurrentState()`, write `SetCurrentState()`. Never guess. Import with `trackedOwner`. |
| `ClusterViewRenderer` | Uses explicit `RGBA8_UNorm`. The swapchain format does not matter to it. |

A target with a different format (for example a shadow map) needs its own pipeline built for that exact format.

## Rules
1. Pass names are string literals.
2. A per-view pass uses `Primary` or `Secondary` scope, never `Shared`. A shared pass would share one descriptor set between views.
3. Execute lambdas capture by value. A chain object is a temporary.
4. Never log or throw from a frame path on every frame. Use `OnceErrorLog`, `BoundedOnceSet<N>` or `BuildRetryGate`.
5. Every CPU wait on the GPU goes through `GpuWaitService`. `BlockingCallCheck` enforces it.
6. A `BlockOutcome` other than `Completed` means the GPU may still use the resource. Do not free it.

## Gotchas
- A pass whose output nobody reads is culled silently. Publish with a keep list or call `Keep*Output()`.
- A pass that skips a binding logs one error per pass and binding, and asserts in debug.
- The raw command buffer does not exist outside `src/Renderer/`. `NativeLeakCheck` forbids `RecordingAccess` and `PlatformHandle(` elsewhere.
- Same pass name in two views makes two timing rows.

## Key files
| File | Role |
|---|---|
| `src/Renderer/RenderGraph/RenderGraphBuilder.h` | Declare passes and resources |
| `src/Renderer/RenderGraph/RenderGraphCompiler.h` | Order, cull, lifetimes |
| `src/Renderer/RenderGraph/RenderPipeline.h` | Providers, phases, blackboard |
| `src/Renderer/RenderGraph/Shader.h` | Shader load, reflection, retry |
| `src/Renderer/RenderGraph/OneShot.h` | One-shot runner |
| `src/Renderer/RenderGraph/GpuBuffer.h` | RAII device buffer |
| `src/Renderer/RenderGraph/RecordingContext.h` | Vulkan-free recording handle |
| `src/Renderer/RenderGraph/RenderPassToggleRegistry.h` | Pass toggles |

## Related
- [Rendering](rendering.md) · [Render honesty](../conventions/render-honesty.md)
- [Feature kit](../reference/feature-kit.md) · [Writing a render feature](../guides/writing-a-render-feature.md)
- [Frame Debugger](../debugging/frame-debugger.md) · [Memory tracking](../conventions/memory-tracking.md)
- [AGENTS.md](../../AGENTS.md#vulkan-boundary-strict)
