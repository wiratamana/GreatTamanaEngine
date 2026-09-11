#pragma once

// Atmosphere Scattering + Aerial Perspective campaign, Phase 3
// (task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASE3_TRANSMITTANCE_LUT_v1.md)
// - the first real, permanent atmosphere compute pass owner. Modeled
// directly on src/Editor/ComputeBlurValidation.h/.cpp's proven shape (see
// that phase's own Step 3): a lazily-initialized ComputePipeline +
// ComputeDescriptorSetLayout + ComputeDescriptorSet, a persistent output
// texture, and an AddXxxPass(RenderGraphBuilder&, ...) -> TextureHandle
// method per LUT.
//
// This class is the SINGLE home for every atmosphere LUT compute pass added
// across Phases 3-6 of this campaign (Transmittance/Multi-Scattering/
// Sky-View/Aerial-Perspective) - do not create a separate, unrelated class
// per LUT; grow this one's public surface one method per phase instead
// (mirroring how src/Renderer/GpuSkinning/GpuSkinningRigCache.h accumulated
// per-model responsibility over several phases of its own campaign).
//
// BINDING CONVENTION for the Transmittance LUT specifically (see the
// strategy document's own "Revision Notes" at its top): binding 0 is
// AtmosphereParametersGpu, bound as a read-only STORAGE buffer (this engine
// has no VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER descriptor support anywhere
// today - see Vulkan/DescriptorSetLayoutBuilder.h/Renderer/ComputeDescriptorSet.h),
// NEVER a true uniform buffer; binding 1 is the output image2D. The output
// texture is a plain Texture2D (allowStorageImageAccess = true), NOT a
// RenderTexture/VolumeTexture - correct for THIS LUT since a transmittance
// value is always in [0, 1] per channel, but this choice does NOT
// automatically transfer to Phase 4/5's own HDR-valued LUTs (see this
// class's own .cpp for the full reasoning).
//
// Phase 4 (task_manager/atmosphere-scattering-1/
// ATMOSPHERE_PHASE4_MULTISCATTERING_LUT_v1.md) adds the SECOND LUT pass,
// AddMultiScatteringLutPass() - same overall shape (lazily-initialized
// pipeline/descriptor-set/output texture), REUSING the SAME
// m_atmosphereParametersBuffer AddTransmittanceLutPass() above already
// creates/uploads (no second, duplicate buffer for the same data), but
// its own output texture is a RenderTexture with an explicit HDR float
// format (VK_FORMAT_R16G16B16A16_SFLOAT) rather than a Texture2D - a
// multi-scattering "response" value can legitimately exceed 1.0, unlike
// the Transmittance LUT's bounded [0, 1] output (see this class's own
// .cpp for the full reasoning, and this campaign's own "Revision Notes").
//
// Phase 5 (task_manager/atmosphere-scattering-1/
// ATMOSPHERE_PHASE5_SKYVIEW_LUT_v1.md) adds the THIRD LUT pass,
// AddSkyViewLutPass() - the campaign's first genuinely PER-FRAME pass (its
// content depends on the CURRENT camera height/sun direction, via the new
// AtmosphereFrameUniforms buffer, rather than only on the session-stable
// AtmosphereParametersGpu). PER-VIEW DECISION (see this phase's own
// completion report for the full reasoning): this class computes ONE
// Sky-View LUT PER VIEW (Game View and, once Phase 7 wires it up, Scene
// View), each with its own distinct registered texture name (the
// `outputTextureName` parameter below) and its own distinct
// AtmosphereFrameUniforms buffer/descriptor set/output RenderTexture - the
// simpler, lower-risk default this phase's own strategy document
// recommended, since Game View and Scene View render through different
// cameras/aspect ratios in the same frame and must never have one view's
// camera height silently overwrite the other's mid-frame. Every per-view
// resource lives in `m_skyViewLutViewStates`, keyed by `outputTextureName`
// - lazily created the first time a given name is seen. The compute
// PIPELINE/descriptor-set-LAYOUT are shared across every view (same
// shader, same binding layout) - only the per-view descriptor SET/frame-
// uniforms buffer/output texture are ever duplicated.
//
// Phase 6 (task_manager/atmosphere-scattering-1/
// ATMOSPHERE_PHASE6_AERIAL_PERSPECTIVE_FROXEL_VOLUME_v1.md) adds the
// FOURTH LUT pass, AddAerialPerspectiveVolumePass() - the campaign's first
// pass writing a genuine 3D VolumeTexture (Phase 2's own
// RenderGraphBuilder::ImportVolumeTexture()/WriteVolumeTexture()) rather
// than a 2D texture. Same per-view state shape as the Sky-View LUT above
// (a std::unordered_map keyed by `outputVolumeName`, mirroring
// m_skyViewLutViewStates exactly - see ATMOSPHERE_PHASE5_COMPLETION_REPORT.md's
// own "Their Role" note recommending this same pattern), and the SAME
// "reuse m_atmosphereParametersBuffer, don't duplicate it" rule as every
// LUT pass above. Unlike the Sky-View LUT, this pass's own per-view output
// is a persistent VolumeTexture (created ONCE via
// Renderer::CreateVolumeTexture(), per Phase 2's own completion report
// "Their Role" answer (c) - never a graph-pooled/transient resource) that
// is re-imported into the render graph fresh every frame via
// ImportVolumeTexture(), exactly the pattern that phase's own disposable
// validation code proved out end-to-end.

