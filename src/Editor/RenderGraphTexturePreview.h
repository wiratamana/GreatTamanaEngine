#pragma once

#include "../Renderer/Texture2D.h"

#include <optional>
#include <string>
#include <volk.h>

namespace gte {

// Owns at most one ImGui-sampled texture preview: one Texture2D, one
// VkDescriptorSet. Requesting a new preview always releases the old one
// first. Destructor releases both - no caller-side cleanup required.
//
// Must be destroyed before ImGui_ImplVulkan_Shutdown() runs, like every
// other panel-owned Vulkan resource in this editor.
class RenderGraphTexturePreview {
public:
    RenderGraphTexturePreview() = default;
    ~RenderGraphTexturePreview() { Release(); }

    RenderGraphTexturePreview(const RenderGraphTexturePreview&) = delete;
    RenderGraphTexturePreview& operator=(const RenderGraphTexturePreview&) = delete;

    // `device` is captured here, once, and reused by every later Release().
    void Request(VkDevice device, std::string resourceName, Texture2D texture);

    // Waits for the GPU only if there is actually something live to
    // release - an empty Release() (e.g. at shutdown, after an already-
    // empty preview) is a free no-op, never a redundant stall.
    void Release();

    VkDescriptorSet Descriptor() const noexcept { return m_descriptor; }
    const std::string& ResourceName() const noexcept { return m_resourceName; }
    int Width() const noexcept { return m_texture.has_value() ? m_texture->Width() : 0; }
    int Height() const noexcept { return m_texture.has_value() ? m_texture->Height() : 0; }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    std::string m_resourceName;
    std::optional<Texture2D> m_texture;
    VkDescriptorSet m_descriptor = VK_NULL_HANDLE;
};

} // namespace gte
