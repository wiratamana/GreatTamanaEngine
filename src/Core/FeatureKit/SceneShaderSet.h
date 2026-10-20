#pragma once

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

    struct Impl;
    explicit SceneShaderSet(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> m_impl;
};

} // namespace gte::fx
