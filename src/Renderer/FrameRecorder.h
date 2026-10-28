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

// CPU-side frame state: the queued Pipeline+Mesh draws, and RecordFrame(), which turns them into the
// dynamic-rendering sequence for Renderer::RenderOffscreen(). Holds no device state, so it is trivially movable.
class FrameRecorder {
public:
    // Call once per frame: drops draws queued last frame so the queue cannot grow unbounded. See Renderer::BeginFrame().
    void BeginFrame();

    // Queues one draw for the next RecordFrame(). `materialDescriptorSet` is bound as set 0 and `sceneServicesSet`
    // as set 1 when non-null. See Renderer::Submit().
    void Submit(const Pipeline& pipeline, const Mesh& mesh, const Mat4& modelMatrix = Mat4::Identity(),
        const Mat4& viewProjMatrix = Mat4::Identity(), VkDescriptorSet materialDescriptorSet = VK_NULL_HANDLE,
        VkDescriptorSet sceneServicesSet = VK_NULL_HANDLE);

    // Issues one draw against `cmd`: pipeline, push constants (model, viewProj), optional set 0 (material) and
    // set 1 (scene services), buffers, then an indexed or plain draw. Stateless; never touches the queue or DrawStats.
    static void IssueDrawCommand(VkCommandBuffer cmd, VkPipeline pipeline, VkPipelineLayout layout,
        VkBuffer vertexBuffer, std::uint32_t vertexCount, VkBuffer indexBuffer, std::uint32_t indexCount,
        const Mat4& model, const Mat4& viewProj, VkDescriptorSet materialDescriptorSet,
        VkDescriptorSet sceneServicesSet = VK_NULL_HANDLE);

    // Indirect-draw sibling of IssueDrawCommand(): set 0 = `materialDescriptorSet`, set 1 = `sceneServicesSet`
    // (each when non-null), set 2 = buffer set. Counted draw; needs drawIndirectCount, multiDrawIndirect and
    // drawIndirectFirstInstance. Never touches DrawStats.
    static void IssueIndirectDrawCommand(VkCommandBuffer cmd, VkPipeline pipeline, VkPipelineLayout layout,
        VkBuffer vertexBuffer, VkBuffer indexBuffer, VkDescriptorSet bufferDescriptorSet,
        VkDescriptorSet materialDescriptorSet, VkDescriptorSet sceneServicesSet, const Mat4& viewProj,
        VkBuffer indirectBuffer, uint64_t indirectOffset, std::uint32_t maxDrawCount, VkBuffer countBuffer,
        uint64_t countBufferOffset);

    // Records attachment barriers, the dynamic-rendering clear to `clearColor`, the queued draws, `recordExtra` and the
    // transition to `finalLayout`. A null target.depthImage skips depth; a real one is cleared to 1.0 and discarded.
    DrawStats RecordFrame(VkCommandBuffer cmd, const RenderTarget& target, VkFormat expectedFormat,
        VkFormat expectedDepthFormat, VkImageLayout finalLayout, const std::array<float, 4>& clearColor,
        const std::function<void(VkCommandBuffer)>& recordExtra);

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

    // This frame's queued draws; cleared by BeginFrame() and after RecordFrame().
    std::vector<DrawItem> m_drawQueue;
};

} // namespace gte
