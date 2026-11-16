#pragma once

namespace gte {

struct GrassSettings;
struct GrassViewSnapshot;

// Draws the "Grass" window content and edits `edit` in place. Returns true when a control changed it.
bool BuildGrassPanel(GrassSettings& edit, const GrassViewSnapshot& game, const GrassViewSnapshot& scene);

} // namespace gte