#include "AtmosphereSkyBackgroundRenderer.h"
#include "AtmosphereTypes.h"
#include "../../ECS/Registry.h"
#include "../../Math/Vec3.h"
#include "../Buffer.h"
#include "../ComputeDescriptorSet.h"
#include "../ComputePipeline.h"
#include "../Renderer.h" // gte::Renderer::CapturedRawPixels - needed by CaptureAerialPerspectiveVolumeSliceImmediate() below (atmosphere-scattering-2, Phase 5). No circular include: Renderer.h never includes anything under Atmosphere/.
#include "../RenderTexture.h"
#include "../Texture2D.h"
#include "../VolumeTexture.h"
#include "../RenderGraph/RenderGraphBuilder.h"
#include "../RenderGraph/RenderGraphTypes.h"

#include <volk.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace gte {

class AtmosphereLutRenderer {
public:
    AtmosphereLutRenderer() = default;
    ~AtmosphereLutRenderer();

    AtmosphereLutRenderer(const AtmosphereLutRenderer&) = delete;
    AtmosphereLutRenderer& operator=(const AtmosphereLutRenderer&) = delete;
    AtmosphereLutRenderer(AtmosphereLutRenderer&&) = delete;
    AtmosphereLutRenderer& operator=(AtmosphereLutRenderer&&) = delete;

    // Declares this frame's Transmittance LUT compute pass into `builder`:
    // writes this object's own persistent, 256x256 output Texture2D
    // (imported fresh every call, registered under the literal name
    // "AtmosphereTransmittanceLut" - see AGENTS.md's "Named Texture
    // Capture" for why the TEXTURE name, not the pass name, is what matters
    // for GET /get_texture/GET /list_textures visibility). Lazily builds
    // this object's own ComputePipeline/descriptor-set-layout/descriptor-
    // set/parameters buffer/output texture the first time this is called
    // (needs a live Renderer/VkDevice, so can't happen in the default
    // constructor above).
    //
    // Per this phase's own "What We Will NOT Do": no dirty-flag
    // optimization - `params` is re-uploaded and the whole LUT recomputed
    // unconditionally, every single call.
    //
    // Returns the output's TextureHandle - the CALLER must add it to this
    // call's own finalOutputs/outputs root set, or this pass's write will
    // be silently culled the next time RenderGraphCompiler::Compile() runs
    // (mirrors ComputeBlurValidation::AddPass()'s own identical
    // requirement).
    rg::TextureHandle AddTransmittanceLutPass(
        rg::RenderGraphBuilder& builder, Renderer& renderer, const AtmosphereParametersGpu& params);

    // Phase 4 (ATMOSPHERE_PHASE4_MULTISCATTERING_LUT_v1.md) - declares this
    // frame's Multi-Scattering LUT compute pass into `builder`: reads
    // `transmittanceLutHandle` (this call's own AddTransmittanceLutPass()
    // result, from THIS SAME builder call - the render graph therefore
    // orders this pass strictly after it, see RenderGraphCompiler) and
    // writes this object's own persistent, 64x64 HDR output RenderTexture
    // (imported fresh every call, registered under the literal name
    // "AtmosphereMultiScatteringLut"). MUST be called AFTER
    // AddTransmittanceLutPass() in the SAME frame (it needs that call's own
    // return value as an argument, and its own lazy init reuses the
    // AtmosphereParametersGpu buffer that call already created/uploaded).
    //
    // Same "no dirty-flag optimization" contract as AddTransmittanceLutPass()
    // above, and the same "caller must add the returned handle to this
    // call's own outputs root set or the pass is silently culled" contract.
    rg::TextureHandle AddMultiScatteringLutPass(rg::RenderGraphBuilder& builder, Renderer& renderer,
        const AtmosphereParametersGpu& params, rg::TextureHandle transmittanceLutHandle);

