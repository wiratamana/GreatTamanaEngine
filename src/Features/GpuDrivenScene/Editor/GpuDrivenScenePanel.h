#pragma once

namespace gte {

struct GpuDrivenSceneSettings;
struct GpuDrivenSceneViewSnapshot;
struct CullTimeSplits;

// Draws the "Culling" window and edits `edit` in place. Returns true when a control changed it.
// `times` is the per-view GPU time split of the last frame.
bool BuildGpuDrivenScenePanel(GpuDrivenSceneSettings& edit, bool supported, const GpuDrivenSceneViewSnapshot& game,
    const GpuDrivenSceneViewSnapshot& scene, const CullTimeSplits& times);

} // namespace gte
