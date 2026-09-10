#pragma once

#include "../../Math/Mat4.h"

#include <volk.h>

namespace gte {

class Renderer;

// Atmosphere Scattering + Aerial Perspective campaign, Phase 7
// (task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASE7_SKY_BACKGROUND_AND_COMPOSITE_PASSES_v1.md)
// - draws a full-screen sky background wherever the depth buffer still
// shows the frame's own clear value (1.0, i.e. "nothing real was ever
// drawn here" - this engine clears depth to 1.0 every frame and depth-
// tests with VK_COMPARE_OP_LESS everywhere else, see AGENTS.md's "Render
// Target Format Matching"), sampling the per-view Sky-View LUT (Phase 5)
// via a per-pixel camera ray reconstructed from that view's own inverse
// view-projection matrix.
//
// Mirrors src/Editor/SceneGridRenderer.h/.cpp's own PATTERN exactly
// (bypasses Renderer::CreatePipeline()/CreateMesh()/Submit() entirely,
// builds its own dedicated VkPipeline/VkPipelineLayout directly, draws a
// full-screen triangle synthesized purely from gl_VertexIndex with zero
// vertex input) - but DELIBERATELY does NOT live under src/Editor/ like
// SceneGridRenderer does (a documented deviation from this phase's own
// strategy document's literal file-path suggestion): per this campaign's
// own Locked Design Decision 4 (ATMOSPHERE_PHASE0_MASTER_STRATEGY_v1.md),
// atmosphere scattering is an ALWAYS-COMPILED core rendering feature with
// no GTE_ENABLE_EDITOR dependency at all, and this pass must run in BOTH
// the Game View (which still renders even in a -DGTE_ENABLE_EDITOR=OFF
// release build, straight into the swapchain via AddPresentPass()'s own
// direct-Game-render branch) and the Editor-only Scene View - unlike
// SceneGridRenderer, which is genuinely Scene-View-only and Editor-gated.
//
// Depth handling (CONFIRMED, see this phase's own strategy document's
// "Revision Notes"): the pipeline's own depth-compare op is
// VK_COMPARE_OP_EQUAL (not LESS), and the vertex shader writes a fixed NDC
// depth of exactly 1.0 (gl_Position.z = gl_Position.w) - so this pass's
// fragment only ever survives the depth test at a pixel whose depth is
// still EXACTLY the frame's own clear value, i.e. nothing real (including
// this same pass itself, or a later same-frame overlay like
// SceneGridRenderer's own grid) was ever drawn there. Depth WRITE stays
// disabled - matches SceneGridRenderer's own rule; there is nothing
// further behind the sky to occlude.
//
// Owned by AtmosphereLutRenderer (src/Renderer/Atmosphere/AtmosphereLutRenderer.h)
// - see that class's own DrawSkyBackground() method, the sole way this
// class is ever invoked in production.
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
    // contract exactly - the caller is responsible for having already set
    // a viewport/scissor covering the render target and for this being
    // called AFTER the real scene geometry draws, per this phase's own
    // Step 3.2). `viewProjection` is the SAME combined view * projection
    // matrix the real scene geometry was just rendered with this frame
    // (the active ECS Camera's for Game View, EditorCamera's for Scene
    // View). `skyViewLutView`/`skyViewLutSampler` are THIS view's own
    // Sky-View LUT output (Phase 5) - a plain, combined-image-sampler
    // read, exactly like a fragment shader sampling a MaterialTexture
    // today. `eyeHeightKm`/`planetRadiusKm` are this view's own current
    // values (the exact same ones already fed into this frame's
    // AtmosphereFrameUniforms/AddSkyViewLutPass() call - see
    // AtmosphereLutRenderer::DrawSkyBackground()). `skyExposure` (Phase 8 -
    // ATMOSPHERE_PHASE8_SUN_ECS_AND_EDITOR_CONTROLS_v1.md) is the Editor's
    // "Atmosphere" panel-tunable replacement for what used to be a fixed
    // `kSkyExposure` constant in AtmosphereSkyBackground.frag.
    //
    // A safe no-op (draws nothing) whenever `viewProjection` turns out to
    // be singular (Mat4::TryInverse() fails) - mirrors SceneGridRenderer's
    // own identical guard against a degenerate camera matrix.
    void Draw(Renderer& renderer, VkCommandBuffer cmd, const Mat4& viewProjection, VkImageView skyViewLutView,
        VkSampler skyViewLutSampler, float eyeHeightKm, float planetRadiusKm, float skyExposure);

    // Releases the pipeline/pipeline layout/descriptor-set-layout (if
    // built) - waits for the GPU to be idle first, mirrors
    // SceneGridRenderer::Reset()'s own identical reasoning/contract.
    void Reset();

private:
    void EnsurePipeline(Renderer& renderer);

    VkDevice m_device = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
};

} // namespace gte
