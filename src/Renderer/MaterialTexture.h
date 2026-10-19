#pragma once

#include "Buffer.h"
#include "MaterialParams.h"
#include "Texture2D.h"

#include "../Core/GraphicsHandles.h" // DescriptorSetHandle

#include <volk.h>

#include <memory>

namespace gte {

// Textured-shader material: set 0 with the base-color image (binding 0) and MaterialParams (binding 1).
// The image is shared between materials; the set lives in a persistent pool. Move-only.
struct MaterialTexture {
    std::shared_ptr<Texture2D> texture;
    Buffer params;
    DescriptorSetHandle descriptorSet = DescriptorSetHandle::Invalid;
};

} // namespace gte
