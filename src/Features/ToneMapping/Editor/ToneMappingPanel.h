#pragma once

namespace gte {

struct ToneMappingSettings;
struct ToneMappingViewSnapshot;

// Draws the "ToneMapping" window and edits `edit` in place. Returns true when a control changed it.
bool BuildToneMappingPanel(
    ToneMappingSettings& edit, const ToneMappingViewSnapshot& game, const ToneMappingViewSnapshot& scene);

} // namespace gte
