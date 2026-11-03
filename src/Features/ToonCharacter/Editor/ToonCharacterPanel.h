#pragma once

namespace gte {

struct ToonCharacterSettings;
struct ToonCharacterViewSnapshot;

// Draws the "ToonCharacter" window and edits `edit` in place. Returns true when a control changed it.
bool BuildToonCharacterPanel(
    ToonCharacterSettings& edit, const ToonCharacterViewSnapshot& game, const ToonCharacterViewSnapshot& scene);

} // namespace gte
