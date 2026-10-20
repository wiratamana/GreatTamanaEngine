#pragma once

namespace gte {

struct VolumetricLightSettings;
struct VolumetricLightViewSnapshot;

// Draws the "VolumetricLight" window and edits `edit` in place. Returns true when a control changed it.
bool BuildVolumetricLightPanel(
    VolumetricLightSettings& edit, const VolumetricLightViewSnapshot& game, const VolumetricLightViewSnapshot& scene);

} // namespace gte
