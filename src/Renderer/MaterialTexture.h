#pragma once

#include "Buffer.h"
#include "MaterialImageRole.h"
#include "MaterialParams.h"
#include "MaterialSetPool.h"
#include "Texture2D.h"

#include <array>
#include <memory>

namespace gte {

// Images of one material, indexed by MaterialImageRole. A null entry means "use the default image of that slot".
using MaterialImages = std::array<std::shared_ptr<Texture2D>, kMaterialImageRoleCount>;

// One material: set 0 binding 0 base color, 1 params, 2 normal, 3 metallic-roughness, 4 occlusion.
// Owns its set (freed on destruction) and shares its images. Move-only.
struct MaterialTexture {
    MaterialImages images; // All non-null once created (defaults substituted).
    Buffer params;
    MaterialSetLease set;
};

} // namespace gte
