#pragma once

#include "Texture2D.h"

#include <volk.h>

namespace gte {

// Bundles a Texture2D with the VkDescriptorSet (one combined-image-sampler
// binding, set = 0, binding = 0) already written to point at it - built once
// by GpuResourceFactory::CreateMaterialTexture2D() against the ONE shared
// material descriptor-set-layout every textured Pipeline is built with (see
// GpuResourceFactory::MaterialDescriptorSetLayout() and Pipeline.h's
// VertexLayout::PositionNormalUv), so any MaterialTexture can be bound
// directly against any textured Pipeline's layout with no further wiring -
// this is the "material" half of a per-submesh draw (see
// ECS/Components/MeshRenderer.h::texture and RenderSystem::Draw()), the
// Mesh-equivalent primitive for "what to sample" rather than "what to draw".
//
// The descriptor set itself is allocated from GpuResourceFactory's own
// persistent m_materialDescriptorPool and is NEVER individually freed - only
// ever destroyed implicitly, all at once, when that pool itself is destroyed
// alongside the whole GpuResourceFactory/Renderer - so this struct owns no
// Vulkan handle of its own to release besides Texture2D's own (RAII-owned)
// image/view/sampler. Move-only, exactly like Texture2D itself (Texture2D
// has no copy constructor - see Texture2D.h).
struct MaterialTexture {
    Texture2D texture;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};

} // namespace gte
