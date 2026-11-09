#pragma once

namespace gte {

struct VolumetricCloudSettings;
struct VolumetricCloudViewSnapshot;

// Draws the "VolumetricCloud" window and edits `edit` in place. Returns true when a control changed it.
bool BuildVolumetricCloudPanel(
    VolumetricCloudSettings& edit, const VolumetricCloudViewSnapshot& game, const VolumetricCloudViewSnapshot& scene);

} // namespace gte
