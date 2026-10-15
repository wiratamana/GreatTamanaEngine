#include "IndirectDrawPass.h"

#include "MeshPass.h"
#include "RenderGraph.h"
#include "../Renderer.h"

namespace gte::rg {

std::optional<std::string> ValidateIndirectDrawRecord(const IndirectDrawRecord& record)
{
    if (!record.target.IsValid()) {
        return std::string("the target texture is invalid");
    }
    if (!record.indirect.IsValid() || !record.instances.IsValid()) {
        return std::string("the indirect command or instance buffer is invalid");
    }
    if (record.useCount && !record.count.IsValid()) {
        return std::string("the count buffer is invalid");
    }
    if (record.maxDrawCount == 0) {
        return std::string("maxDrawCount is zero");
    }
    return std::nullopt;
}

void DeclareIndirectDraw(RenderGraphBuilder::PassBuilder& pass, const IndirectDrawRecord& record)
{
    pass.WriteColorAttachment(record.target);
    pass.WriteDepthStencilAttachment(record.target);
    pass.ReadBuffer(record.indirect, ResourceAccess::IndirectCommandRead);
    if (record.count.IsValid()) {
        pass.ReadBuffer(record.count, ResourceAccess::IndirectCommandRead);
    }
    pass.ReadBuffer(record.instances, ResourceAccess::VertexShaderStorageRead);
}

void ExecuteIndirectDraw(PassContext& ctx, const IndirectDrawRecord& record, const IndirectDrawInputs& inputs)
{
    if (inputs.pipeline == nullptr || inputs.mesh == nullptr || ctx.renderer == nullptr) {
        return;
    }
    const VkBuffer indirect = ctx.resolveBuffer(record.indirect);
    const VkBuffer count = record.useCount ? ctx.resolveBuffer(record.count) : VK_NULL_HANDLE;
    if (indirect == VK_NULL_HANDLE || (record.useCount && count == VK_NULL_HANDLE)) {
        return;
    }

    const SceneRecording recording(ctx);
    ctx.renderer->SubmitIndirect(*inputs.pipeline, *inputs.mesh, indirect, /*indirectOffset=*/0, record.maxDrawCount,
        count, /*countBufferOffset=*/0, inputs.instanceSet, inputs.sceneSet, inputs.viewProjection);
    if (ctx.recordIndirectDraw) {
        ctx.recordIndirectDraw();
    }
}

} // namespace gte::rg
