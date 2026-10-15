#pragma once

#include "Renderer/VolumeTexturePreviewRenderer.h"

#include <string_view>

namespace gte {

// Picks the preview decoding from the registered volume texture name (editor policy, pure).
// Only the aerial perspective volume prefix selects the frustum view; everything else is generic.
inline VolumeTexturePreviewInterpretation SelectVolumeTexturePreviewInterpretation(std::string_view volumeTextureName)
{
    constexpr std::string_view kAerialPerspectivePrefix = "AtmosphereAerialPerspectiveVolume";
    return volumeTextureName.starts_with(kAerialPerspectivePrefix)
        ? VolumeTexturePreviewInterpretation::AerialPerspectiveFrustum
        : VolumeTexturePreviewInterpretation::GenericDensityInAlpha;
}

} // namespace gte