    // Phase 5 (ATMOSPHERE_PHASE5_SKYVIEW_LUT_v1.md) - declares this frame's
    // Sky-View LUT compute pass into `builder` for ONE view: reads
    // `transmittanceLutHandle`/`multiScatteringLutHandle` (this SAME frame's
    // own AddTransmittanceLutPass()/AddMultiScatteringLutPass() results) and
    // writes a persistent, 200x100 HDR output RenderTexture registered
    // under the literal name `outputTextureName` - the parameter that lets
    // this ONE method serve both the Game View and Scene View call sites
    // with two distinct registered texture names (see this class's own
    // "PER-VIEW DECISION" comment above). `outputTextureName` MUST be a
    // string-literal/static-storage-duration pointer - it is handed
    // straight through to RenderGraphBuilder::ImportTexture(), which
    // asserts exactly that. `frameUniforms` is uploaded into a PER-VIEW
    // buffer (keyed by `outputTextureName`, see m_skyViewLutViewStates
    // below) every call, so two views computed in the same frame never
    // clobber each other's camera height/sun direction mid-frame.
    //
    // MUST be called AFTER both AddTransmittanceLutPass()/
    // AddMultiScatteringLutPass() in the SAME frame (needs their own
    // TextureHandle return values as arguments). Same "no dirty-flag
    // optimization" / "caller must add the returned handle to this call's
    // own outputs root set" contract as the two methods above.
    rg::TextureHandle AddSkyViewLutPass(rg::RenderGraphBuilder& builder, Renderer& renderer,
        const AtmosphereParametersGpu& params, const AtmosphereFrameUniforms& frameUniforms,
        rg::TextureHandle transmittanceLutHandle, rg::TextureHandle multiScatteringLutHandle,
        const char* outputTextureName);

    // Phase 6 (ATMOSPHERE_PHASE6_AERIAL_PERSPECTIVE_FROXEL_VOLUME_v1.md) -
    // declares this frame's Aerial Perspective froxel-volume compute pass
    // into `builder` for ONE view: reads
    // `transmittanceLutHandle`/`multiScatteringLutHandle` (same as
    // AddSkyViewLutPass() above) and writes a persistent, 128x128x32 HDR
    // (rgba16f) output VolumeTexture registered under the literal name
    // `outputVolumeName` - the parameter that lets this ONE method serve
    // both the Game View and Scene View call sites with two distinct
    // registered volume names, mirroring AddSkyViewLutPass()'s own
    // `outputTextureName` parameter exactly. `outputVolumeName` MUST be a
    // string-literal/static-storage-duration pointer - it is handed
    // straight through to RenderGraphBuilder::ImportVolumeTexture(), which
    // asserts exactly that. `frameUniforms` (including its Phase-6-new
    // `invViewProjection` field - see AtmosphereTypes.h) is uploaded into a
    // PER-VIEW buffer (keyed by `outputVolumeName`, see
    // m_aerialPerspectiveVolumeViewStates below) every call, so two views
    // computed in the same frame never clobber each other's camera height/
    // sun direction/view-projection mid-frame.
    //
    // MUST be called AFTER both AddTransmittanceLutPass()/
    // AddMultiScatteringLutPass() in the SAME frame (needs their own
    // TextureHandle return values as arguments). Same "no dirty-flag
    // optimization" contract as every LUT pass above - BUT a different
    // "keep this alive" contract than the texture-returning methods above:
    // a VolumeTextureHandle can NEVER be a `finalOutputs` root (see
    // RenderGraphCompiler::Compile()'s own doc comment - this was, in fact,
    // a genuine Phase 2 infrastructure gap this phase found and fixed, see
    // this class's own .cpp/ATMOSPHERE_PHASE6_COMPLETION_REPORT.md) - the
    // CALLER must instead call
    // `builder.KeepVolumeTextureOutput(returnedHandle)` explicitly, or this
    // pass's write will be silently culled the next time
    // RenderGraphCompiler::Compile() runs.
    rg::VolumeTextureHandle AddAerialPerspectiveVolumePass(rg::RenderGraphBuilder& builder, Renderer& renderer,
        const AtmosphereParametersGpu& params, const AtmosphereFrameUniforms& frameUniforms,
        rg::TextureHandle transmittanceLutHandle, rg::TextureHandle multiScatteringLutHandle,
        const char* outputVolumeName);

