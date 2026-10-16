# Frame Debugger

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

The Frame Debugger is an on-demand Editor window ("Window > Frame Debugger"). It freezes one Game View frame and
shows its render events as a tree, with a preview image, shader/pass facts and channel/levels controls. Follow
these rules when you change the window, its data model, its capture code or the HTTP routes behind it.

## Scope and lifetime

- **Game View only.** Passes with `ViewScope::SceneView` never appear in the tree.
- **One captured frame.** `FrameDebuggerCurrentCapture` (`src/Editor/FrameDebuggerHistory.h`) holds one
  `std::optional<FrameDebuggerHistoryEntry>`: the snapshot tree plus the retained GPU copies. There is no history.
  A new capture replaces the old one. `Clear()` frees every retained texture.
- **Capture is on demand, never continuous.** The window rebuilds nothing from live frames. The tree and images stay
  frozen until the next capture, until "Enable" is turned off, or until playback resumes.
- **Enable pauses playback.** Turning "Enable" on sets `ctx.playbackPaused` and arms a capture. Turning it off clears
  the capture and does not resume playback. Resuming playback while enabled also clears the capture; `m_enabled`
  stays on.
- **Toggle changes re-capture.** A change in the pass toggle registry while enabled arms a new capture, so a
  disabled pass does not stay visible.

## Capture flow

A capture needs a frame that was rendered with the capture context armed from its first draw. The panel therefore
splits one request over two points of a frame:

1. Any trigger (the Enable edge, a Step, the "Capture" button, `GET /frame_debugger/capture`, a toggle change) only
   sets `m_pendingCaptureTrigger`.
2. On the next frame, `Core` asks `IEditorLayer::PrepareFrameDebuggerCaptureContext()`. The panel returns its
   `FrameDebuggerCaptureContext` (after `Reset()`) only while the window is open and enabled; otherwise `nullptr`.
   `RenderSystem::Draw()` records into it through `IFrameDebuggerCaptureRecorder`.
3. While the offscreen graph is declared, `Core` calls `ConsumePendingFrameDebuggerReplayRequest()`. When it returns
   true, `AddReplayPasses()` declares the replay passes and `m_replayServicedThisFrame` is set.
4. After the graph executed, `FrameDebuggerPanel::Build()` sees `m_replayServicedThisFrame` and runs
   `TriggerCapture()`: honesty detectors, `BuildRealFrameDebuggerSnapshot()`, then `CaptureFrame()`.

`CaptureFrame()` copies the Game View (`preview`) and the composited view (`compositedPreview`, when one exists)
into fresh retained textures and moves the replay step textures into `perObjectStepPreviews`. The selection resets to
-1 for every new capture.

## Event tree

`BuildRealFrameDebuggerSnapshot()` (`src/Editor/FrameDebuggerData.cpp`) reshapes one `rg::RenderGraphSnapshot` and the
capture context into `FrameDebuggerSnapshot`. It is pure: no ImGui, no Vulkan. It finds passes by structure, not by
name. The one pivot is the first pass whose `RenderPassEvent` is `Opaques` or later. The tree is:

```
Game View
  <feature name>                 surviving compute passes before the pivot, grouped by owning feature
    <pass> -> "Compute Dispatch"
  Compute Dispatches (Pre-GameView)   such passes with no owning feature
  RenderOpaque                   the first Graphics pass of the view region
    <entity name> (Entity <n>)   one child per recorded scene draw
  <pass> -> "Draw Mesh" | "Draw Quad" | "Blit"   other Graphics/Blit passes of the view region, e.g. DrawSkyBackground
  Compute Dispatches (Post-GameView)  surviving compute passes after the view region
  Other Render Passes            any surviving pass none of the rules above claimed
```

Rules:

- A group exists only when it has at least one child.
- Every pass except `RenderOpaque` is a parent row with exactly one owned child row. The child label comes from
  the pass kind (`Compute Dispatch`) or from `rg::RenderPassDrawKind` (`Draw Mesh`, `Draw Quad`, `Blit`). Both rows
  are selectable. A wrapped pass uses two consecutive event indices.
