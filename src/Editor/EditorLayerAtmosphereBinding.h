#pragma once

namespace gte {

class IEditorLayer;
class AtmosphereFeature;

// Hands a live AtmosphereFeature& to the concrete, ImGui-backed Editor
// layer so its own feature panel can keep editing live settings - called
// exactly once, from EditorHost's constructor, immediately after
// CreateEditorLayer() returns. A caller must only ever pass the real
// ImGui-backed implementation here (never a build linking NullEditorLayer
// instead).
void BindAtmosphereFeatureForEditorLayer(IEditorLayer& layer, AtmosphereFeature& feature);

} // namespace gte