    // Phase 7 (task_manager/atmosphere-scattering-1/
    // ATMOSPHERE_PHASE7_SKY_BACKGROUND_AND_COMPOSITE_PASSES_v1.md) - draws
    // this VIEW's sky background directly against `cmd`, INSIDE the
    // caller's own already-open vkCmdBeginRendering bracket (mirrors
    // src/Editor/SceneGridRenderer's own integration pattern - see
    // AtmosphereSkyBackgroundRenderer.h for the full reasoning). Looks up
    // the ALREADY-COMPUTED Sky-View LUT output for `skyViewLutName` (this
    // SAME frame's own AddSkyViewLutPass() call for that exact name MUST
    // have already run - a programmer error, not a runtime-recoverable
    // one, if it hasn't) and forwards straight into
    // AtmosphereSkyBackgroundRenderer::Draw() - this class stays the
    // single home for every atmosphere GPU pass, including this one, per
    // its own class comment. `skyExposure` (Phase 8 -
    // ATMOSPHERE_PHASE8_SUN_ECS_AND_EDITOR_CONTROLS_v1.md) is the Editor's
    // "Atmosphere" panel-tunable replacement for what used to be a fixed
    // `kSkyExposure` constant in AtmosphereSkyBackground.frag.
    void DrawSkyBackground(Renderer& renderer, VkCommandBuffer cmd, const Mat4& viewProjection,
        const AtmosphereParametersGpu& params, const AtmosphereFrameUniforms& frameUniforms,
        const char* skyViewLutName, float skyExposure);

    // Phase 7 - declares this frame's Aerial Perspective Composite compute
    // pass into `builder` for ONE view: reads `sourceColorHandle` (the
    // view's own already-imported Game/Scene View TextureHandle, POST-Sky-
    // Background-pass) for BOTH its color half (ShaderRead) and, via the
    // new `isDepthResource=true` overload of PassBuilder::ReadTexture()
    // (RenderGraphBuilder.h), its DEPTH half (requires that RenderTexture's
    // own companion DepthBuffer to have been created with
    // allowSampledAccess=true - see DepthBuffer.h/RenderTexture.h) -
    // `sourceColorSampler`/`sourceDepthView`/`sourceDepthSampler` are the
    // CALLER-resolved plain Vulkan objects behind that same handle (an
    // IMPORTED texture's resolved sampler/depth view are never available
    // via PassContext::resolveTexture() - mirrors
    // ComputeBlurValidation::AddPass()'s own identical `sceneViewSampler`
    // parameter/reasoning). Also reads `aerialPerspectiveVolumeHandle`
    // (Phase 6's own output, looked up again by `aerialPerspectiveVolumeName`
    // for its own trilinear sampler - see m_aerialPerspectiveVolumeViewStates)
    // and writes a NEW, separate, persistent output RenderTexture registered
    // under the literal name `outputTextureName`
    // ("GameViewComposited"/"SceneViewComposited") - explicit
    // VK_FORMAT_R8G8B8A8_UNORM (never the swapchain's own negotiated
    // format), mirroring ComputeBlurValidation's own identical reasoning
    // for why (this texture is never bound to the same Pipeline as the
    // swapchain/Game/Scene views - only ever sampled via ImGui::Image()/
    // `/get_game_view` - so there is no reason to inherit the swapchain's
    // own uncertain storage-image-format support).
    //
    // `invViewProjection`/`cameraWorldPosition` are this view's own current
    // values, pushed as compute push constants (never a per-view uniform
    // buffer - this pass's own per-view parameters are small enough that a
    // buffer would be pure overhead, matching BoxBlur.comp's own simple
    // push-constant convention). `aerialPerspectiveStrength` (Phase 8 -
    // ATMOSPHERE_PHASE8_SUN_ECS_AND_EDITOR_CONTROLS_v1.md) is the Editor's
    // "Atmosphere" panel-tunable overall multiplier for the effect (1.0 =
    // unchanged physical result, 0.0 = fully disabled/pass-through) - see
    // AtmosphereAerialPerspectiveComposite.comp's own doc comment for the
    // exact blend formula this scales. `maxDistanceKm`/`depthExponent`
    // (atmosphere-scattering-2 campaign Phase 1) are this composite pass's
    // OWN copy of the SAME two values AddAerialPerspectiveVolumePass()'s own
    // frameUniforms fields use to GENERATE the volume this pass reads - this
    // pass's Z-slice lookup must stay the exact inverse of that generation
    // mapping, so both values are sourced from the SAME AtmosphereSettings
    // fields as the volume-generation pass (see AtmospherePassSequence.cpp).
    //
    // Returns the composited output's TextureHandle - the CALLER must add
    // it to this call's own outputs root set, or this pass's write will be
    // silently culled the next time RenderGraphCompiler::Compile() runs
    // (same contract as every AddXxxLutPass() above).
    rg::TextureHandle AddAerialPerspectiveCompositePass(rg::RenderGraphBuilder& builder, Renderer& renderer,
        rg::TextureHandle sourceColorHandle, VkSampler sourceColorSampler, VkImageView sourceDepthView,
        VkSampler sourceDepthSampler, rg::VolumeTextureHandle aerialPerspectiveVolumeHandle,
        const char* aerialPerspectiveVolumeName, const Mat4& invViewProjection, Vec3 cameraWorldPosition,
        float aerialPerspectiveStrength, float maxDistanceKm, float depthExponent, VkExtent2D extent,
        const char* outputTextureName);

