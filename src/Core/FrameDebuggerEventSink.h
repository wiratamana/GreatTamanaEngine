#pragma once

#include <volk.h>

namespace gte {

// Pure hook: "a real Draw/Dispatch just finished recording against this
// attachment - note it if a Frame Debugger-equivalent observer is armed for
// it". No Editor/ImGui/RenderGraph type appears here - this is the
// engine-layer half; the Editor-layer implementation lives elsewhere.
//
// CURRENTLY INERT: only gte::rg::CommandBuffer::Draw() calls
// NoteCommandResult() today, and no real pass in this engine goes through
// that method yet (every real graphics pass either bypasses CommandBuffer
// entirely or calls vkCmdDraw directly via its Native() escape hatch). Until
// some real pass is migrated onto CommandBuffer::Draw(), GET
// /frame_debugger/get_event_texture always returns found == false.
//
// Split into two calls, deliberately NOT one combined "capture now" call:
// NoteCommandResult() fires WHILE the owning pass's dynamic-rendering
// instance (vkCmdBeginRendering/vkCmdEndRendering) is still open, so it must
// never touch the GPU directly - a real image copy cannot legally be
// recorded inside an active rendering instance. It only remembers which
// attachment to copy, if any. FlushPendingCapture() fires once per pass,
// strictly AFTER that pass's own vkCmdEndRendering has already closed it -
// this is the only place an implementation may actually record a copy.
class FrameDebuggerEventSink {
public:
    virtual ~FrameDebuggerEventSink() = default;

    // `image`/`format`/`aspect` describe whichever ONE attachment this draw
    // actually wrote - color or depth, never both in one call. Bookkeeping
    // only - must never record a Vulkan command.
    virtual void NoteCommandResult(VkImage image, VkExtent2D extent, VkFormat format, VkImageAspectFlags aspect) = 0;

    // Called once per pass by the render graph executor, right after that
    // pass's own rendering bracket has closed. Performs the real GPU copy
    // for whatever NoteCommandResult() noted during that pass, if anything -
    // a safe no-op otherwise.
    virtual void FlushPendingCapture(VkCommandBuffer cmd) = 0;
};

} // namespace gte
