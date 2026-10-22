#pragma once

namespace gte {

struct EditorContext;
struct AtmosphereSettings;
struct AtmosphereAerialSnapshot;

// Stateless panel that edits `edit` once per frame from the editor layer. True when a value changed.
// `effective` is what the Game view's aerial perspective really used last frame.
bool BuildAtmospherePanel(EditorContext& ctx, AtmosphereSettings& edit, const AtmosphereAerialSnapshot& effective);

} // namespace gte