    // Returns a pointer to `outputTextureName`'s own persistent composited
    // output RenderTexture (the SAME one AddAerialPerspectiveCompositePass()
    // above imports every call), or nullptr if that name has never been
    // passed to it yet this session - used by Application::Run() both to
    // hand the Editor a stable RenderTexture* to display in "Game"/"Scene"
    // (see IEditorLayer::SetGameViewCompositedTexture()/
    // SetSceneViewCompositedTexture()) and as the new source for
    // `GET /get_game_view` capture (network-impl-2 campaign).
    RenderTexture* CompositedOutput(const char* outputTextureName) noexcept;

    // Transitions `outputTextureName`'s own composited output texture from
    // the ComputeShaderWrite state AddAerialPerspectiveCompositePass() above
    // leaves it in (VK_IMAGE_LAYOUT_GENERAL) to a real ShaderRead state,
    // ready for Dear ImGui/`GET /get_game_view`/`GET /get_texture` to sample
    // it directly - mirrors RenderPasses.h's own
    // FinalizeRenderTextureForExternalSampling() for the Game/Scene views
    // themselves, applied here against ComputeShaderWrite as the "previous"
    // access instead of ColorAttachmentWrite (matches
    // ComputeBlurValidation::FinalizeForSampling()'s own identical
    // reasoning for its own compute-written output). Must be called against
    // the SAME command buffer the offscreen RenderGraph::Execute() call just
    // recorded into, AFTER that call returns and BEFORE that command buffer
    // is ended/submitted - see Application::Run(). A safe no-op if
    // `outputTextureName` has never been passed to
    // AddAerialPerspectiveCompositePass() at all this session.
    void FinalizeAerialPerspectiveCompositeForSampling(VkCommandBuffer cmd, const char* outputTextureName);

    // Phase 9 (ATMOSPHERE_PHASE9_VALIDATION_DEBUG_TOOLING_AND_DOCS_v1.md,
    // Step 3.1) - exposes the persistent Texture2D behind
    // AddTransmittanceLutPass()'s own output, so
    // src/Editor/AtmosphereTransmittanceLutValidation.cpp can read back its
    // REAL, currently-computed pixels (via Renderer::CaptureImagePixels())
    // without this class needing any dependency on that validation tool.
    // Returns nullptr if AddTransmittanceLutPass() has never run yet this
    // session.
    Texture2D* TransmittanceLutOutput() noexcept
    {
        return m_transmittanceLutOutput.has_value() ? &(*m_transmittanceLutOutput) : nullptr;
    }

    // atmosphere-scattering-2 campaign, Phase 5
    // (task_manager/atmosphere-scattering-2/PHASE5_AERIAL_LUT_NUMERIC_VALIDATION_TOOL.md)
    // - the real Z-slice count for `aerialPerspectiveVolumeName`, so
    // src/Editor/AtmosphereAerialPerspectiveLutInspection.cpp's
    // InspectAerialPerspectiveVolume() can loop over every one of the
    // volume's real depth slices without hardcoding this campaign's own
    // fixed 128x128x32 resolution constant (which lives only as a private
    // implementation detail of this class's own .cpp). Returns 0 if
    // `aerialPerspectiveVolumeName` has never been generated this session
    // (mirrors this class's other graceful-failure contracts - a caller
    // should treat 0 as "nothing to inspect yet", never divide/index by it
    // unconditionally).
    int AerialPerspectiveVolumeDepth(const char* aerialPerspectiveVolumeName) const noexcept;

    // Phase 9, Step 3.2 - the volume-texture debug-visibility gap Phase 2
    // deliberately deferred (RenderGraphDebugTextureRegistry has no 3D
    // concept - see AGENTS.md's "Named Texture Capture"). Declares a tiny
    // compute pass copying ONE Z slice of `aerialPerspectiveVolumeHandle`
    // (this SAME frame's own AddAerialPerspectiveVolumePass() result,
    // looked up again by `aerialPerspectiveVolumeName` for its own
    // trilinear sampler/dimensions - an imported VolumeTextureHandle's
    // resolved sampler is always VK_NULL_HANDLE, mirrors
    // AddAerialPerspectiveCompositePass()'s own identical
    // `aerialVolumeSampler` lookup) into a REAL, registered 2D
    // RenderTexture (rgba16f, matching the volume's own HDR format exactly
    // - never clipped to [0, 1]) under the literal name
    // `outputTextureName` - so it becomes automatically
    // GET /get_texture/GET /list_textures-capturable with zero further
    // networking changes (GET /get_texture's own existing isHdrColor check
    // already handles VK_FORMAT_R16G16B16A16_SFLOAT, unchanged by this
    // phase). `debugSliceIndex` is clamped to [0, volume depth - 1]
    // internally - never out of bounds even if the Editor's own slider
    // briefly disagrees with the volume's real depth.
    //
    // MUST be called AFTER AddAerialPerspectiveVolumePass() for
    // `aerialPerspectiveVolumeName` in the SAME frame. Unlike
    // AddAerialPerspectiveVolumePass() itself, this pass's own OWN output
    // is a plain 2D TextureHandle, so the CALLER just adds it to this
    // call's own ordinary `outputs` root set (finalOutputs) - never
    // `KeepVolumeTextureOutput()`, which only applies to a VolumeTexture
    // WRITE, not a Texture WRITE that merely READS a volume texture.
    rg::TextureHandle AddAerialPerspectiveVolumeDebugSlicePass(rg::RenderGraphBuilder& builder, Renderer& renderer,
        rg::VolumeTextureHandle aerialPerspectiveVolumeHandle, const char* aerialPerspectiveVolumeName,
        std::uint32_t debugSliceIndex, const char* outputTextureName);

