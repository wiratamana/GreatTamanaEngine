#pragma once

#include "IPluginCapabilityOrchestrator.h"
#include "RenderFeatureNamePool.h"

#include "../../Renderer/ComputeDescriptorSet.h"
#include "../../Renderer/ComputePipeline.h"
#include "../../Renderer/RenderTexture.h"
#include "../../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../../Renderer/RenderGraph/RenderGraphTypes.h"

#include "../../../plugins/gte_plugin_abi/IRenderFeatureModule.h"

#include <volk.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// editor-core-separation-6 campaign, PHASE4
// (PHASE4_RENDER_FEATURE_COMPOSITOR_CORE_AND_ORDERING.md) - the second real
// IPluginCapabilityOrchestrator implementation: the PERMANENT home for the
// `_v2` render-feature pipeline (PHASE0_MASTER_STRATEGY.md's whole reason to
// exist). Every loaded IRenderFeatureModule_v2 gets its OWN private
// offscreen render target, composited in explicit, author-declared
// stage+priority order, through a real, host-owned GPU blend pipeline -
// never "last write wins on shared memory" the way the legacy `_v1` path
// (LegacyRenderFeatureOrchestrator) still works, forever, by design (Locked
// Design Decision #9 - `_v1` and `_v2` are never unified).
//
// THIS PHASE's own scope: the blend between two plugins/stages is
// TEMPORARILY hardcoded to a single, simple, Replace-equivalent behavior
// (see EnsureBlendStubInitialized()/DispatchBlendStub() below) - proving the
// real ordering/private-target/collision-detection/seeding mechanism works
// end-to-end BEFORE PHASE5 replaces just the blend shader's own body with
// the real, multi-mode RenderFeatureBlend.comp (same descriptor-set-layout
// shape, by design - see PHASE4_RENDER_FEATURE_COMPOSITOR_CORE_AND_ORDERING.md's
// own Step 3.6).
namespace gte {

class Core; // forward declaration only - this header must not #include "../Core.h"
            // (that would be a circular include: Core.h itself will gain a member
            // of type std::unique_ptr<IPluginCapabilityOrchestrator>, and
            // RenderFeatureCompositor.h is reachable from Core.cpp's own
            // #include list). The .cpp file #includes "../Core.h" for the real
            // Core& method calls (Core::FindPluginRenderFeatureTarget()).
class Renderer; // forward declaration only - held as a plain reference member;
                 // the .cpp file #includes "../Core.h", which transitively pulls
                 // in the real Renderer.h for every real method call.

// The C++-side push-constant struct mirroring RenderFeatureOps.comp's own
// `PushConstants` GLSL block byte-for-byte (4 vec4s, 64 bytes total, no
// padding) - mirrors AerialPerspectiveCompositePushConstants's own plain-
// float[]-members shape exactly (AtmosphereLutRenderer.h). Used by both
// RenderFeatureCompositor::DispatchOps() and PluginRenderPassBuilderAdapter_v2.
struct RenderFeatureOpsPushConstants {
    float opCodeAndPad[4] = {};       // .x = opCode (0=SolidFill, 1=RadialVignette, 2=ColorGrade)
    float colorRgba[4] = {};          // solid fill color / vignette color / tint color
    float centerAndRadius[4] = {};    // vignette: centerX, centerY, innerRadius, outerRadius
    float gradeParams[4] = {};        // color grade: brightness, contrast, saturation, tintStrength
};

class RenderFeatureCompositor final : public IPluginCapabilityOrchestrator {
public:
    explicit RenderFeatureCompositor(Core& core, Renderer& renderer);

    void OnPluginsLoaded(const std::vector<IPluginModule*>& modules) override;
    void ContributeRenderGraphPasses(
        const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out) override;

    // Called by PluginRenderPassBuilderAdapter_v2 - dispatches one of the 3
    // fixed drawing operations (RenderFeatureOps.comp) against the private
    // target already imported/created for `stateKey` this frame (a
    // programmer error, not a runtime-recoverable one, if `stateKey` was
    // never seen by ContributeRenderGraphPasses() first this frame - see
    // .cpp).
    void DispatchOps(rg::RenderGraphBuilder& builder, rg::TextureHandle privateTarget, const char* stateKey,
        const char* debugName, const RenderFeatureOpsPushConstants& pushConstants);

private:
    struct Entry {
        IRenderFeatureModule_v2* module = nullptr;
        GtePluginRenderFeatureDescriptor descriptor{};
    };

