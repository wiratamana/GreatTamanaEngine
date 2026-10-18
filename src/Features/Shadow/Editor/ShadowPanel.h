#pragma once

namespace gte {

struct EditorContext;
struct ShadowSettings;
struct ShadowViewSnapshot;

// Draws the "Shadow" window and edits `edit` in place. Returns true when a control changed it.
bool BuildShadowPanel(EditorContext& ctx, ShadowSettings& edit, const ShadowViewSnapshot& game,
    const ShadowViewSnapshot& scene);

} // namespace gte