    // atmosphere-scattering-2 campaign, Phase 5
    // (task_manager/atmosphere-scattering-2/PHASE5_AERIAL_LUT_NUMERIC_VALIDATION_TOOL.md,
    // Step 3.3a) - the ad-hoc, IMMEDIATE-dispatch counterpart of
    // AddAerialPerspectiveVolumeDebugSlicePass() above, for the "copy this
    // one Z slice and read it back RIGHT NOW, outside any per-frame
    // RenderGraph pass" case src/Editor/AtmosphereAerialPerspectiveLutInspection.cpp's
    // own InspectAerialPerspectiveVolume() needs (looping over every one of
    // the volume's Z slices from an Editor button click). Reuses the SAME
    // lazily-initialized pipeline/descriptor-set-layout
    // (EnsureAerialPerspectiveVolumeDebugSliceInitialized()) and per-name
    // output RenderTexture (EnsureAerialPerspectiveVolumeDebugSliceViewInitialized())
    // AddAerialPerspectiveVolumeDebugSlicePass() itself already builds - only
    // the RECORDING differs: a renderer.ImmediateSubmit() callback issuing
    // raw vkCmdBindPipeline/vkCmdBindDescriptorSets/vkCmdPushConstants/
    // vkCmdDispatch calls directly, mirroring
    // VolumeTexturePreviewRenderer::RenderPreview()'s own already-shipped
    // "immediate, non-per-frame dispatch" pattern EXACTLY - NEVER
    // renderer.Dispatch() (gated to render-graph-pass recording only) and
    // NEVER a throwaway rg::RenderGraph + RenderGraph::Execute() call (see
    // this phase's own strategy document, "Which approach, and why", for the
    // verified reasoning both of those would be unsafe/infeasible here).
    //
    // `outputTextureName` MUST be a DIFFERENT literal than
    // "AtmosphereAerialPerspectiveVolumeDebugSlice" (e.g.
    // "AtmosphereAerialPerspectiveVolumeInspectionSlice") - it gets its own,
    // separate entry in m_aerialPerspectiveVolumeDebugSliceViewStates, so
    // this method's own 32-slice sweep never clobbers the separately-live,
    // per-frame, Editor-slider-driven, GET /get_texture-capturable debug-
    // slice texture a user/AI agent may currently be relying on.
    //
    // Returns the RAW captured bytes (Renderer::CapturedRawPixels::pixels) -
    // exactly width*height*8 bytes, tightly packed row-major, 4 IEEE-754
    // HALF floats (2 bytes/channel) per texel, matching
    // VK_FORMAT_R16G16B16A16_SFLOAT's own real memory layout - this is NOT 4
    // full 32-bit floats/texel. The caller
    // (AtmosphereAerialPerspectiveLutInspection.cpp's
    // InspectAerialPerspectiveVolume()) must decode each channel via
    // Encoding::DecodeHalfFloat() - never reinterpret_cast this buffer as
    // `const float*` directly. Returns an empty, default-constructed
    // CapturedRawPixels (width == 0, pixels empty) if
    // `aerialPerspectiveVolumeName` has never been generated this session
    // (mirrors AddAerialPerspectiveVolumeDebugSlicePass()'s own graceful-
    // failure contract - a programmer/caller-ordering error, never a crash).
    Renderer::CapturedRawPixels CaptureAerialPerspectiveVolumeSliceImmediate(Renderer& renderer,
        const char* aerialPerspectiveVolumeName, std::uint32_t sliceIndex, const char* outputTextureName);

private:
    // Per-VIEW state for the Sky-View LUT (Phase 5) - one instance per
    // distinct `outputTextureName` ever passed to AddSkyViewLutPass(),
    // stored in m_skyViewLutViewStates below. The descriptor SET/frame-
    // uniforms buffer/output texture must each be genuinely distinct per
    // view (see this class's own "PER-VIEW DECISION" header comment) -
    // only the pipeline/descriptor-set-LAYOUT (shared members, below this
    // struct) are common to every view.
    struct SkyViewLutViewState {
        ComputeDescriptorSet descriptorSet;
        std::optional<Buffer> frameUniformsBuffer;
        std::optional<RenderTexture> output;
    };

