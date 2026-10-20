#include "GpuSkinningPipelines.h"

#include "../RenderGraph/ComputePass.h"
#include "../RenderGraph/RenderGraphBuilder.h"

namespace gte {

bool GpuSkinningPipelines::Declare(rg::RenderGraphBuilder& builder, const GpuSkinningDispatch& dispatch,
    rg::ViewScope scope, rg::RenderPassEvent event)
{
    if (dispatch.bindPose == nullptr || dispatch.skinWeights == nullptr || dispatch.boneMatrices == nullptr
        || dispatch.output == nullptr) {
        builder.ReportOnce("GpuSkinning", "a dispatch is missing one of its buffers; nothing was declared");
        return false;
    }
    const bool textured = dispatch.uv != nullptr;
    rg::Shader& shader = textured ? m_positionNormalUv : m_positionNormal;

    rg::ComputePass pass(builder, shader, "GpuSkinning.Dispatch", scope, event);
    pass.Instance(dispatch.instance)
        .Bind("bindPose", dispatch.bindPose->Handle())
        .Bind("skinWeights", dispatch.skinWeights->Handle())
        .Bind("boneMatrices", dispatch.boneMatrices->Handle())
        .Bind("outputBuffer", dispatch.outputHandle)
        .Push(dispatch.vertexCount);
    if (textured) {
        pass.Bind("bindPoseUv", dispatch.uv->Handle());
    }
    return pass.Dispatch(Extent2D{ dispatch.vertexCount, 1 });
}

} // namespace gte
