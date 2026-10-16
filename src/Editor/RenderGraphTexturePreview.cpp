#include "RenderGraphTexturePreview.h"

#include "../Renderer/Renderer.h"

#include <utility>

namespace gte {

void RenderGraphTexturePreview::Request(Renderer& renderer, std::string resourceName, Texture2D texture)
{
    Release(); // Uses the PREVIOUS m_renderer - correct even if `renderer` differs from call to call.
    m_renderer = &renderer;
    m_texture = std::make_unique<Texture2D>(std::move(texture));
    m_resourceName = std::move(resourceName);
    // CreateTexture2D() leaves the image in SHADER_READ_ONLY_OPTIMAL, so a freshly owned texture is safe to wrap.
    m_descriptor = m_renderer->GetUiTextureHandle(*m_texture);
}

void RenderGraphTexturePreview::Release()
{
    // A recent frame may still sample the texture, so the renderer retires both after the GPU is done.
    if ((m_texture || m_descriptor != UiTextureHandle::Invalid) && m_renderer != nullptr) {
        m_renderer->RetireUiTexture(m_descriptor, std::move(m_texture));
    }
    m_descriptor = UiTextureHandle::Invalid;
    m_texture.reset();
    m_resourceName.clear();
}

} // namespace gte
