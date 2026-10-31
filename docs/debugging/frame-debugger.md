# Frame Debugger
> An editor window and HTTP routes that freeze one Game view frame and show its render events. For anyone who debugs what a frame draws.
> Verified against code: 2026-10-31. HTTP examples are read from code, not run live.

## TL;DR
- Open it with `Window > Frame Debugger`. Turn on `Enable`. It pauses the game and captures one frame. Turn `Enable` off and the game runs again.
- It works with any tab layout. While `Enable` is on, the Game view renders every frame even if the Game tab is hidden.
- It shows the Game view only. Scene view passes never appear.
- It keeps one capture. A new capture replaces the old one. There is no history.
- Capture is on demand. The tree and images stay frozen until you capture again or disable it.
- Every capture also runs three [honesty detectors](../conventions/render-honesty.md).

## Quick start
In the editor:
1. `Window > Frame Debugger`, then tick `Enable`. A capture starts, the game pauses, the window fills in one or two frames.
2. Click an event in the left tree, or drag the frame-step slider. Left and Right keys also work.
3. Use the channel buttons (All, R, G, B, A) and the levels sliders to look at the preview.
4. Click `Capture` to capture again after you change something.

Over HTTP (port 8080, see [HTTP cheat sheet](http-cheat-sheet.md)):
```cmd
curl.exe "http://127.0.0.1:8080/frame_debugger/open"
curl.exe "http://127.0.0.1:8080/frame_debugger/enable?value=true"
curl.exe "http://127.0.0.1:8080/frame_debugger/capture"
curl.exe "http://127.0.0.1:8080/frame_debugger/state"
curl.exe "http://127.0.0.1:8080/frame_debugger/select_event?index=3"
curl.exe "http://127.0.0.1:8080/get_swapchain" -o fd.png
```
- Poll `state` until `captureStatus` is `captured`. `failed` gives a reason in `captureError`.
- `get_swapchain` shows the window with the selected preview. The `open` route pins the window onto the main ImGui viewport so it is visible.
- When done, call `enable?value=false`. The game runs again if the debugger paused it.

## Reading the data
### Routes
| Route | Effect |
|---|---|
| `GET /frame_debugger/open` | Opens the window |
| `GET /frame_debugger/enable?value=true\|false` | Same as the checkbox. `value=true` also opens the window. |
| `GET /frame_debugger/capture` | Arms a capture. Refused while not enabled. |
| `GET /frame_debugger/select_event?index=<n>` | Selects a tree event. `-1` deselects. |
| `GET /frame_debugger/set_channel?value=all\|r\|g\|b\|a` | Channel isolation |
| `GET /frame_debugger/set_levels?black=<f>&white=<f>` | Levels range |
| `GET /frame_debugger/state` | Flat JSON, no `success` key |
| `GET /frame_debugger/get_event_texture?index=<n>` | PNG of one per-draw event. `format=json` gives base64 JSON. |

- All routes except `state` and `get_event_texture` answer `{"success": ..., "state": {...}}`.
- `state` fields: `enabled`, `windowOpen`, `hasCapturedFrame`, `totalEventCount`, `selectedEventIndex`, `channel`, `levelsBlack`, `levelsWhite`, `perDrawEventCount`, `captureStatus` (`disabled`, `pending`, `captured`, `failed`), `captureError` (text, only when `failed`).
- Poll `state` until `captureStatus` is `captured`. Stop and read `captureError` if it is `failed`.
- `get_event_texture` returns 404 until that index was armed and a later frame was captured. Poll it.
- The `index` of `get_event_texture` is a per-draw index. It is not the tree `eventIndex`.
- Details and error codes: [HTTP routes (debug)](http-routes-debug.md).

### Event tree
The tree is built from the render graph snapshot by structure, not by pass name. The pivot is the first pass with `RenderPassEvent` of `Opaques` or later.

```text
Game View
  <feature name>                  surviving compute passes before the pivot, grouped by owner
    <pass> -> "Compute Dispatch"
  Compute Dispatches (Pre-GameView)   such passes with no owner
  RenderOpaque                    the first Graphics pass of the view region
    <entity name> (Entity <n>)    one child per recorded scene draw
  <pass> -> "Draw Mesh" | "Draw Quad" | "Blit"   other view-region passes, e.g. DrawSkyBackground
  Compute Dispatches (Post-GameView)  surviving compute passes after the view region
  Other Render Passes             any surviving pass no rule above claimed
```

