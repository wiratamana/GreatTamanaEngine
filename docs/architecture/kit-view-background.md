# View background and its replay

A feature may draw a fullscreen shader into the view's own color and depth, only on pixels no geometry touched
(for example a sky). The Frame Debugger must be able to draw the same background again inside its replay pass.

## Kit verb

`PostOpaqueFrame::ViewBackground(FullscreenShader&, StaticName passName)` returns a `FullscreenChain` that is already set up with:

- target = `SceneColor()`, loaded, not cleared;
- depth test on `SceneDepth()`, compare `Equal`, write off;
- the replay hook armed.

The author adds `Bind(...)` and `Push(...)` like any fullscreen draw, then calls `Draw()`.
`FullscreenChain::DepthTest(Texture, DepthCompare, bool write)` is the public form of the depth test.

## Replay value

`rg::FullscreenReplay` (`src/Renderer/RenderGraph/FullscreenReplay.h`) holds two functions built from the validated draw:

| Member | Purpose |
|---|---|
| `declare(PassBuilder&)` | Declares the shader bindings only. The host pass declares its own attachments. |
| `record(PassContext&)` | Binds the pipeline, the bound set and the push constants, then draws 3 vertices. |

`rg::FullscreenPass::Draw(FullscreenReplay* replay = nullptr)` fills it when the pass was declared.
The pass's own execute function and the replay share one record function, so they cannot drift apart.

## Publishing

On a successful `Draw()` of a view background in the Game view, the chain publishes the value on the blackboard under
`kViewBackgroundReplayCallbackKey`. A disabled pass declares nothing and publishes nothing, so no stale replay exists.
Scene view backgrounds are not published: the Frame Debugger replays the Game view only.

## Consumer

`Core::BuildFrame` fetches the value and passes it to `IFrameDebuggerCaptureRecorder::AddReplayPasses`.
The background replay step calls `declare(pass)` in its setup and `record(ctx)` in its execute, after the scene draws.
The replay targets have the formats of the Game view target, so the pipeline built for the real draw fits them.

## Rules

- The shader textures are graph handles; the replay pass reads them, so the graph orders it after their producers.
- No Vulkan type and no `CommandBuffer` crosses into feature code.
- Reading a texture by name from the graph is not allowed; hand it over with a `Channel`.