- The view-region walk stops at the first surviving compute pass. Culled passes, `SceneView` passes and passes in
  category `FrameDebuggerInternal` (the replay passes) are skipped.
- A tracking array marks every pass that became a leaf or was excluded on purpose. The last sweep puts every other
  surviving pass into "Other Render Passes". No surviving pass can be dropped silently.
- `eventIndex` grows in execution order. `totalEventCount` is the next free index.
- A compute leaf uses the raw pass name as `passName` and `shaderName`. Its blend/Z/stencil rows read
  `n/a (compute pass)`. Its textures are labeled by `rg::ResourceKind` (`Read Texture`, `Write Buffer`,
  `Read Volume Texture`, ...). It also reports `GPU Time (ms)`.
- The `RenderOpaque` leaf lists the distinct pipeline debug names and material texture names of the frame, the clear
  color, `Draw Stats (Calls, Tris)` from the graph snapshot and the `ViewProjection` matrix. Its per-entity children
  show their own pipeline name, material texture, `Triangle Count` and `Entity (Index, Generation)`.
- Blend/Z/stencil facts come from `DescribeStandardPipelineState()`. `DrawSkyBackground` uses
  `DescribeSkyBackgroundPipelineState()` (depth test `Equal`, depth write off).
- Group names are the owning feature names the graph recorded when the pass was declared. The Frame Debugger never
  names a feature.

## Preview image

Every leaf carries a `FrameDebuggerStepPreviewKind`, set once by the snapshot builder:

| Kind | Leaf | Image |
| --- | --- | --- |
| `NotYetDrawn` | compute pass before the pivot | none; the box says "Nothing drawn yet at this point in the frame." |
| `PerObjectStep` | one per-entity child (`stepPreviewIndex`) | the Game View with only objects `[0..i]` drawn |
| `PreComposite` | `RenderOpaque`, other view-region leaves, compute leaves before the composite pass | `preview` |
| `PostComposite` | the composite pass, later leaves, or no selection | `compositedPreview`, else `preview` |

The composite pass is found by the texture it writes (`GameViewComposited`), not by its name.

`ChooseFrameDebuggerPreviewSource()` is the pure picking rule. `NotYetDrawn` always wins. `PerObjectStep` shows its
own retained image or nothing; it never falls back to a whole-frame image. `PreComposite` shows `preview`.
`PostComposite` prefers `compositedPreview`.

A leaf whose pass writes a private render-graph resource (anything other than `GameView`) sets
`privateWriteTextureName`. The panel previews that texture directly from `RenderGraph::DebugTextureSnapshotFor()`
and skips the retained images.

The composited image comes from the feature's view output: a feature calls `Frame::ExposeAsViewOutput()` and
`Core` passes the texture to the editor layer through `SetGameViewCompositedTexture()` /
`SetSceneViewCompositedTexture()`. See [Render feature authoring](render-feature-authoring.md).

### Replay passes

On a capture frame, `FrameDebuggerCaptureContext::AddReplayPasses()` (`src/Editor/FrameDebuggerReplayPasses.cpp`)
declares one graphics pass per drawn object. Pass `i` redraws objects `[0..i]` from scratch into its own texture
(`Game::Render()` with `maxDrawCount`). When a view background was published, one extra step redraws all objects and
then the background. The cost is quadratic in the object count; it is paid once per human-triggered capture.

- Pass and texture names are `FrameDebuggerReplayStep<N>`. The passes have category `FrameDebuggerInternal` and the
  owner "Frame Debugger". The toggle `FrameDebuggerReplay` switches the whole mechanism off.
- The background comes from `fx::ViewBackground`, which publishes an `rg::FullscreenReplay` under
  `kViewBackgroundReplayCallbackKey` (`Core.ViewBackgroundReplayCallback`) for the Game view. `declare` adds the
  shader bindings to the replay pass, `record` draws through `rg::RecordingContext`. A disabled background pass
  publishes nothing, so there is no background step. See
  [View background](../architecture/kit-view-background.md).
- Every replay destination is added to the graph outputs, so the compiler does not cull it.
- `rg::RecordingContext` carries no Vulkan type. Only `src/Renderer` can open the raw command buffer. See
  [Recording context](../architecture/recording-context.md).

### Channels and Levels