    // Bundles exactly what one (plugin, view) private-target slot needs -
    // mirrors AtmosphereLutRenderer's own AerialPerspectiveCompositeViewState
    // shape (a persistent output RenderTexture PLUS its own dedicated
    // ComputeDescriptorSet, never shared with any other slot - a Vulkan
    // descriptor set must never be Rewrite()-ed and dispatched against more
    // than once per frame for two DIFFERENT physical resources).
    struct PrivateTargetState {
        ComputeDescriptorSet opsDescriptorSet; // allocated once, at state-creation time.
        std::optional<RenderTexture> texture;  // created lazily, first use; resized in place on extent change.
    };

    // Bundles one blend-stage physical slot: either a per-(plugin, view)
    // ACCUMULATOR (the output of plugin i's blend, i < N-1, that becomes
    // plugin i+1's own "currentInput"), or the per-VIEW SEED. Same
    // "persistent RenderTexture + its own dedicated ComputeDescriptorSet"
    // shape as PrivateTargetState, for the identical reason. The LAST entry
    // in a view's combined list never gets a texture here at all (its own
    // blend writes directly into the view's real composited handle instead
    // - see EnsureBlendStageDescriptorOnly()) - only a dedicated descriptor
    // set, since a blend dispatch always needs one regardless of where its
    // output physically lands.
    struct BlendStageState {
        ComputeDescriptorSet blendDescriptorSet;
        std::optional<RenderTexture> texture;
    };

    void EnsureTextureSized(std::optional<RenderTexture>& texture, const char* internedName, VkExtent2D extent);

    PrivateTargetState& EnsurePrivateTargetState(const char* internedName, VkExtent2D extent);
    BlendStageState& EnsureBlendStageDescriptorOnly(const char* internedName);
    BlendStageState& EnsureBlendStageState(const char* internedName, VkExtent2D extent);

    void EnsureOpsInitialized(Renderer& renderer);
    void EnsureBlendStubInitialized(Renderer& renderer); // PHASE5 replaces this with EnsureBlendPipelineInitialized().

    // THIS PHASE'S temporary, simplified (Replace-equivalent) blend/seed
    // dispatch - see RenderFeatureBlendStub.comp's own doc comment. `state`
    // supplies the dedicated descriptor set this dispatch rewrites/binds;
    // its own `.texture` field is irrelevant here (the destination handle is
    // passed explicitly, since the LAST entry in a view's combined list
    // writes into a handle `state` itself never owns).
    void DispatchBlendStub(rg::RenderGraphBuilder& builder, rg::TextureHandle dstIn, VkSampler dstInSampler,
        rg::TextureHandle srcIn, VkSampler srcInSampler, rg::TextureHandle destination, BlendStageState& state,
        const char* debugName, VkExtent2D extent);

    Core& m_core;
    Renderer& m_renderer;
    VkDevice m_device = VK_NULL_HANDLE;

    std::vector<Entry> m_postComposite; // sorted by priority ascending
    std::vector<Entry> m_preUi;         // sorted by priority ascending
    RenderFeatureNamePool m_namePool;

    std::optional<ComputePipeline> m_opsPipeline;
    VkDescriptorSetLayout m_opsDescriptorSetLayout = VK_NULL_HANDLE;
    std::optional<ComputePipeline> m_blendStubPipeline; // PHASE5 renames/replaces this.
    VkDescriptorSetLayout m_blendStubDescriptorSetLayout = VK_NULL_HANDLE;

    // Keyed by RenderFeatureNamePool's own interned "<Plugin>_<View>_Private" names.
    std::unordered_map<std::string, PrivateTargetState> m_privateTargetStates;
    // Keyed by RenderFeatureNamePool's own interned "<Plugin>_<View>_Accum" names
    // AND "RenderFeatureCompositor_<View>_Seed" names (same map, both roles -
    // they never collide, since interned names always carry either "_Accum"
    // or "_Seed" as an unambiguous suffix).
    std::unordered_map<std::string, BlendStageState> m_blendStageStates;
};

} // namespace gte