    // Phase 6 - Aerial Perspective Volume. One instance per distinct
    // `outputVolumeName` ever passed to AddAerialPerspectiveVolumePass(),
    // stored in m_aerialPerspectiveVolumeViewStates below - mirrors
    // SkyViewLutViewState's own shape exactly, just with a VolumeTexture
    // output instead of a RenderTexture.
    struct AerialPerspectiveVolumeViewState {
        ComputeDescriptorSet descriptorSet;
        std::optional<Buffer> frameUniformsBuffer;
        std::optional<VolumeTexture> output;
    };

    // Phase 7 - Aerial Perspective Composite. One instance per distinct
    // `outputTextureName` ever passed to AddAerialPerspectiveCompositePass()
    // ("GameViewComposited"/"SceneViewComposited") - mirrors
    // SkyViewLutViewState's own shape, minus the per-view uniforms buffer
    // (this pass's own per-view parameters are pushed as push constants
    // instead - see AddAerialPerspectiveCompositePass()'s own doc comment).
    struct AerialPerspectiveCompositeViewState {
        ComputeDescriptorSet descriptorSet;
        std::optional<RenderTexture> output;
    };

    // Phase 9 - Aerial Perspective Volume Debug Slice. One instance per
    // distinct `outputTextureName` ever passed to
    // AddAerialPerspectiveVolumeDebugSlicePass() (today just
    // "AtmosphereAerialPerspectiveVolumeDebugSlice" - the Game View's own
    // volume only, see that method's own doc comment) - mirrors
    // AerialPerspectiveCompositeViewState's own shape exactly (no per-view
    // uniforms buffer of its own; this pass's own per-call parameters are
    // pushed as compute push constants instead).
    struct AerialPerspectiveVolumeDebugSliceViewState {
        ComputeDescriptorSet descriptorSet;
        std::optional<RenderTexture> output;
    };

    void EnsureTransmittanceLutInitialized(Renderer& renderer, const AtmosphereParametersGpu& params);
    void EnsureMultiScatteringLutInitialized(Renderer& renderer);
    void EnsureSkyViewLutInitialized(Renderer& renderer);
    SkyViewLutViewState& EnsureSkyViewLutViewInitialized(Renderer& renderer, const char* outputTextureName);
    void EnsureAerialPerspectiveVolumeInitialized(Renderer& renderer);
    AerialPerspectiveVolumeViewState& EnsureAerialPerspectiveVolumeViewInitialized(
        Renderer& renderer, const char* outputVolumeName);
    void EnsureAerialPerspectiveCompositeInitialized(Renderer& renderer);
    AerialPerspectiveCompositeViewState& EnsureAerialPerspectiveCompositeViewInitialized(
        Renderer& renderer, const char* outputTextureName, VkExtent2D extent);
    void EnsureAerialPerspectiveVolumeDebugSliceInitialized(Renderer& renderer);
    AerialPerspectiveVolumeDebugSliceViewState& EnsureAerialPerspectiveVolumeDebugSliceViewInitialized(
        Renderer& renderer, const char* outputTextureName, int width, int height);


    VkDevice m_device = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_transmittanceLutDescriptorSetLayout = VK_NULL_HANDLE;
    std::optional<ComputePipeline> m_transmittanceLutPipeline;
    ComputeDescriptorSet m_transmittanceLutDescriptorSet;
    std::optional<Buffer> m_atmosphereParametersBuffer;
    std::optional<Texture2D> m_transmittanceLutOutput;

    // Phase 4 - Multi-Scattering LUT. Deliberately NO second
    // m_atmosphereParametersBuffer here - reuses m_atmosphereParametersBuffer
    // above (see this class's own .cpp).
    VkDescriptorSetLayout m_multiScatteringLutDescriptorSetLayout = VK_NULL_HANDLE;
    std::optional<ComputePipeline> m_multiScatteringLutPipeline;
    ComputeDescriptorSet m_multiScatteringLutDescriptorSet;
    std::optional<RenderTexture> m_multiScatteringLutOutput;

