#pragma once

#include "../Math/Mat4.h"
#include "DrawStats.h"
#include "Mesh.h"
#include "Pipeline.h"
#include "RenderTarget.h"

#include <volk.h>

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace gte {

// CPU-side frame state: the clear color, the queued Pipeline+Mesh draws, and RecordFrame(), which turns them into
// the dynamic-rendering sequence for Renderer::RenderOffscreen(). IssueDrawCommand() is the per-item primitive
// shared with Renderer::Submit() while a graph pass records. It holds no device state, so it is trivially movable.
class FrameRecorder {
public:
    // Sets the clear color (0-255 per channel) for the next RecordFrame(). See Renderer::Clear().
    void Clear(std::uint8_t r = 0, std::uint8_t g = 0, std::uint8_t b = 0, std::uint8_t a = 255);

    // Call once per frame: drops draws queued last frame so the queue cannot grow unbounded. See Renderer::BeginFrame().
    void BeginFrame();

    // Queues one draw for the next RecordFrame(): a Pipeline and Mesh plus the world and view-projection
    // matrices (pushed to the shader). `materialDescriptorSet` is bound as set 0 and `sceneServicesSet` as
    // set 1 when non-null. See Renderer::Submit().
    void Submit(const Pipeline& pipeline, const Mesh& mesh, const Mat4& modelMatrix = Mat4::Identity(),
        const Mat4& viewProjMatrix = Mat4::Identity(), VkDescriptorSet materialDescriptorSet = VK_NULL_HANDLE,
        VkDescriptorSet sceneServicesSet = VK_NULL_HANDLE);

    // Issues one draw directly against `cmd`: bind pipeline, push constants (model then viewProj), optional
    // set 0 (material) and set 1 (scene services), bind vertex/index buffers, then vkCmdDraw or
    // vkCmdDrawIndexed depending on whether `indexBuffer` is null. Stateless; never touches the queue or DrawStats.
    static void IssueDrawCommand(VkCommandBuffer cmd, VkPipeline pipeline, VkPipelineLayout layout,
        VkBuffer vertexBuffer, std::uint32_t vertexCount, VkBuffer indexBuffer, std::uint32_t indexCount,
        const Mat4& model, const Mat4& viewProj, VkDescriptorSet materialDescriptorSet,
        VkDescriptorSet sceneServicesSet = VK_NULL_HANDLE);

    // Indirect-draw sibling of IssueDrawCommand(): binds the instance set at set 0 and `sceneServicesSet` at set 1
    // (when non-null), pushes `viewProj`, then issues an indirect draw: counted when a count buffer and device
    // support exist, else with `maxDrawCount` as a fixed count. Never touches DrawStats.
    static void IssueIndirectDrawCommand(VkCommandBuffer cmd, VkPipeline pipeline, VkPipelineLayout layout,
        VkBuffer vertexBuffer, VkBuffer indexBuffer, VkDescriptorSet instanceBufferDescriptorSet,
        VkDescriptorSet sceneServicesSet, const Mat4& viewProj, VkBuffer indirectBuffer, uint64_t indirectOffset,
        std::uint32_t maxDrawCount, VkBuffer countBuffer, uint64_t countBufferOffset, bool supportsDrawIndirectCount);

    // Records the barriers to attachment state, the dynamic-rendering clear, every queued draw, `recordExtra`,
    // and the final transition to `finalLayout`. Clears the queue afterwards so a second call does not redraw it.
    // The formats are asserted in debug builds against the target (depth only when a depth image exists).

    // A null target.depthImage skips the depth attachment. A real one is cleared to 1.0 and discarded afterwards.
    // Returns this call's DrawStats, accumulated inline in the loop that issues each draw.
    DrawStats RecordFrame(VkCommandBuffer cmd, const RenderTarget& target, VkFormat expectedFormat,
        VkFormat expectedDepthFormat, VkImageLayout finalLayout, const std::function<void(VkCommandBuffer)>& recordExtra);

private:
    // One queued Submit(): plain Vulkan handles, not references to the Pipeline/Mesh, which the caller owns for
    // the frame. `layout` is kept because vkCmdPushConstants takes a layout.
    struct DrawItem {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        std::uint32_t vertexCount = 0;
        // Null means the mesh has no index buffer, so RecordFrame() issues vkCmdDraw.
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        std::uint32_t indexCount = 0;
        Mat4 model = Mat4::Identity(); // Mat4's default ctor is all-zero, not identity.
        Mat4 viewProj = Mat4::Identity(); // The active camera's view-projection at Submit() time.
        // Null means no material texture to bind.
        VkDescriptorSet materialDescriptorSet = VK_NULL_HANDLE;
        // Null means no scene-services set to bind.
        VkDescriptorSet sceneServicesSet = VK_NULL_HANDLE;
    };

    std::array<float, 4> m_clearColor{ 0.0f, 0.0f, 0.0f, 1.0f };

    // This frame's queued draws; cleared by BeginFrame() and after RecordFrame().
    std::vector<DrawItem> m_drawQueue;
};

} // namespace gte