- A group exists only when it has a child.
- Every pass except `RenderOpaque` is a parent row with one child row. Both rows can be selected.
- Culled passes, Scene view passes and `FrameDebuggerInternal` passes are skipped. Nothing else is dropped.
- `eventIndex` grows in execution order. `totalEventCount` is the next free index.
- A compute leaf shows `n/a (compute pass)` for blend, Z and stencil. It also shows `GPU Time (ms)`.
- `RenderOpaque` lists the pipelines, the bound material images, the clear color, draw stats and the `ViewProjection` matrix.
- Entity children show their pipeline, four material slots, `Triangle Count` and `Entity (Index, Generation)`.
- Group names are the feature names the graph recorded. The Frame Debugger never names a feature itself.

### Preview image
Each leaf has a preview kind. The snapshot builder sets it once.

| Kind | Leaf | Image shown |
|---|---|---|
| `NotYetDrawn` | Compute pass before the pivot | None. Text: "Nothing drawn yet at this point in the frame." |
| `PerObjectStep` | One entity child | The Game view with only objects `[0..i]` drawn |
| `PreComposite` | `RenderOpaque`, other view-region leaves, early compute leaves | `preview` |
| `PostComposite` | The composite pass, later leaves, or no selection | `compositedPreview`, else `preview` |

- The composite pass is found by the texture it writes (`GameViewComposited`), not by its name.
- A leaf whose pass writes a private resource (not `GameView`) is previewed directly from the graph debug texture snapshot.
- A feature makes the composited image with `Frame::ExposeAsViewOutput()`. `Core` passes it to the editor layer through `SetPrimaryViewCompositedTexture()`.
- Channel and Levels are applied by `FrameDebuggerPreviewProcessing`. Levels go first, then channel. `Shaders/FrameDebuggerPreview.comp` does it on the GPU.
- The retained copy is never changed. These controls are separate from `channel=color|depth` of `GET /get_texture`.
- In the `ShaderProperties` sections of the event details, a row that names a 2D or volume texture has a `View` button. Buffers, material textures and texture arrays have none.

## Recipes
### Why is this object not drawn or drawn wrong?
1. Capture. Select the `RenderOpaque` entity child of that object.
2. Check `Triangle Count`, the pipeline name and the material slots. `None` means an unnamed image.
3. Step the slider. The preview shows the frame with objects `[0..i]` drawn.

### Which pass breaks the image?
1. Toggle the pass: `GET /render_graph/set_pass_enabled?name=<Pass>&enabled=false`.
2. The panel re-captures by itself when a toggle changes while enabled.
3. Compare the preview with `GET /get_texture?texture_name=<private texture>`.
4. Check `GET /get_logs?category=RenderPassHonesty` after toggling.

### Make a new feature debuggable
1. Declare passes with the kit verbs: `Compute`, `Fullscreen`, `SceneDraw`, `Blit`. Use a stable literal pass name. Put the toggle name in the `fx::Gate`.
2. Write intermediate results to private named textures. They are previewed directly and readable through `/get_texture`.
3. Send the final image through `Frame::ExposeAsViewOutput()`.
4. Draw a background with `PostOpaqueFrame::ViewBackground()`. The replay step is automatic.
5. Hand-off data that must vanish with a toggle needs a rule in `KnownRiskBlackboardKeyRules()`.
6. Toggle the pass off and on. Check the three honesty log categories stay empty.

## How a capture works
A capture needs a frame rendered with the capture context armed from its first draw. `FrameDebuggerCaptureHandshake` tracks one request:

1. `Request()`: a trigger fires. Triggers are: the Enable edge, a Step, the `Capture` button, `/frame_debugger/capture`, and a pass toggle.
2. The next offscreen build calls `IEditorLayer::PrepareFrameDebuggerCaptureContext()`. The panel returns its context while enabled and open. `RenderSystem::Draw()` records into it. The Game view renders for this even when its tab is hidden.
3. `ConsumeForReplay()`: while the offscreen graph is declared, `ConsumePendingFrameDebuggerReplayRequest()` returns true and `AddReplayPasses()` declares the replay passes.
4. `ConsumeForCapture()`: in the same frame, after execution, `FrameDebuggerPanel::Build()` runs `TriggerCapture()`: the detectors, `BuildRealFrameDebuggerSnapshot()`, then `CaptureFrame()`.

