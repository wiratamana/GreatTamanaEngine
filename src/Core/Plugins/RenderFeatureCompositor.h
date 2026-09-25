#pragma once

#include "IPluginCapabilityOrchestrator.h"
#include "RenderFeatureDebugEntry.h"
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

// editor-core-separation-6 campaign, PHASE4/PHASE5
// (PHASE4_RENDER_FEATURE_COMPOSITOR_CORE_AND_ORDERING.md,
// PHASE5_BLEND_MODE_COMPUTE_SHADER_AND_PREUI_STAGE.md) - the second real
// IPluginCapabilityOrchestrator implementation: the PERMANENT home for the
// `_v2` render-feature pipeline (PHASE0_MASTER_STRATEGY.md's whole reason to
// exist). Every loaded IRenderFeatureModule_v2 gets its OWN private
// offscreen render target, composited in explicit, author-declared
// stage+priority order, through a real, host-owned GPU blend pipeline -
// never "last write wins on shared memory" the way the legacy `_v1` path
// (LegacyRenderFeatureOrchestrator) still works, forever, by design (Locked
// Design Decision #9 - `_v1` and `_v2` are never unified).
//
// PHASE5 replaced PHASE4's own temporary, Replace-equivalent blend stub
// (`RenderFeatureBlendStub.comp`/`m_blendStubPipeline`) with the real,
// permanent, 5-mode `RenderFeatureBlend.comp` uber blend shader (Replace /
// AlphaOver / Additive / Multiply / ScreenSpaceMask), selected per-plugin via
// `GtePluginRenderFeatureDescriptor::blendMode` at dispatch time through a
// push constant - the ordering/private-target/collision-detection/seeding
// mechanism PHASE4 built is completely unchanged; only WHAT the blend pass
// computes changed.
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

// editor-core-separation-6 campaign, PHASE5 - the C++-side push-constant
// struct mirroring RenderFeatureBlend.comp's own `PushConstants` GLSL block
// byte-for-byte (1 vec4, 16 bytes). Used by RenderFeatureCompositor::
// DispatchBlend() - `.x` carries the RenderFeatureBlendMode, cast to float
// (matching the shader's own `int(pc.blendModeAndPad.x)` cast).
struct RenderFeatureBlendPushConstants {
    float blendModeAndPad[4] = {}; // .x = RenderFeatureBlendMode, as a float cast to int in-shader
};

class RenderFeatureCompositor final : public IPluginCapabilityOrchestrator {
public:
    explicit RenderFeatureCompositor(Core& core, Renderer& renderer);

    void OnPluginsLoaded(const std::vector<IPluginModule*>& modules) override;
    void ContributeRenderGraphPasses(
        const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out) override;

    // editor-core-separation-6 campaign, PHASE7
    // (PHASE7_RENDER_GRAPH_PANEL_VISIBILITY.md) - a read-only, `_v2`-ABI-free
    // snapshot of this compositor's own REAL, resolved ordering decision:
    // walks m_postComposite then m_preUi (the exact same combined order
    // ContributeRenderGraphPasses() itself uses), converting each Entry's
    // descriptor fields into small, human-readable strings (see the
    // RenderFeatureCompositor.cpp-local ToString(RenderFeatureStage)/
    // ToString(RenderFeatureBlendMode) helpers, mirroring RenderPassEvent's
    // own existing ToString() free-function precedent, RenderGraphTypes.h/
    // .cpp). Consumed by the Editor's "Render Graph" panel
    // (RenderGraphPanel::Build()) via Core::GetRenderFeatureCompositor()-
    // >DebugSnapshot(). Safe to call at most once per Editor frame - never
    // on a hot render path, and never mutates any of this class's own state.
    std::vector<RenderFeatureDebugEntry> DebugSnapshot() const;

    // Called by PluginRenderPassBuilderAdapter_v2 - dispatches one of the 3
    // fixed drawing operations (RenderFeatureOps.comp) against the private
    // target already imported/created for `stateKey` this frame (a
    // programmer error, not a runtime-recoverable one, if `stateKey` was
    // never seen by ContributeRenderGraphPasses() first this frame - see
    // .cpp).
    void DispatchOps(rg::RenderGraphBuilder& builder, rg::TextureHandle privateTarget, const char* stateKey,
        const char* debugName, const RenderFeatureOpsPushConstants& pushConstants);

    // editor-core-separation-8 campaign, PHASE2
    // (PHASE2_PLUGIN_RENDER_FEATURE_ENABLE_DISABLE_AND_PRIORITY.md) - host-side
    // enable/disable override. Finds `name` in EITHER m_postComposite OR
    // m_preUi (a plugin's own descriptor.name is unique by construction - two
    // plugins sharing a name is not a case this engine defends against
    // anywhere else either) and sets its enabledOverride. Returns false
    // (no-op) if `name` matches no loaded _v2 plugin - defense-in-depth; a
    // caller (the panel, PHASE4, or the HTTP bridge, PHASE5) is expected to
    // only ever pass a name it already saw via DebugSnapshot()/
    // GET /render_graph's own render_features[] array.
    bool SetFeatureEnabled(const std::string& name, bool enabled);