`FrameDebuggerPreviewProcessing` applies Levels first, then Channel isolation (an isolated channel is replicated
over RGB, alpha 1). `ApplyFrameDebuggerPreviewTransform()` is the CPU reference; `Shaders/FrameDebuggerPreview.comp`
does the same on the GPU. The neutral state (All, black 0, white 1) shows the retained texture directly. Any other
state runs the compute pass into a scratch texture, only when the source, channel or levels changed. The retained
copy is never modified. These controls are independent of the `channel=color|depth` query of `GET /get_texture`.

### Shader-property texture view

In the "ShaderProperties" tab, a row that names a render-graph 2D or volume texture has a "View" button. A 2D texture is
read back once with `Renderer::CaptureDebugTexturePixels()` and converted to RGBA8. A volume texture is ray-marched
by `VolumeTexturePreviewRenderer`. Either result is uploaded as an owned copy and shown until the user clicks "Back
to Step Preview", selects another event, captures again, or disables the window. Buffers, material textures and
texture arrays get no button. The preview rect keeps the image aspect ratio (`ComputeAspectFitImageRect()`).

## Per-draw event images

`CommandBuffer::Draw()` reports each draw to `FrameDebuggerEventSink::NoteCommandResult()` with an `AttachmentWrite`
(opaque `NativeImageToken`, `Extent2D`, `PixelFormat`, `isDepth`). A draw whose format is `Undefined` is not
reported. `FlushPendingCapture(RecordingContext)` runs once per pass, after the rendering instance closed, and
calls `rg::CaptureAttachmentIntoSnapshot()` (`src/Renderer`) to copy the armed attachment into one retained scratch
image. Only the armed index is kept, to bound VRAM. These per-draw indices are a separate index space from the
tree's `eventIndex`; `perDrawEventCount` in the state JSON is their high-water mark.

## Honesty detectors

Each capture runs three checks. A finding is logged once per distinct entry.

| Log category | Checker | Finds |
| --- | --- | --- |
| `RenderPassHonesty` | `DetectRenderPassHonestyMismatches()` | a surviving pass whose toggle reads disabled |
| `FrameDebuggerSideChannel` | `DetectDisabledPassBlackboardKeyLeaks()` | a curated blackboard key still published while its gating toggle is off |
| `FrameDebuggerCoverage` | `DetectPassesMissingFromFrameDebuggerTree()` | a surviving, non-internal, non-SceneView pass with no leaf in the tree |

`KnownRiskBlackboardKeyRules()` (`src/Editor/FrameDebuggerSideChannelChecker.cpp`) is the curated key list. Add a rule
when a new feature publishes hand-off data that must vanish with its toggle. The honesty and side-channel checks run
only when a toggle registry exists; the side-channel check also needs the offscreen blackboard.

## Window behavior

- `FrameDebuggerPanel` (`src/Editor/Panels/FrameDebuggerPanel.h/.cpp`) is a stateful class called by name from
  `ImGuiEditorLayer`. It owns the capture context, the retained capture, the selection, the channel/levels state and
  its own preview descriptors.
- Left pane: the event tree, with a draggable splitter. Right pane: render target caption, channel buttons, levels,
  the preview box, then the selected event's Shader/Pass/Blend/ZClip/ZTest/ZWrite/Cull/Stencil rows and the
  "ShaderProperties" tab (Textures, Vectors, Matrices). The "Preview" tab is a placeholder.
- The frame-step slider selects events. It follows mouse drags and Left/Right keys. Every selection path (tree click,
  slider, `select_event`) goes through `SetSelectedEventIndex()`.
- Placeholders: "No frame captured yet.", "No event selected.", "No Texture", and the `NotYetDrawn` text above.
- The "Editor" mode combo is a disabled stub.
- The panel descriptors are released before the UI backend shuts down (`ReleasePreviewDescriptor()`).

## HTTP automation

`FrameDebuggerCommandBridge` (`src/Application/FrameDebuggerCommandBridge.h/.cpp`) carries every command from the
network thread to the main thread. The routes are in `src/Network/NetworkServer.cpp`.

