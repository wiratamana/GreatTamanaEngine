#pragma once

namespace gte {

struct EditorContext;
struct GpuDrivenSceneSettings;
struct GpuDrivenSceneViewSnapshot;

// Draws the "GPU Driven Scene" window and edits `edit` in place. Returns true when a control changed it.
bool BuildGpuDrivenScenePanel(EditorContext& ctx, GpuDrivenSceneSettings& edit, bool supported,
    const GpuDrivenSceneViewSnapshot& game, const GpuDrivenSceneViewSnapshot& scene);

} // namespace gte
