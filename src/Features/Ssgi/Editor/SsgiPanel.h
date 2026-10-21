#pragma once

namespace gte {

struct SsgiSettings;
struct SsgiViewSnapshot;

// Draws the "Ssgi" window and edits `edit` in place. Returns true when a control changed it.
bool BuildSsgiPanel(SsgiSettings& edit, const SsgiViewSnapshot& game, const SsgiViewSnapshot& scene);

} // namespace gte