- HTTP and Step requests are served in the same frame. The `Capture` button and the pass toggle are served one frame later.
- Persistent render-graph textures change size only after the graph ran (`RenderGraph::PersistentTexturesResizedThisFrame()`). A frame that resized any of them is incomplete, so `Build()` requests the capture again for the next frame. This repeats at most 3 times in a row, so the status stays `pending` for a frame or two after a view resize or a first render.
- A request ends as `captured` or `failed`. If the offscreen build throws, or `CaptureFrame()` throws, the phase is `Failed` and `captureError` has the reason.

### Replay passes
- One graphics pass per drawn object. Pass `i` redraws objects `[0..i]` into its own texture.
- If a view background was published, one extra step redraws all objects and then the background.
- Cost grows with the square of the object count. It is paid once per capture.
- Names are `FrameDebuggerReplayStep<N>`. Category is `FrameDebuggerInternal`. The toggle `FrameDebuggerReplay` switches all of it off.
- The background comes from `fx::ViewBackground`. A disabled background pass publishes nothing, so no background step exists.

### Per-draw event images
- `CommandBuffer::Draw()` reports each draw to `FrameDebuggerEventSink::NoteCommandResult()`. A draw with an `Undefined` format is not reported.
- After each pass, `rg::CaptureAttachmentIntoSnapshot()` copies the armed attachment into one scratch image.
- Only the armed index is kept, to bound VRAM.
- `perDrawEventCount` in `state` is the high-water mark of these per-draw indices.

## Gotchas
- `Enable` pauses the game. `Disable` resumes it, unless you had paused it yourself first.
- Clicking `Resume` while `Enable` is on turns `Enable` off.
- Closing the window turns `Enable` off.
- The Game view renders at the size the Game panel last had. If the Game tab was never shown, that is the window size at startup.
- If the Game view fails to render while a capture is pending, `captureStatus` is `failed`. Click `Capture` to retry.
- A capture takes one or two frames. Do not read the tree right after `capture`. Poll `state`.
- On a capture frame, the Game view keeps its full CPU draw list. GPU-driven cluster claims and scene culling are off for that view.
- `/get_swapchain` captures the whole window. Call `open` first so the window sits on the main viewport.
- Panel descriptors are released before the UI backend shuts down (`ReleasePreviewDescriptor()`).

## Code map
| File | Role |
|---|---|
| `src/Editor/Panels/FrameDebuggerPanel.*` | The window, selection, capture flow |
| `src/Editor/FrameDebuggerData.*` | Snapshot model, tree builder, preview rule |
| `src/Editor/FrameDebuggerCapture.*` | Capture context, per-draw capture, pipeline state text |
| `src/Editor/FrameDebuggerDrawRecording.cpp` | Records one scene draw |
| `src/Editor/FrameDebuggerReplayPasses.cpp` | Replay passes |
| `src/Editor/FrameDebuggerCapturedFrame.*` | The one retained capture |
| `src/Editor/FrameDebuggerCaptureHandshake.h` | The capture life cycle and the status word |
| `src/Editor/FrameDebuggerPreviewProcessing.*` | Channel and levels |
| `src/Editor/FrameDebuggerPassMetadataRecorder.h` | Records pass category, draw kind, owner, barrier labels |
| `src/Editor/*Checker.*`, `*Guard.*` | The three honesty detectors |
| `src/Core/FrameDebuggerCaptureRecorder.h`, `FrameDebuggerEventSink.h` | Engine-side interfaces. `gte_core` needs no editor symbol. |
| `src/Application/FrameDebuggerCommandBridge.h` | Hand-off from the network thread to the main thread |

## Related
- [Render honesty](../conventions/render-honesty.md) · [Render graph](../architecture/render-graph.md)
- [HTTP cheat sheet](http-cheat-sheet.md) · [HTTP routes (debug)](http-routes-debug.md)
- [Feature kit](../reference/feature-kit.md) · [Rendering](../architecture/rendering.md)
