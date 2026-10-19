#pragma once

namespace gte {

struct EditorContext;
struct GtaoSettings;
struct GtaoViewSnapshot;

// Draws the "Gtao" window and edits `edit` in place. Returns true when a control changed it.
bool BuildGtaoPanel(EditorContext& ctx, GtaoSettings& edit, const GtaoViewSnapshot& game, const GtaoViewSnapshot& scene);

} // namespace gte
