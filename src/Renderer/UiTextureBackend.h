#pragma once
#include <volk.h>

#include <functional>

namespace gte {

// Bridges Renderer::GetUiTextureHandle()/ReleaseUiTextureHandle() to a real
// ImGui Vulkan backend without Renderer itself ever including an ImGui
// header - gte_core (which Renderer is part of) builds with no ImGui
// dependency at all in a headless/Player build. Editor startup code (once
// it owns a real ImGui Vulkan backend) installs the bridge here exactly
// once, before any GetUiTextureHandle() call.
void SetUiTextureBackend(
    std::function<VkDescriptorSet(VkSampler, VkImageView)> addFn, std::function<void(VkDescriptorSet)> removeFn);

// Renderer-internal only. Returns VK_NULL_HANDLE if no backend was ever
// installed (e.g. a headless Player build) rather than asserting - a
// missing UI-texture backend is a normal, expected state outside the Editor.
VkDescriptorSet AddUiTexture(VkSampler sampler, VkImageView view);
void RemoveUiTexture(VkDescriptorSet set);

} // namespace gte
