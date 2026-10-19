#pragma once

#include <memory>

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

// The pipelines a scene redraw uses instead of each object's own. Built lazily; a failed build is rolled
// back and retried on a growing cooldown. Registered pipelines are removed on destruction.
class SceneShaderSet {
public:
    // Depth-only draw: shaders/<base>PositionColor.vert, <base>PositionNormal.vert, <base>PositionNormalUv.vert
    // plus shaders/<base>.frag, one pipeline per vertex layout.
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
