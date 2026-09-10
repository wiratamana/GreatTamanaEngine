#pragma once

#include <volk.h>

namespace gte {

// Plain, non-owning description of a 3D image, the VolumeTexture (see
// VolumeTexture.h) counterpart of RenderTarget.h (used for 2D color+depth
// targets). Deliberately just Vulkan handles + metadata, no ownership -
// exactly mirrors RenderTarget's own shape/reasoning, just for a single 3D
// image with no depth-companion concept at all (a volume texture has no
// depth buffer equivalent).
//
// This is what lets RenderGraphBuilder.h stay decoupled from VolumeTexture's
// own (heavier) header - it `#include`s ONLY this file, never
// "../VolumeTexture.h" - exactly the same layering discipline RenderTarget.h
// already established for RenderGraphBuilder::ImportTexture() (see
// ATMOSPHERE_PHASE2_VOLUME_TEXTURE_RENDERGRAPH_SUPPORT_v1.md, Step 3.3).
struct VolumeTarget {
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkExtent3D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;
};

} // namespace gte
