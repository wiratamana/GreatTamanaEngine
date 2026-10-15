#pragma once
#include <volk.h>

#include <functional>

namespace gte {

// Bridges Renderer::GetUiTextureHandle()/ReleaseUiTextureHandle() to an ImGui Vulkan backend without
// the renderer including ImGui (a standalone build has no ImGui). The editor installs it once at startup,
// before any GetUiTextureHandle() call.
void SetUiTextureBackend(
    std::function<VkDescriptorSet(VkSampler, VkImageView)> addFn, std::function<void(VkDescriptorSet)> removeFn);

// Renderer-internal. Returns VK_NULL_HANDLE when no backend is installed, a normal state outside the editor.
VkDescriptorSet AddUiTexture(VkSampler sampler, VkImageView view);
void RemoveUiTexture(VkDescriptorSet set);

} // namespace gte
