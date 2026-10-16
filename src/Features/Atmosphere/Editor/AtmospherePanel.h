#pragma once

namespace gte {

struct EditorContext;
struct AtmosphereSettings;

// Stateless panel that edits the atmosphere knobs by reference, once per frame from the editor layer.
void BuildAtmospherePanel(EditorContext& ctx, AtmosphereSettings& settings);

} // namespace gte
