#pragma once

#include "Buffer.h"
#include "MaterialParams.h"
#include "Texture2D.h"

#include "../Core/GraphicsHandles.h" // DescriptorSetHandle

#include <volk.h>

#include <memory>

namespace gte {

// A material as the textured shaders see it: set 0 with the base-color image (binding 0) and the
// MaterialParams block (binding 1). The set comes from a persistent pool and is never freed
// individually. The image is shared by every material that uses it. Move-only.
struct MaterialTexture {
    std::shared_ptr<Texture2D> texture;
    Buffer params;
    DescriptorSetHandle descriptorSet = DescriptorSetHandle::Invalid;
};

} // namespace gte
