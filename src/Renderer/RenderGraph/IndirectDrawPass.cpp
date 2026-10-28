#include "IndirectDrawPass.h"

#include "ClusterDrawBindings.h"
#include "MeshPass.h"
#include "RenderGraph.h"
#include "../Renderer.h"

#include <cassert>

namespace gte::rg {

std::optional<std::string> ValidateIndirectDrawRecord(const IndirectDrawRecord& record)
{
    if (!record.target.IsValid()) {
        return std::string("the target texture is invalid");
    }
    if (!record.indirect.IsValid()) {
        return std::string("the indirect command buffer is invalid");
    }
    if (!record.instances.IsValid()) {
        return std::string("the instance buffer is invalid");
    }
    if (!record.visible.IsValid()) {
        return std::string("the visible list buffer is invalid");
    }
    if (!record.count.IsValid()) {
        return std::string("the count buffer is invalid");
    }
    if (record.maxDrawCount == 0) {
        return std::string("maxDrawCount is zero");
    }
    return std::nullopt;
}

void DeclareIndirectDraw(RenderGraphBuilder::PassBuilder& pass, const IndirectDrawRecord& record)
{
    if (!record.depthOnly) {
        pass.WriteColorAttachment(record.target, record.clearColor);
    }
    pass.WriteDepthStencilAttachment(record.target, record.clearDepth);
    pass.ReadBuffer(record.indirect, ResourceAccess::IndirectCommandRead);
    pass.ReadBuffer(record.count, ResourceAccess::IndirectCommandRead);
    pass.ReadBuffer(record.instances, ResourceAccess::VertexShaderStorageRead);
    pass.ReadBuffer(record.visible, ResourceAccess::VertexShaderStorageRead);
}

void ExecuteIndirectDraw(PassContext& ctx, const IndirectDrawRecord& record, const IndirectDrawInputs& inputs)
{
    constexpr const char* kCategory = "IndirectDrawPass";
    if (inputs.pipeline == nullptr || inputs.mesh == nullptr || inputs.clusterBindings == nullptr
        || ctx.renderer == nullptr) {
        ctx.ReportOnce(kCategory, "draw skipped: the pipeline, mesh, bindings or renderer is missing");
        assert(false && "an indirect draw must resolve its pipeline, mesh and bindings when declared");
        return;
    }
    const VkBuffer indirect = ctx.resolveBuffer(record.indirect);
    const VkBuffer count = ctx.resolveBuffer(record.count);
    if (indirect == VK_NULL_HANDLE || count == VK_NULL_HANDLE) {
        ctx.ReportOnce(kCategory, "draw skipped: the indirect or count buffer did not resolve");
        return;
    }
    if (inputs.pipeline->HasMaterialSet() && inputs.materialSet == DescriptorSetHandle::Invalid) {
        ctx.ReportOnce(kCategory, "draw skipped: the pipeline needs a material set and none was resolved");
        return;
    }
    if (!inputs.clusterBindings->Rewrite(ctx, inputs.clusterBindingIndex, record.visible, record.instances)) {
        ctx.ReportOnce(kCategory, "draw skipped: the descriptor set could not be written");
        assert(false && "an indirect draw must have a prepared set and resolvable buffers");
        return;
    }
    const DescriptorSetHandle bufferSet = inputs.clusterBindings->Set(inputs.clusterBindingIndex);

    const SceneRecording recording(ctx);
    ctx.renderer->SubmitIndirect(*inputs.pipeline, *inputs.mesh, indirect, /*indirectOffset=*/0, record.maxDrawCount,
        count, /*countBufferOffset=*/0, bufferSet, inputs.sceneSet, inputs.viewProjection, inputs.materialSet,
        inputs.drawTag);
    if (ctx.recordIndirectDraw) {
        ctx.recordIndirectDraw();
    }
}

} // namespace gte::rg
