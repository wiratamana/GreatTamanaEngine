#pragma once

// task_manager/mrt-1 campaign (Multi-Render-Target / G-Buffer support),
// PHASE4 (PHASE4_GBUFFER_VALIDATION_PASS_AND_SHADER.md) - this campaign's
// own first REAL consumer of PHASE1-3's new MRT mechanism: a small,
// additive, debug-only "GBuffer Validation" pass that writes TWO color
// attachments (albedo/normal) in a single draw, through PHASE3's new
// N-format Renderer::CreatePipeline() overload, plus a second, small
// compute pass that reads ONE of those two textures back and copies it
// into its own separate, independently-inspectable output - proving both
// halves of the mechanism end-to-end (N targets written in one pass; a
// later pass reading one of N outputs, cross-pass, barrier-synchronized
// automatically by the existing RenderGraphBarrierPlanner, with zero new
// barrier code).
//
// Mirrors src/Editor/ComputeBlurValidation.h/.cpp's shape almost exactly
// (lazy init against a live Renderer&, persistent owned RenderTextures,
// declared via RenderGraphBuilder::AddRenderPass(), ViewScope::SceneView +
// RenderPassCategory::Debug, toggled by its own small Scene-panel
// checkbox, zero effect on the default Game View) - the one real
// structural difference: this pass's FIRST half is a GRAPHICS pass (a
// real fragment shader with two `layout(location = N) out` color
// attachments via WriteColorAttachment(), the whole point of proving the
// MRT mechanism), not a compute one. The SECOND half (the "read one
// output back" pass) IS a compute pass, deliberately chosen as the
// smaller diff over a second graphics pass (see PHASE4_COMPLETION_REPORT.md
// for the full reasoning) - it reuses ComputeBlurValidation's own
// descriptor-set-layout/dispatch shape almost verbatim, just with a
// trivial imageLoad/imageStore copy shader (Shaders/GBufferCopy.comp)
// instead of a blur kernel.
//
// A REAL, non-obvious wrinkle (see PHASE4_GBUFFER_VALIDATION_PASS_AND_SHADER.md's
// own Step 2 for the full analysis): `Pipeline`'s constructor (via
// `Renderer::CreatePipeline()`, including PHASE3's new N-format overload)
// ALWAYS unconditionally enables depth test AND depth write
// (VK_COMPARE_OP_LESS) and ALWAYS declares exactly one real vertex-input
// binding - unlike `AtmosphereSkyBackgroundRenderer`/`SceneGridRenderer`,
// this class goes through the STANDARD `Renderer::CreatePipeline()`/
// `Pipeline` path (the strategy document's own weakly-recommended,
// simpler option, chosen here specifically so PHASE3's new N-format
// Pipeline capability is genuinely exercised by this campaign's own first
// real consumer, rather than bypassed) - which means this class must (a)
// pair its color writes with a real (here: scratch/unused) depth
// attachment via WriteDepthStencilAttachment() - reusing m_albedoOutput's
// own companion DepthBuffer (every RenderTexture already owns one - see
// RenderTexture.h) costs nothing extra to allocate - and (b) bind a real
// (throwaway/unused) 3-vertex Mesh before its own draw call, since the
// mandatory vertex binding must have SOMETHING bound - see
// m_dummyTriangle below. The vertex shader itself
// (Shaders/GBufferValidation.vert) never reads this dummy data at all -
// it derives a full-screen triangle purely from gl_VertexIndex, mirroring
// Shaders/AtmosphereSkyBackground.vert's own technique exactly (an unused
// vertex attribute is legal in Vulkan; only a shader `in` variable with
// nothing bound for it would be illegal, and this shader declares none).
//
// Owned by ImGuiEditorLayer, alongside m_blurValidation - exposed to
// Application purely through two new IEditorLayer methods
// (AddGBufferValidationPass()/FinalizeGBufferValidationForSampling(), see
// EditorLayer.h), mirroring AddBlurValidationPass()'s own exact boundary.
// A small, permanent, clearly-labeled "Show GBuffer Validation (debug)"
// checkbox in the "Scene" panel's own toolbar (see Panels/ScenePanel.cpp)
// toggles whether this pass is even declared at all each frame - kept
// permanently (never deleted after validation), the same discipline as
// ComputeBlurValidation/the Bone Viewer.
//
// This pass does NOT read the Scene View at all (a deliberate
// simplification - see PHASE4's own strategy document, Step 2) - its two
// color outputs are entirely self-contained, procedural test patterns
// derived purely from a full-screen UV varying, with no dependency on
// whatever the Scene View itself is currently showing. This also means
// its RenderPassEvent tag can stay at the default (Opaques) - unlike
// ComputeBlurValidation, there is no real cross-pass read of a Scene-View
// color target here that a future render-pass-4-style effective-order
// change could ever silently mis-order.
//
// `m_albedoOutput`/`m_normalOutput`/`m_visualizedOutput`'s color images
// are all created at an EXPLICIT VK_FORMAT_R8G8B8A8_UNORM (mirrors
// ComputeBlurValidation's own `blurredOutput` format choice exactly - see
// that class's own header comment for the full reasoning).

