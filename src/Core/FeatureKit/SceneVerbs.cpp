#include "Frame.h"

#include "FrameContext.h"
#include "../Core.h"
#include "../../Renderer/SceneServicesDescriptorSet.h"

namespace gte::fx {

static_assert(kSceneSlotCount == kSceneServiceSlotCount, "the kit's slot count must match the engine's");
static_assert(kMaxSceneUniformBytes == kSceneGlobalUniformBlockSize, "the kit's uniform limit must match the engine's");

void Frame::PublishToScene(SceneSlot slot, Texture texture)
{
    if (m_context == nullptr || m_context->blackboard == nullptr) {
        ReportOnce("PublishToScene() needs a frame that belongs to a render callback");
        return;
    }
    if (!slot.IsValid()) {
        ReportOnce("PublishToScene(): the scene slot is invalid");
        return;
    }
    if (!Owns(texture)) {
        ReportOnce("PublishToScene(): the texture is invalid or was made by another frame callback");
        return;
    }
    m_context->blackboard->Publish<rg::TextureHandle>(SceneServiceBlackboardKey(slot.index, m_context->view), texture.handle);
}

void Frame::SetSceneUniformBytes(std::span<const std::byte> bytes)
{
    if (m_context == nullptr || m_context->core == nullptr) {
        ReportOnce("SetSceneUniforms() needs a frame that belongs to a render callback");
        return;
    }
    m_context->core->GetSceneServicesDescriptorSet().UpdateGlobalUniformBlock(m_context->view, bytes);
}

} // namespace gte::fx
