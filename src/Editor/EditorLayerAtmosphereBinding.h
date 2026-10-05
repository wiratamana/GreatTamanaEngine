#pragma once

namespace gte {

class IEditorLayer;
class AtmosphereFeature;
class Renderer;

namespace rg {
class RenderGraph;
} // namespace rg

// Hands a live AtmosphereFeature& (plus the stable Renderer&/
// const rg::RenderGraph& EditorHost already owns) to the concrete,
// ImGui-backed Editor layer - constructs and registers an
// AtmospherePluginPanelModule exactly once. MUST be called AFTER every
// EditorPanelRegistry::RegisterBuiltinPanelName() call has already run
// (built-ins register first, plugins after). MUST be called exactly once,
// ever, per process - the concrete implementation refuses (logs loudly and
// returns, in every build configuration) on a second call. A caller must
// only ever pass the real ImGui-backed implementation here (never a build
// linking NullEditorLayer instead).
void BindAtmosphereFeatureForEditorLayer(IEditorLayer& layer, AtmosphereFeature& feature,
    Renderer& renderer, const rg::RenderGraph& renderGraph);

} // namespace gte