#include "EditorLayer.h"
#include "../Renderer/ComputeDescriptorSet.h"
#include "../Renderer/ComputePipeline.h"
#include "../Renderer/Mesh.h"
#include "../Renderer/Pipeline.h"
#include "../Renderer/RenderTexture.h"
#include "../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../Renderer/RenderGraph/RenderGraphTypes.h"

#include <volk.h>

#include <optional>

namespace gte {

class Renderer;

// GBufferValidationHandles - see EditorLayer.h's own definition (this
// class reuses that exact same struct as its own AddPass() return type
// directly, rather than defining a second, parallel one here - the two
// must always be byte-for-byte the same shape, so there is only ever ONE
// definition).

class GBufferValidation {
public:
    GBufferValidation() = default;
    ~GBufferValidation();

    GBufferValidation(const GBufferValidation&) = delete;
    GBufferValidation& operator=(const GBufferValidation&) = delete;
    GBufferValidation(GBufferValidation&&) = delete;
    GBufferValidation& operator=(GBufferValidation&&) = delete;

    // Declares this frame's two passes into `builder` (the graphics
    // "GBufferValidation" pass, writing outAlbedo/outNormal in one draw;
    // and the compute "GBufferValidationCopy" pass, reading outAlbedo back
    // and copying it into m_visualizedOutput) - lazily builds this
    // object's own Pipeline/ComputePipeline/RenderTextures/dummy Mesh the
    // first time this is called (needs a live Renderer/VkDevice, so can't
    // happen in the default constructor). `sceneExtent` must be non-zero
    // in both dimensions - the caller is expected to have already checked
    // this (mirrors ComputeBlurValidation::AddPass()'s own identical
    // expectation).
    GBufferValidationHandles AddPass(rg::RenderGraphBuilder& builder, Renderer& renderer, VkExtent2D sceneExtent);

    // Transitions all three outputs from whatever write state AddPass()
    // above leaves them in (ColorAttachmentWrite for albedo/normal,
    // ComputeShaderWrite for the visualized copy) to a real ShaderRead
    // state - mirrors ComputeBlurValidation::FinalizeForSampling() (the
    // compute half) plus RenderPasses.h's own
    // FinalizeRenderTextureForExternalSampling() (the graphics half). Must
    // be called against the SAME command buffer the offscreen
    // RenderGraph::Execute() call just recorded into, AFTER that call
    // returns and BEFORE that command buffer is ended/submitted - see
    // Application::Run(). A safe no-op whenever AddPass() above was not
    // actually called this frame.
    void FinalizeForSampling(VkCommandBuffer cmd);

    // The visualized (copy-of-albedo) output RenderTexture - used by the
    // caller (ImGuiEditorLayer) to (re)create its own ImGui descriptor for
    // the Scene panel's own optional debug preview, mirroring
    // ComputeBlurValidation::OutputTexture() exactly. nullptr before
    // AddPass() has ever been called.
    RenderTexture* OutputTexture() noexcept
    {
        return m_visualizedOutput.has_value() ? &m_visualizedOutput.value() : nullptr;
    }

private:
    void EnsureInitialized(Renderer& renderer, VkExtent2D initialExtent);

    VkDevice m_device = VK_NULL_HANDLE;

    std::optional<RenderTexture> m_albedoOutput;
    std::optional<RenderTexture> m_normalOutput;
    std::optional<RenderTexture> m_visualizedOutput;

    std::optional<Pipeline> m_gbufferPipeline;
    // A real, but throwaway/unused, 3-vertex Mesh - see this file's own
    // header comment on why the standard Pipeline path requires SOMETHING
    // bound at the mandatory vertex binding, even though
    // Shaders/GBufferValidation.vert never reads it.
    std::optional<Mesh> m_dummyTriangle;

    VkDescriptorSetLayout m_copyDescriptorSetLayout = VK_NULL_HANDLE;
    std::optional<ComputePipeline> m_copyPipeline;
    ComputeDescriptorSet m_copyDescriptorSet;

    // See ComputeBlurValidation::m_writtenThisFrame's own doc comment for
    // why this is tracked internally rather than trusted to match the
    // caller's own enable/visibility condition every time.
    bool m_writtenThisFrame = false;
};

} // namespace gte
