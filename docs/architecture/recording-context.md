# Recording context

Code outside `src/Renderer` never sees a `VkCommandBuffer`. It carries `rg::RecordingContext` instead.

## Type

`src/Renderer/RenderGraph/RecordingContext.h` has no Vulkan include. It is a 24-byte value:
an opaque native pointer, the `Renderer*`, and an optional `const PassContext*` (valid while that pass executes).

| Member | Purpose |
|---|---|
| `Commands()` | Returns a `CommandBuffer` for this command buffer. Inside a pass it carries that pass's draw stats and frame-debugger sink. Outside a pass it carries neither. |
| `PlatformHandle()` | Opaque native pointer for the ImGui Vulkan backend glue. Only `Editor/ImGuiEditorLayer.cpp` may call it. |
| `operator bool` | True when the context holds a command buffer. |

## Who makes one

- `PassContext::Recording()`: draws feed the pass stats and the frame debugger.
- `Renderer::BeginOffscreenRenderGraphRecording()`: the offscreen command buffer, outside any pass.

## Who opens one

Only `src/Renderer`, through `rg::RecordingAccess` (`Native`, `Wrap`, `Adopt`, `ImageToken`, `Image`).
It is the single friend of `CommandBuffer`, `PassContext` and `RecordingContext`.
`CommandBuffer::AdoptRaw`, `CommandBuffer::RawCommandBuffer` and `PassContext::RawCommandBuffer` no longer exist.
`NativeLeakCheck` forbids the tokens `RecordingAccess` and `PlatformHandle(` outside `src/Renderer`
(the ImGui glue file is the one allowlisted use).

## Interfaces that carry it

`Core::SetPresentImGuiRecorder`, `RenderPassViewData::recordSceneOverlay`, `IEditorLayer::Render` / `RenderSceneGrid`,
`FrameDebuggerEventSink::FlushPendingCapture`, `RenderTexture::FinalizeForExternalSampling`, `RenderGraph::Execute`.
The Frame Debugger background replay carries an `rg::FullscreenReplay` instead (see `kit-view-background.md`).

## Frame Debugger events

`FrameDebuggerEventSink::NoteCommandResult` takes an `AttachmentWrite` (opaque `NativeImageToken`, `Extent2D`,
`PixelFormat`, depth flag). The copy into the retained snapshot is `rg::CaptureAttachmentIntoSnapshot`, which lives in
`src/Renderer`; the Editor only stores the token between the note and the flush.

## Rules

- A context is only valid inside the callback that received it. Never store it.
- Recording through `Commands()` still needs an open rendering scope; `SceneRecording` opens it for scene draws.