    // Phase 5 - Sky-View LUT. Deliberately NO second
    // m_atmosphereParametersBuffer here either - reuses the SAME buffer as
    // above. Pipeline/descriptor-set-LAYOUT are shared across every view;
    // m_skyViewLutViewStates holds the genuinely per-view state (see
    // SkyViewLutViewState's own doc comment above).
    VkDescriptorSetLayout m_skyViewLutDescriptorSetLayout = VK_NULL_HANDLE;
    std::optional<ComputePipeline> m_skyViewLutPipeline;
    std::unordered_map<std::string, SkyViewLutViewState> m_skyViewLutViewStates;

    // Phase 6 - Aerial Perspective Volume. Deliberately NO second
    // m_atmosphereParametersBuffer here either - reuses the SAME buffer as
    // above. Pipeline/descriptor-set-LAYOUT are shared across every view;
    // m_aerialPerspectiveVolumeViewStates holds the genuinely per-view
    // state (see AerialPerspectiveVolumeViewState's own doc comment above).
    VkDescriptorSetLayout m_aerialPerspectiveVolumeDescriptorSetLayout = VK_NULL_HANDLE;
    std::optional<ComputePipeline> m_aerialPerspectiveVolumePipeline;
    std::unordered_map<std::string, AerialPerspectiveVolumeViewState> m_aerialPerspectiveVolumeViewStates;

    // Phase 7 - Aerial Perspective Composite. Deliberately NO
    // AtmosphereParametersGpu/frame-uniforms buffer at all - this pass's
    // own per-view parameters are pushed as compute push constants instead
    // (see AddAerialPerspectiveCompositePass()'s own doc comment). Pipeline/
    // descriptor-set-LAYOUT are shared across every view;
    // m_aerialPerspectiveCompositeViewStates holds the genuinely per-view
    // state (see AerialPerspectiveCompositeViewState's own doc comment
    // above).
    VkDescriptorSetLayout m_aerialPerspectiveCompositeDescriptorSetLayout = VK_NULL_HANDLE;
    std::optional<ComputePipeline> m_aerialPerspectiveCompositePipeline;
    std::unordered_map<std::string, AerialPerspectiveCompositeViewState> m_aerialPerspectiveCompositeViewStates;

    // Phase 9 - Aerial Perspective Volume Debug Slice. Deliberately NO
    // AtmosphereParametersGpu/frame-uniforms buffer at all, same reasoning
    // as Aerial Perspective Composite above (this pass's own per-call
    // parameters are pushed as compute push constants instead). Pipeline/
    // descriptor-set-LAYOUT are shared across every view;
    // m_aerialPerspectiveVolumeDebugSliceViewStates holds the genuinely
    // per-view state (see AerialPerspectiveVolumeDebugSliceViewState's own
    // doc comment above).
    VkDescriptorSetLayout m_aerialPerspectiveVolumeDebugSliceDescriptorSetLayout = VK_NULL_HANDLE;
    std::optional<ComputePipeline> m_aerialPerspectiveVolumeDebugSlicePipeline;
    std::unordered_map<std::string, AerialPerspectiveVolumeDebugSliceViewState>
        m_aerialPerspectiveVolumeDebugSliceViewStates;

    // Phase 7 - the Sky Background pass's own dedicated graphics-pipeline
    // owner (see AtmosphereSkyBackgroundRenderer.h) - a genuinely different
    // kind of object than every compute pipeline above, owned here (rather
    // than a sibling class of its own) since AtmosphereLutRenderer remains
    // this campaign's single home for atmosphere GPU pass orchestration
    // (see this class's own header comment) - only the low-level
    // VkPipeline-building logic itself lives in a separate file.
    AtmosphereSkyBackgroundRenderer m_skyBackgroundRenderer;
};

// Phase 5 (ATMOSPHERE_PHASE5_SKYVIEW_LUT_v1.md, Step 3) - resolves this
// frame's AtmosphereFrameUniforms for ONE view, given that view's own eye
// world-space position (Game View: the active ECS Camera's resolved world
// position; Scene View: the Editor's own EditorCamera position - see
// IEditorLayer::SceneViewCameraWorldPosition()). `registry` is used to
// resolve the real, first-active ECS DirectionalLight entity (Phase 8 -
// ATMOSPHERE_PHASE8_SUN_ECS_AND_EDITOR_CONTROLS_v1.md) via
// Renderer/Atmosphere/DirectionalLightResolver.h's
// ResolveActiveDirectionalLight() - falling back to that same helper's own
// hardcoded placeholder sun whenever the Registry has no active
// DirectionalLight at all (a scene with no Sun entity still renders a
// plausible-looking sky). Keep this function's SIGNATURE stable regardless
// of future changes to how the sun is resolved - every call site
// (AtmospherePassSequence.cpp's Game View/Scene View wiring, the Aerial
// Perspective volume) depends on it never changing shape.
AtmosphereFrameUniforms ResolveAtmosphereFrameUniforms(Registry& registry, Vec3 eyeWorldPosition);


} // namespace gte
