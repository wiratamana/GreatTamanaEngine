#pragma once

namespace gte {

struct TemporalAaSettings;
struct TemporalAaViewSnapshot;

// Draws the "TemporalAa" window and edits `edit` in place. Returns true when a control changed it.
bool BuildTemporalAaPanel(
    TemporalAaSettings& edit, const TemporalAaViewSnapshot& game, const TemporalAaViewSnapshot& scene);

} // namespace gte
