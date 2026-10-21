#pragma once

namespace gte {

struct SceneLightingSettings;

// Draws the "SceneLighting" window and edits `edit` in place. Returns true when a control changed it.
bool BuildSceneLightingPanel(SceneLightingSettings& edit);

} // namespace gte
