#pragma once

namespace gte {

struct GpuDrivenSceneSettings;
struct GpuDrivenSceneViewSnapshot;

// Draws the "Culling" window and edits `edit` in place. Returns true when a control changed it.
bool BuildGpuDrivenScenePanel(GpuDrivenSceneSettings& edit, bool supported,
    const GpuDrivenSceneViewSnapshot& game, const GpuDrivenSceneViewSnapshot& scene);

} // namespace gte
