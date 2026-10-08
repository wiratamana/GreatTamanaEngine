#include "RenderGraphTexturePreview.h"

#include <backends/imgui_impl_vulkan.h>

namespace gte {

void RenderGraphTexturePreview::Request(VkDevice device, std::string resourceName, Texture2D texture)
{
    Release(); // Uses the PREVIOUS m_device - correct even if `device` somehow differs call to call.
    m_device = device;
    m_texture.emplace(std::move(texture));
    m_resourceName = std::move(resourceName);
    // CreateTexture2D() leaves the image in SHADER_READ_ONLY_OPTIMAL - safe
    // to wrap directly, this is a freshly-owned, non-pooled texture.
    m_descriptor = ImGui_ImplVulkan_AddTexture(
        m_texture->Sampler(), m_texture->View(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void RenderGraphTexturePreview::Release()
{
    // Wait ONLY when there is something live and a device is actually
    // known - a descriptor/texture this old may still be referenced by an
    // in-flight command buffer from a recent frame.
    if ((m_texture.has_value() || m_descriptor != VK_NULL_HANDLE) && m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }
    if (m_descriptor != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(m_descriptor);
        m_descriptor = VK_NULL_HANDLE;
    }
    m_texture.reset();
    m_resourceName.clear();
}

} // namespace gte
