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

#include "AtmosphereTypes.h"
#include "../../ECS/Registry.h"
#include "../../Math/Vec3.h"
#include "../Buffer.h"
#include "../ComputeDescriptorSet.h"
#include "../ComputePipeline.h"
#include "../RenderTexture.h"
#include "../Texture2D.h"
#include "../RenderGraph/RenderGraphBuilder.h"
#include "../RenderGraph/RenderGraphTypes.h"

#include <volk.h>

#include <optional>
#include <string>
#include <unordered_map>

namespace gte {

class Renderer;

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

    void EnsureTransmittanceLutInitialized(Renderer& renderer, const AtmosphereParametersGpu& params);
    void EnsureMultiScatteringLutInitialized(Renderer& renderer);
    void EnsureSkyViewLutInitialized(Renderer& renderer);
    SkyViewLutViewState& EnsureSkyViewLutViewInitialized(Renderer& renderer, const char* outputTextureName);

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
};

// Phase 5 (ATMOSPHERE_PHASE5_SKYVIEW_LUT_v1.md, Step 3) - resolves this
// frame's AtmosphereFrameUniforms for ONE view, given that view's own eye
// world-space position (Game View: the active ECS Camera's resolved world
// position - see Application.cpp's temporary validation call site; Scene
// View, once Phase 7 wires it up: the Editor's own EditorCamera position).
// `registry` is accepted now purely so a future phase (8) can look up a
// real DirectionalLight entity from it - THIS phase's own sun-direction
// branch is a hardcoded placeholder (see this function's own .cpp
// definition, clearly marked `// TODO(ATMOSPHERE_PHASE8)`) and does not
// read `registry` at all yet. Keep this function's SIGNATURE stable even
// once Phase 8 replaces that one branch's body - every call site (this
// phase's own Game View wiring, a future Phase 6 aerial-perspective
// volume, a future Phase 7 Scene View wiring) depends on it never changing
// shape.
AtmosphereFrameUniforms ResolveAtmosphereFrameUniforms(Registry& registry, Vec3 eyeWorldPosition);


} // namespace gte