| Route | Effect |
| --- | --- |
| `GET /frame_debugger/open` | opens the window; a programmatic open pins it once onto the main ImGui viewport so `GET /get_swapchain` can see it |
| `GET /frame_debugger/enable?value=true\|false` | same edge handling as the checkbox |
| `GET /frame_debugger/capture` | arms a capture; refused while not enabled |
| `GET /frame_debugger/select_event?index=<n>` | selects a tree event; `-1` deselects |
| `GET /frame_debugger/set_channel?value=all\|r\|g\|b\|a` | channel isolation |
| `GET /frame_debugger/set_levels?black=<f>&white=<f>` | levels range |
| `GET /frame_debugger/state` | flat JSON: `enabled`, `windowOpen`, `hasCapturedFrame`, `totalEventCount`, `selectedEventIndex`, `channel`, `levelsBlack`, `levelsWhite`, `perDrawEventCount` |
| `GET /frame_debugger/get_event_texture?index=<n>` | image of one per-draw event (PNG, or base64 JSON with `format=json`); 404 until that index was armed and captured, so poll |

Every other `/frame_debugger/*` route answers `{"success": ..., "state": {...}}` with the same state object.

Related routes: `GET /get_texture?texture_name=<n>[&channel=color|depth]` and `GET /list_textures` read any
render-graph texture, including `GameViewComposited`. `GET /get_game_view` returns the Game view.
`GET /render_graph/passes` and `GET /render_graph/set_pass_enabled?name=<n>&enabled=<bool>` list and toggle passes.
`GET /get_logs` shows the detector categories above. The control routes go through command bridges; the network
thread never touches Editor state directly. See [Networking](networking.md).

## Making a new feature debuggable

A pass declared through the render graph appears in the tree without any Frame Debugger code.

1. Declare passes with the kit verbs (`Compute`, `Fullscreen`, `SceneDraw`, `Blit`). Use a stable literal pass name
   and register the toggle name in the feature's `fx::Gate`. See
   [Render feature authoring](render-feature-authoring.md).
2. Pick the right pass kind and `ViewScope`. A compute pass before the opaque pivot lands under the feature's group.
   A graphics pass lands in the view region or in "Other Render Passes".
3. Write intermediate results to private, named textures. They are previewed directly and readable through
   `GET /get_texture`.
4. A final image that replaces the Game View output goes through `Frame::ExposeAsViewOutput()`.
5. A background drawn behind the scene uses `PostOpaqueFrame::ViewBackground()`; the replay is automatic.
6. Hand-off data that must disappear with a toggle gets a rule in `KnownRiskBlackboardKeyRules()`.
7. Check `/get_logs` for `RenderPassHonesty`, `FrameDebuggerSideChannel` and `FrameDebuggerCoverage` entries after
   toggling the pass off and on.

## Code map

| File | Role |
| --- | --- |
| `src/Editor/FrameDebuggerData.h/.cpp` | snapshot model, tree builder, preview picking rule, aspect-fit helper |
| `src/Editor/FrameDebuggerCapture.h/.cpp` | `FrameDebuggerCaptureContext`: draw records, per-draw event capture, pipeline state descriptions |
| `src/Editor/FrameDebuggerDrawRecording.cpp` | records one scene draw (entity name, pipeline, material texture, triangles) |
| `src/Editor/FrameDebuggerReplayPasses.cpp` | replay passes |
| `src/Editor/FrameDebuggerHistory.h/.cpp` | the one retained capture |
| `src/Editor/FrameDebuggerPreviewProcessing.h/.cpp` | channel/levels transform and its compute dispatcher |
| `src/Editor/FrameDebuggerPassMetadataRecorder.h` | records pass category, draw kind, owning feature and barrier labels while the graph is declared and executed |
| `src/Editor/*Checker.*`, `src/Editor/*Guard.*` | the three honesty detectors and their logging wrappers |
| `src/Editor/Panels/FrameDebuggerPanel.h/.cpp` | the window |
| `src/Core/FrameDebuggerCaptureRecorder.h`, `src/Core/FrameDebuggerEventSink.h` | engine-side interfaces; `gte_core` needs no Editor symbol |
| `src/Application/FrameDebuggerCommandBridge.h/.cpp` | HTTP command bridge |
