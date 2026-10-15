#pragma once

#include "Texture2D.h"

#include "../Core/GraphicsHandles.h" // DescriptorSetHandle

#include <volk.h>

namespace gte {

// A Texture2D plus the shared-layout descriptor set (set 0, binding 0, combined sampler) already written for it.
// The set comes from a persistent pool and is never freed individually. Move-only.
struct MaterialTexture {
    Texture2D texture;
    DescriptorSetHandle descriptorSet = DescriptorSetHandle::Invalid;
};

} // namespace gte