    // editor-core-separation-8 campaign, PHASE2 - host-side LIVE priority
    // override (PHASE0_MASTER_STRATEGY.md's Locked Product Decision #2 -
    // safe, because ContributeRenderGraphPasses() rebuilds its entire
    // compositing chain fresh from m_postComposite/m_preUi every single
    // frame, and every interned target name is keyed by plugin name + view
    // name, never by list position). Mutates the SAME host-side
    // Entry::descriptor.priority copy already made once at OnPluginsLoaded()
    // time (never writes back into the plugin's own memory), then
    // immediately re-sorts + re-runs the SAME collision-detection/tie-break
    // logic OnPluginsLoaded() already uses, for ONLY the one stage `name`
    // belongs to. Returns false if `name` matches no loaded _v2 plugin.
    bool SetFeaturePriority(const std::string& name, std::int32_t priority);

private:
    struct Entry {
        IRenderFeatureModule_v2* module = nullptr;
        GtePluginRenderFeatureDescriptor descriptor{};
        // editor-core-separation-8 campaign, PHASE2 - host-side-only
        // override, NEVER part of the plugin's own descriptor/ABI (the
        // plugin ABI stays byte-for-byte unchanged - see
        // PHASE0_MASTER_STRATEGY.md's Step 2.2/Locked Product Decision,
        // mirroring GtePluginRenderFeatureDescriptor's own "the HOST NEVER
        // trusts a plugin to self-order at runtime" rule, extended here to
        // "and the host may now ALSO fully hide a plugin from the
        // compositing chain, without the plugin itself ever knowing").
        // Defaults true - every existing loaded plugin behaves EXACTLY as
        // before this phase until something explicitly calls
        // SetFeatureEnabled(false).
        bool enabledOverride = true;
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

    // editor-core-separation-8 campaign, PHASE2 - extracted VERBATIM from
    // OnPluginsLoaded()'s own former inline `sortAndDetectCollisions` lambda,
    // so SetFeaturePriority() can reuse the EXACT same sort+collision-tie-
    // break behavior for a single re-sort after a live priority change,
    // without duplicating the logic.
    static void SortAndDetectCollisionsInStage(std::vector<Entry>& entries, const char* stageName);

    // editor-core-separation-8 campaign, PHASE2 - shared lookup used by both
    // SetFeatureEnabled() and SetFeaturePriority(): searches m_postComposite
    // then m_preUi for an Entry whose descriptor.name matches `name` exactly.
    // Returns nullptr if not found. Non-const overload only (both callers
    // mutate through it).
    Entry* FindEntryByName(const std::string& name);

    PrivateTargetState& EnsurePrivateTargetState(const char* internedName, VkExtent2D extent);
    BlendStageState& EnsureBlendStageDescriptorOnly(const char* internedName);
    BlendStageState& EnsureBlendStageState(const char* internedName, VkExtent2D extent);

    void EnsureOpsInitialized(Renderer& renderer);
    void EnsureBlendPipelineInitialized(Renderer& renderer);

    // The real, permanent blend/seed dispatch (RenderFeatureBlend.comp,
    // PHASE5) - `state` supplies the dedicated descriptor set this dispatch
    // rewrites/binds; its own `.texture` field is irrelevant here (the
    // destination handle is passed explicitly, since the LAST entry in a
    // view's combined list writes into a handle `state` itself never owns).
    // `blendMode` selects the blend formula via the push constant -
    // RenderFeatureCompositor's own per-view "seed" dispatch always passes
    // RenderFeatureBlendMode::Replace explicitly, regardless of any
    // individual plugin's own declared blend mode; every per-plugin dispatch
    // passes that plugin's own `descriptor.blendMode`.
    void DispatchBlend(rg::RenderGraphBuilder& builder, rg::TextureHandle dstIn, VkSampler dstInSampler,
        rg::TextureHandle srcIn, VkSampler srcInSampler, rg::TextureHandle destination, BlendStageState& state,
        const char* debugName, VkExtent2D extent, RenderFeatureBlendMode blendMode);

    Core& m_core;
    Renderer& m_renderer;
    VkDevice m_device = VK_NULL_HANDLE;

    std::vector<Entry> m_postComposite; // sorted by priority ascending
    std::vector<Entry> m_preUi;         // sorted by priority ascending
    RenderFeatureNamePool m_namePool;

    std::optional<ComputePipeline> m_opsPipeline;
    VkDescriptorSetLayout m_opsDescriptorSetLayout = VK_NULL_HANDLE;
    std::optional<ComputePipeline> m_blendPipeline; // RenderFeatureBlend.comp (PHASE5).
    VkDescriptorSetLayout m_blendDescriptorSetLayout = VK_NULL_HANDLE;

    // Keyed by RenderFeatureNamePool's own interned "<Plugin>_<View>_Private" names.
    std::unordered_map<std::string, PrivateTargetState> m_privateTargetStates;
    // Keyed by RenderFeatureNamePool's own interned "<Plugin>_<View>_Accum" names
    // AND "RenderFeatureCompositor_<View>_Seed" names (same map, both roles -
    // they never collide, since interned names always carry either "_Accum"
    // or "_Seed" as an unambiguous suffix).
    std::unordered_map<std::string, BlendStageState> m_blendStageStates;
};

} // namespace gte
