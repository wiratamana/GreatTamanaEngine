#pragma once

#include "../../Core/GraphicsHandles.h"
#include "../../Math/Mat4.h"
#include "../../Renderer/Pipeline.h"

#include <volk.h>

#include <memory>

namespace gte {

class Renderer;

// Draws a full-screen sky background wherever the depth buffer still shows
// the frame's own clear value (1.0, i.e. "nothing real was ever drawn
// here"), sampling the per-view Sky-View LUT via a per-pixel camera ray
// reconstructed from that view's own inverse view-projection matrix.
//
// Mirrors src/Editor/SceneGridRenderer.h/.cpp's own PATTERN (a dedicated
// pipeline, full-screen triangle synthesized purely from gl_VertexIndex
// with zero vertex input) built via Renderer's configurable pipeline API -
// but DELIBERATELY does NOT live under src/Editor/: atmosphere scattering
// is an always-compiled core rendering feature with no GTE_ENABLE_EDITOR
// dependency, running in both the Game View and the Editor-only Scene View.
//
// Depth handling: the pipeline's own depth-compare op is EQUAL (not LESS),
// and the vertex shader writes a fixed NDC depth of exactly 1.0 - so this
// pass's fragment only ever survives the depth test at a pixel whose depth
// is still EXACTLY the frame's own clear value. Depth WRITE stays disabled -
// nothing is ever meant to occlude the sky itself.
//
// Owned by AtmosphereLutRenderer - see that class's own DrawSkyBackground()
// method, the sole way this class is ever invoked in production.
class AtmosphereSkyBackgroundRenderer {
public:
    AtmosphereSkyBackgroundRenderer() = default;
    ~AtmosphereSkyBackgroundRenderer();

    AtmosphereSkyBackgroundRenderer(const AtmosphereSkyBackgroundRenderer&) = delete;
    AtmosphereSkyBackgroundRenderer& operator=(const AtmosphereSkyBackgroundRenderer&) = delete;
    AtmosphereSkyBackgroundRenderer(AtmosphereSkyBackgroundRenderer&&) = delete;
    AtmosphereSkyBackgroundRenderer& operator=(AtmosphereSkyBackgroundRenderer&&) = delete;

    // Records one full-screen-triangle draw call for the sky background
    // directly against `cmd`, INSIDE the caller's own already-open
    // vkCmdBeginRendering bracket (mirrors SceneGridRenderer::Draw()'s own
    // contract exactly). `viewProjection` is the SAME combined
    // view * projection matrix the real scene geometry was just rendered
    // with this frame. `skyViewLutView`/`skyViewLutSampler` are THIS view's
    // own Sky-View LUT output - a plain, combined-image-sampler read.
    // `eyeHeightKm`/`planetRadiusKm` are this view's own current values.
    // `skyExposure` is the Editor's "Atmosphere" panel-tunable exposure.
    //
    // A safe no-op (draws nothing) whenever `viewProjection` turns out to be
    // singular (Mat4::TryInverse() fails).
    void Draw(Renderer& renderer, VkCommandBuffer cmd, const Mat4& viewProjection, VkImageView skyViewLutView,
        VkSampler skyViewLutSampler, float eyeHeightKm, float planetRadiusKm, float skyExposure);

    // A real, permanent, hand-verified identifier for the vert+frag shader
    // pair this class's own EnsurePipeline() actually loads - mirrors
    // Pipeline::DebugName()'s own "X.vert/X.frag" naming convention.
    static constexpr const char* ShaderDebugName() noexcept
    {
        return "AtmosphereSkyBackground.vert/AtmosphereSkyBackground.frag";
    }

    // Releases the pipeline/descriptor-set-layout (if built) - waits for
    // the GPU to be idle first, mirrors SceneGridRenderer::Reset()'s own
    // identical reasoning/contract.
    void Reset();

private:
    void EnsurePipeline(Renderer& renderer);

    VkDevice m_device = VK_NULL_HANDLE;
    Renderer* m_renderer = nullptr; // Non-owning - needed to destroy m_setLayoutHandle.

    // Real descriptor-SET layout, still built by hand - needed for
    // AllocateComputeDescriptorSet(), which the opaque pipeline API does not
    // replace (that API only builds pipeline-compatible set LAYOUTS).
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

    // Built via Renderer's configurable pipeline API - see EnsurePipeline().
    std::unique_ptr<Pipeline> m_pipeline;
    DescriptorSetLayoutHandle m_setLayoutHandle = DescriptorSetLayoutHandle::Invalid;
};

} // namespace gte
