#pragma once

#include "../Math/Mat4.h"
#include "Mesh.h"
#include "Pipeline.h"
#include "RenderTarget.h"

#include <volk.h>

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace gte {

// Everything Game (or a future Editor) hands Renderer every frame BEFORE any
// actual Vulkan recording happens: the color to clear to (Clear()) and the
// list of Pipeline+Mesh draw calls queued for this frame (Submit()) - plus
// the one shared routine (RecordFrame()) that turns that CPU-side state into
// the actual dynamic-rendering command sequence (barrier -> clear -> queued
// draws -> caller-supplied overlay -> barrier). RecordFrame() is used
// identically by both Renderer::Present() (target = the swapchain image) and
// Renderer::RenderOffscreen() (target = a RenderTexture) - see
// FramePresenter.h, which owns the actual command-buffer/queue plumbing
// around each of those calls.
//
// Deliberately holds no Vulkan device/queue/command-pool state of its own -
// just CPU-side data plus recording logic against a caller-supplied
// VkCommandBuffer - so it needs no custom constructor/destructor and is
// trivially/safely movable (implicit move ctor/assignment), unlike the
// objects around it that actually own Vulkan handles.
class FrameRecorder {
public:
    // Sets the color the render target will be cleared to on the next
    // RecordFrame() call (0-255 per channel). See Renderer::Clear().
    void Clear(std::uint8_t r = 0, std::uint8_t g = 0, std::uint8_t b = 0, std::uint8_t a = 255);

    // Call once per frame, before Game::Update()/Render() - clears any draw
    // items queued last frame via Submit() (see below) so this frame always
    // starts from an empty queue. Also guards against a queue growing
    // unbounded across frames where nothing ever consumes it (e.g. a
    // minimized window with no Editor - see RecordFrame). See
    // Renderer::BeginFrame().
    void BeginFrame();

    // Queues one draw call - a Pipeline plus the Mesh to draw with it, plus
    // the world matrix (pushed to the shader via vkCmdPushConstants - see
    // Pipeline.h's push constant range and Shaders/Triangle.vert) and the
    // view-projection matrix of whichever camera this recording's target is
    // being rendered through - to be recorded the next time RecordFrame()
    // runs. Both default to Mat4::Identity() so existing callers that don't
    // care about a transform/camera are unaffected. See Renderer::Submit()
    // for the full seam this is part of.
    void Submit(const Pipeline& pipeline, const Mesh& mesh, const Mat4& modelMatrix = Mat4::Identity(),
        const Mat4& viewProjMatrix = Mat4::Identity());

    // Records the undefined->color-attachment barrier, the dynamic-
    // rendering clear + every queued Submit() draw + recordExtra, and the
    // final transition to `finalLayout` - shared by FramePresenter::Present()
    // (target = the current swapchain image, finalLayout =
    // VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) and FramePresenter::RenderOffscreen()
    // (target = a RenderTexture, finalLayout =
    // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL).
    //
    // expectedFormat is asserted (debug builds only) against target.format -
    // see AGENTS.md ("Render Target Format Matching") - the caller passes
    // whatever Renderer::ColorFormat() currently is.
    //
    // Clears the queued draw list right after recording it (NOT at the top
    // of this call) so a second RecordFrame() call later in the SAME frame
    // (e.g. Present()'s editor-chrome-only pass, after RenderOffscreen()
    // already drew the queue into the Game view texture) never redraws it -
    // see BeginFrame()/Submit() above.
    void RecordFrame(VkCommandBuffer cmd, const RenderTarget& target, VkFormat expectedFormat,
        VkImageLayout finalLayout, const std::function<void(VkCommandBuffer)>& recordExtra);

private:
    // One queued Submit() call's worth of plain Vulkan handles - deliberately
    // NOT a reference/pointer to the Pipeline/Mesh themselves (those are
    // owned by whoever called Submit(), typically Game, and must outlive
    // the RecordFrame() call that consumes this - which is always true
    // within a single frame, since Submit() is called from Game::Render()
    // and consumed later that same frame). `layout` is needed alongside
    // `pipeline` because vkCmdPushConstants() takes a VkPipelineLayout, not
    // a VkPipeline - see Pipeline::Layout().
    struct DrawItem {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        std::uint32_t vertexCount = 0;
        Mat4 model = Mat4::Identity(); // Mat4's default ctor is all-zero, NOT identity - see Math/Mat4.h.
        Mat4 viewProj = Mat4::Identity(); // The active camera's view-projection matrix at Submit() time - see Renderer::Submit().
    };

    std::array<float, 4> m_clearColor{ 0.0f, 0.0f, 0.0f, 1.0f };

    // This frame's queued Submit() calls. Cleared at the top of every frame
    // (BeginFrame()) AND immediately after being recorded (RecordFrame()),
    // so a frame is never drawn twice (e.g. RenderOffscreen() then
    // Present() in the same Editor-build frame) and never silently
    // accumulates across frames where nothing consumes it.
    std::vector<DrawItem> m_drawQueue;
};

} // namespace gte
