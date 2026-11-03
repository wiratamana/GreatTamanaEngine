#pragma once

#include "../../Assets/MaterialData.h"
#include "../../Renderer/MaterialParams.h"
#include "../Lighting/ToonClass.h"

#include <cstdint>
#include <span>

namespace gte {

// PMX edge size that maps to an outline width scale of 1.
inline constexpr float kToonReferenceEdgeSize = 0.54f;

// First matching rule wins. `textures` is the model's texture list; the toon ramp is told apart by its file name
// ("skin" / "hair"). Rules follow miHoYo MMD naming; other models fall back to Cloth.
ToonClass ClassifyToonMaterial(const Material& material, std::span<const MaterialTextureRef> textures) noexcept;

// The outline pass draws a material when its edge flag is set, except for classes that never get an outline.
bool ToonClassDrawsEdge(ToonClass toonClass, bool drawEdgeFlag) noexcept;

// Material params carrying class id, edge width scale and draw-edge flag.
MaterialParams MakeToonParamsFor(const Material& material, std::span<const MaterialTextureRef> textures) noexcept;

} // namespace gte
