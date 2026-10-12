#include "RenderGraphTexturePreview.h"

#include "../Renderer/Renderer.h"

namespace gte {

void RenderGraphTexturePreview::Request(Renderer& renderer, std::string resourceName, Texture2D texture)
{
    Release(); // Uses the PREVIOUS m_renderer - correct even if `renderer` somehow differs call to call.
    m_renderer = &renderer;
    m_texture.emplace(std::move(texture));
    m_resourceName = std::move(resourceName);
    // CreateTexture2D() leaves the image in SHADER_READ_ONLY_OPTIMAL - safe
    // to wrap directly, this is a freshly-owned, non-pooled texture.
    m_descriptor = m_renderer->GetUiTextureHandle(*m_texture);
}

void RenderGraphTexturePreview::Release()
{
    // Wait ONLY when there is something live and a renderer is actually
    // known - a descriptor/texture this old may still be referenced by an
    // in-flight command buffer from a recent frame.
    if ((m_texture.has_value() || m_descriptor != UiTextureHandle::Invalid) && m_renderer != nullptr) {
        m_renderer->WaitForGpuIdle();
    }
    if (m_descriptor != UiTextureHandle::Invalid) {
        m_renderer->ReleaseUiTextureHandle(m_descriptor);
        m_descriptor = UiTextureHandle::Invalid;
    }
    m_texture.reset();
    m_resourceName.clear();
}

} // namespace gte
