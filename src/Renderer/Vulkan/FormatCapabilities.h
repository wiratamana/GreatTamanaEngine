#pragma once

#include <volk.h>

namespace gte {

// Queries whether `format` supports VK_IMAGE_USAGE_STORAGE_BIT (i.e. its
// optimalTilingFeatures include VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) on
// `physicalDevice` - the exact same "ask the device, never assume/hardcode"
// discipline VulkanDevice::PickDepthFormat() already applies to depth
// formats (see AGENTS.md, "Render Target Format Matching"), just for a
// different feature bit.
//
// This exists because NOT every VkFormat supports storage-image use on
// every driver/GPU - VK_FORMAT_R8G8B8A8_UNORM (Texture2D's own fixed
// format) is a safe, broadly-supported choice for a NEW storage-capable
// texture with no other format constraint, but a RenderTexture's
// default/negotiated swapchain color format (commonly
// VK_FORMAT_B8G8R8A8_UNORM) is genuinely NOT guaranteed to support it -
// see COMPUTE_PHASE1_RESOURCE_VOCABULARY_STRATEGY_v2.md's Step 6 for the
// full reasoning. Callers requesting storage-image access
// (RenderTexture/Texture2D's `allowStorageImageAccess` constructor
// parameter, wired up via GpuResourceFactory::CreateRenderTexture()/
// CreateTexture2D()) MUST check this first and fail loudly (a thrown
// std::runtime_error, never a silent fallback) if it reports false.
bool SupportsStorageImageUsage(VkPhysicalDevice physicalDevice, VkFormat format);

} // namespace gte
