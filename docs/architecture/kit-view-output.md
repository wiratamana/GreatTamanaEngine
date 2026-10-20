# Exposed view output

A feature produces a texture the editor panel and the HTTP endpoints must read (`/get_game_view`,
`/get_texture?texture_name=GameViewComposited`). Those readers need an engine-owned `RenderTexture`
in shader-read state at the end of the frame.

## Kit types

`fx::ViewOutput` (`src/Core/FeatureKit/ViewOutput.h`) is a feature member, like `PersistentTarget`.

| Call | Meaning |
|---|---|
| `ViewOutput(suffix)` | Texture name per view scope: `GameView<suffix>`, `SceneView<suffix>`. |
| `Texture Acquire(Frame&, Extent2D, PixelFormat)` | Makes or resizes this view's `RenderTexture` (storage-capable), imports it with its state tracked, keeps it alive. A resize waits for the GPU first. |
| `Frame::ExposeAsViewOutput(ViewOutput&, Texture produced)` | Marks the texture as this frame's output of the view. Call it only after the pass that writes it was declared. |

`produced` must be the texture `Acquire` returned in this callback; anything else is refused with one log.

## Engine side

`Core::ExposeViewOutput(ViewScope, RenderTexture&, name)` records the exposure for the frame. A second call for the same view
replaces the first.
After the offscreen graph ran, `Core::BuildFrame` for every view's final exposure:

1. finalizes the texture for sampling on the offscreen recording context;
2. tells the graph its debug state is shader-read (`NotifyDebugTextureStateOverride`);
3. hands it to the editor layer (`SetGameViewCompositedTexture` / `SetSceneViewCompositedTexture`).

A view with no exposure this frame gets `nullptr`, and the panel falls back to the raw view.
Because the feature exposes only after a successful declaration, "exposed" means "written this frame";
no snapshot lookup by name is needed.

## Removed

`Core::RegisterFinalizeForSamplingHook` and the finalize hook list are deleted; `ViewOutput` is the only route.

## Rules

- The last exposure of a view in a frame wins: a later `ExposeViewOutput` of the same view replaces an earlier one, with no error.
  ToneMapping runs last in the PostComposite chain, so its `GameViewToneMapped` / `SceneViewToneMapped` (RGBA8_UNorm, final
  display values) is the exposed "composited" texture of the view when the feature is on.
- Only the final exposure of a view is finalized and handed to the editor layer. Each composite pass is still found by the
  texture name it writes (Frame Debugger).
- The texture name is the HTTP contract; the suffix keeps these names stable.
- The engine owns exposure bookkeeping; the feature never sees a `RenderTexture`.
- `Renderer::ResizeRenderTexture` is the only place that idles the GPU for a resize.
