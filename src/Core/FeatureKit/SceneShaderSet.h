#pragma once

#include "../PipelineDesc.h" // ScenePipelineState, CullMode, CompareOp, BlendMode
#include "../../Renderer/VertexLayout.h"

#include <memory>
#include <span>
#include <string_view>

namespace gte {
struct PipelineOverrideSet;
} // namespace gte

namespace gte::fx {

class SceneShaderSet;

namespace detail {
struct FrameContext;

// Builds the set's pipelines when needed. True when the set is ready to draw.
bool PrepareSceneShaders(SceneShaderSet& set, FrameContext& context);

// The per-vertex-layout pipelines of a ready set.
const PipelineOverrideSet& OverridesOf(const SceneShaderSet& set) noexcept;

// True when the set's pipelines read the scene services set, so a draw must bind it.
bool BindsSceneServices(const SceneShaderSet& set) noexcept;
} // namespace detail

// The vertex layouts a depth-only set builds a pipeline for, in build order.
std::span<const VertexLayout> DepthOnlyLayouts() noexcept;

// Vertex shader name suffix of `layout`; empty when the layout has no depth-only shader.
std::string_view DepthOnlyShaderSuffix(VertexLayout layout) noexcept;

// The pipelines a scene redraw uses instead of each object's own. Built lazily; a failed build is rolled
// back and retried on a growing cooldown. Registered pipelines are removed on destruction.
class SceneShaderSet {
public:
    // Depth-only draw: shaders/<base><suffix>.vert for each suffix of DepthOnlyShaderSuffix()
    // (PositionColor, PositionNormal, PositionNormalUv, PositionNormalUvTangent) plus shaders/<base>.frag.
    static SceneShaderSet DepthOnly(const char* baseName);

    // Colour draw of position+normal+uv meshes (skinned ones included): shaders/<base>.vert and shaders/<base>.frag.
    // Binds the material set (set 0) and the scene services set (set 1). Other vertex layouts are skipped by the draw.
    //   fx::SceneShaderSet::Shaded("Look", { .cull = CullMode::None });
    static SceneShaderSet Shaded(const char* baseName, const ScenePipelineState& state = {});

    ~SceneShaderSet();
    SceneShaderSet(SceneShaderSet&&) noexcept;
    SceneShaderSet& operator=(SceneShaderSet&&) noexcept;
    SceneShaderSet(const SceneShaderSet&) = delete;
    SceneShaderSet& operator=(const SceneShaderSet&) = delete;

    // True once every pipeline is built.
    bool Ready() const noexcept;

private:
    friend bool detail::PrepareSceneShaders(SceneShaderSet&, detail::FrameContext&);
    friend const PipelineOverrideSet& detail::OverridesOf(const SceneShaderSet&) noexcept;
    friend bool detail::BindsSceneServices(const SceneShaderSet&) noexcept;

    struct Impl;
    explicit SceneShaderSet(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> m_impl;
};

} // namespace gte::fx
