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

#include "AtmosphereTypes.h"
#include "../Buffer.h"
#include "../ComputeDescriptorSet.h"
#include "../ComputePipeline.h"
#include "../Texture2D.h"
#include "../RenderGraph/RenderGraphBuilder.h"
#include "../RenderGraph/RenderGraphTypes.h"

#include <volk.h>

#include <optional>

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

private:
    void EnsureTransmittanceLutInitialized(Renderer& renderer, const AtmosphereParametersGpu& params);

    VkDevice m_device = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_transmittanceLutDescriptorSetLayout = VK_NULL_HANDLE;
    std::optional<ComputePipeline> m_transmittanceLutPipeline;
    ComputeDescriptorSet m_transmittanceLutDescriptorSet;
    std::optional<Buffer> m_atmosphereParametersBuffer;
    std::optional<Texture2D> m_transmittanceLutOutput;
};

} // namespace gte
