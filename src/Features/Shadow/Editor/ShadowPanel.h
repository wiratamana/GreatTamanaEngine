#pragma once

namespace gte {

struct ShadowSettings;
struct ShadowViewSnapshot;

// Draws the Shadow settings into the current window. Returns true when a control changed `edit`.
bool BuildShadowPanel(ShadowSettings& edit, const ShadowViewSnapshot& game, const ShadowViewSnapshot& scene);

} // namespace gte
