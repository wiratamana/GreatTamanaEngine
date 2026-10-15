# Render pass API (`gte::rg`)

The layer under every render feature. Pass code never touches Vulkan and never names a raw wrapper outside
`src/Renderer`. This page describes what exists today.

## The six ideas

1. **One shader type.** `rg::Shader` loads `shaders/<base>.comp.spv` or `<base>.vert/.frag.spv` (relative paths
   resolve against the executable directory only), reflects every stage once (names, types, access, stages, push
   block) and owns its layouts and pipelines (RAII). A failed build never throws into the frame: one warning, a timed
   retry, one info line on recovery.
2. **Bind by name.** `.Bind("sourceColor", tex)`. Access (read, write, read-write) comes from the GLSL qualifiers
   (`readonly` / `writeonly`, none = read-write). Unknown name, name bound twice, wrong resource type, binding never
   supplied, push size mismatch, a depth texture bound without `rg::Depth()`, a depth view of a color texture and an
   invalid handle are loud errors that list the valid names. Supported descriptor types: combined image sampler,
   storage image, storage buffer, uniform buffer.
3. **The graph owns descriptor sets, layouts, pipelines and barriers.** Sets come from growable pools keyed by
   (pass, view scope, layout). Pool statistics (in use / capacity / peak per type) show in the Memory panel and in
   `/get_logs` above 75%. Exhaustion skips the pass and logs once; it never throws into the frame.
4. **Declarative pass kinds**, each a pure record plus a chain that ends in a terminal call (forgetting it asserts in
   debug and logs once in release):

   | Kind | Chain | Record |
   |---|---|---|
   | Compute | `ComputePass(...).Bind().Params().Dispatch()` | bindings, push bytes |
   | Fullscreen | `FullscreenPass(...).Bind().Push().Target()/Targets({...}).DepthTest().Draw()` | attachments, key |
   | Mesh | `MeshPass(...).Color()/Depth().Draw(fn)`; `SceneDrawPass` (Game) adds the camera and pipeline overrides | `MeshPassRecord` |
   | Indirect draw | `MakeIndirectDrawBody` (validated setup/execute pair) | `IndirectDrawRecord` |
   | Transfer | `MakeTransferBody` (validated setup/execute pair) | `TransferRecord` (Fill, CopyBuffer, Readback, CopyTexture, Clear) |
   | Blit | `AddBlitPass` with `rg::BlitFilter` | `BlitSpec` |

   Attachment formats come from the declared textures (`DescOf`), never from `ColorPixelFormat()`.
5. **One-shot execution** of the same pass chains outside the frame loop (`rg::OneShot`, see below).
6. **First-class GPU buffer handle** (`rg::GpuBuffer`): not built yet.

## Rules

- Pass names are string literals (`rg::StaticName`).
- A pass declared per view must carry `GameView` or `SceneView`, never `Shared` (a `Shared` pass would share one
  descriptor set between views).
- Execute lambdas capture values only; a chain object is a temporary.
- The raw command buffer is not public. Use `PassContext::Cmd()`, the pass kinds or `rg::SceneRecording` (RAII
  Begin/EndGraphPassRecording). The only raw routes are `PassContext::RawCommandBuffer()`,
  `CommandBuffer::RawCommandBuffer()` and `CommandBuffer::AdoptRaw()`; NativeLeakCheck lists every use outside
  `src/Renderer`.
- Transfer and blit passes on a graph texture need `TextureUsage::TransferSrc` / `TransferDst` (2D textures).
- `rg::MeshPass` lives in `src/Renderer`, which must not depend on `src/Game`. The scene draw itself (`DrawScene`, the
  pipeline override set, the camera) therefore lives in `gte::SceneDrawPass` (`src/Game`), a thin wrapper over
  `MeshPass`; the kit's `SceneDraw` wraps that.
- `TextureDesc::hasColor` is false only for depth-only textures (a depth map). Those are sampled through `rg::Depth()`
  and are never a color target or a storage image. A view target (color plus depth companion) is an ordinary color
  texture and can also be sampled through `rg::Depth()`.

## One-shot execution

`rg::OneShot os(renderer)` imports textures, takes the same pass chains on `os.Builder()` and `os.Run()` submits and
waits. It is for user-driven or one-time work (editor previews, a capture copy), never per frame.

Decision: a small executor that shares the graph's barrier planner and descriptor-write code
(`RenderGraph::RunOneShot`), not the frame loop. A real frame run also does GPU timing slots, debug texture
snapshots, event snapshots, persistent-resource bookkeeping and a shared binding cache, none of which a one-shot may
touch (the snapshots would show up in `/list_textures`).

- Supported passes: compute and transfer. A pass with attachments or a blit is skipped with one logged error.
- Imported textures keep their tracked state (`CurrentState()` on `RenderTexture` and `Texture2D`). Each import has a
  `FinalState`: `Restore` (back to the imported state), `ShaderRead` or `Keep`. Tracked states are written back only
  after the wait succeeded; a failed run leaves them unchanged and logs once.
- `ImportReadOnly(texture, state)` states the state of a texture whose tracked state is unknown.
- Descriptor sets are allocated per run and freed after the wait, so no binding-cache entry outlives the run.
- The blocking submit reuses fences from a small pool in `Renderer`.
- A volume import takes an engine sampler handle so a pass can bind it by name.

## Checks

`gte_ci_scan` runs VolkLeakCheck, NativeLeakCheck, FeaturePurityCheck and EngineVocabularyCheck (ctest); the
HeaderPurity probe compiles the Vulkan-free headers with `src` as the only include directory. Allowlists only
shrink; a stale line fails the check.
