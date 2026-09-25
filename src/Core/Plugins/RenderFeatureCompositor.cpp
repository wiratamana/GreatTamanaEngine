#include "RenderFeatureCompositor.h"

#include "PluginRenderPassBuilderAdapter_v2.h"

#include "../Core.h"
#include "../Logging.h"

#include "../../Renderer/ComputeDispatch.h"
#include "../../Renderer/Vulkan/DescriptorSetLayoutBuilder.h"

#include <cassert>
#include <cstring>
#include <utility>

namespace gte {

RenderFeatureCompositor::RenderFeatureCompositor(Core& core, Renderer& renderer)
    : m_core(core)
    , m_renderer(renderer)
{
}

// editor-core-separation-6 campaign, PHASE4 (Step 3.4) - discovers every
// loaded IRenderFeatureModule_v2, snapshots its descriptor exactly once,
// refuses (loudly) any module declaring an unwired stage
// (PHASE0_MASTER_STRATEGY.md's Locked Design Decision #1), then sorts each
// stage's own surviving entries by priority ascending with a documented,
// stable, lexical tie-break for a same-priority collision (never left to
// std::sort's own unspecified-for-equal-keys behavior).
void RenderFeatureCompositor::OnPluginsLoaded(const std::vector<IPluginModule*>& modules)
{
    for (IPluginModule* module : modules) {
        auto* feature =
            static_cast<IRenderFeatureModule_v2*>(module->QueryCapability(kIRenderFeatureModule_v2_Name));
        if (feature == nullptr) {
            continue;
        }

        Entry entry;
        entry.module = feature;
        entry.descriptor = feature->GetRenderFeatureDescriptor();

        if (entry.descriptor.stage == RenderFeatureStage::PreOpaque
            || entry.descriptor.stage == RenderFeatureStage::PostOpaque
            || entry.descriptor.stage == RenderFeatureStage::PostTransparent) {
            GTE_LOG_WARNING("RenderFeatureCompositor",
                std::string(entry.descriptor.name) + " declared a RenderFeatureStage that is not wired in this "
                "engine build - this feature will not run any frame. See "
                "task_manager/editor-core-separation-6/PHASE0_MASTER_STRATEGY.md's Locked Design Decision #1.");
            continue;
        }

        if (entry.descriptor.stage == RenderFeatureStage::PreUI) {
            m_preUi.push_back(entry);
        } else {
            m_postComposite.push_back(entry);
        }
    }

    auto sortAndDetectCollisions = [](std::vector<Entry>& entries, const char* stageName) {
        std::stable_sort(entries.begin(), entries.end(),
            [](const Entry& a, const Entry& b) { return a.descriptor.priority < b.descriptor.priority; });

        std::size_t i = 0;
        while (i < entries.size()) {
            std::size_t j = i;
            while (j + 1 < entries.size() && entries[j + 1].descriptor.priority == entries[i].descriptor.priority) {
                ++j;
            }
            if (j > i) {
                // entries[i..j] all declared the identical priority within
                // this stage - an ambiguous, but never-crashing, situation
                // (PHASE0_MASTER_STRATEGY.md's own Locked Design Decision,
                // mirroring the Proposal's Section 3.4 step 3).
                for (std::size_t k = i; k < j; ++k) {
                    GTE_LOG_WARNING("RenderFeatureCompositor",
                        std::string(entries[k].descriptor.name) + " and " + entries[k + 1].descriptor.name
                        + " both declared priority " + std::to_string(entries[k].descriptor.priority) + " in stage "
                        + stageName + " - this is ambiguous; falling back to a stable, lexical name tie-break. "
                        "Assign each plugin a distinct priority to remove this warning.");
                }
                std::stable_sort(entries.begin() + static_cast<std::ptrdiff_t>(i),
                    entries.begin() + static_cast<std::ptrdiff_t>(j) + 1, [](const Entry& a, const Entry& b) {
                        return std::strcmp(a.descriptor.name, b.descriptor.name) < 0;
                    });
            }
            i = j + 1;
        }
    };

    sortAndDetectCollisions(m_postComposite, "PostComposite");
    sortAndDetectCollisions(m_preUi, "PreUI");

    // Populate the name pool for every surviving entry now, for BOTH known
    // views ("Game"/"Scene" - confirmed the only two RenderViewId::Named()
    // values anywhere in this engine, PHASE4's own Step 2 evidence) - the set
    // of loaded plugins AND the set of views are both fixed for the
    // process's entire remaining lifetime, never re-interned per frame.
    static constexpr const char* kViewNames[] = { "Game", "Scene" };
    for (const char* viewName : kViewNames) {
        m_namePool.SeedName(viewName);
        m_namePool.SeedCopyPassName(viewName);
        for (const Entry& entry : m_postComposite) {
            m_namePool.PrivateName(entry.descriptor.name, viewName);
            m_namePool.AccumName(entry.descriptor.name, viewName);
            m_namePool.BlendPassName(entry.descriptor.name, viewName);
        }
        for (const Entry& entry : m_preUi) {
            m_namePool.PrivateName(entry.descriptor.name, viewName);
            m_namePool.AccumName(entry.descriptor.name, viewName);
            m_namePool.BlendPassName(entry.descriptor.name, viewName);
        }
    }
}

void RenderFeatureCompositor::EnsureTextureSized(
    std::optional<RenderTexture>& texture, const char* internedName, VkExtent2D extent)
{
    const int width = extent.width > 0 ? static_cast<int>(extent.width) : 1;
    const int height = extent.height > 0 ? static_cast<int>(extent.height) : 1;

    if (!texture.has_value()) {
        texture.emplace(m_renderer.CreateRenderTexture(width, height, VK_FORMAT_R8G8B8A8_UNORM, internedName,
            /*depthDebugName=*/nullptr, /*allowStorageImageAccess=*/true));
        return;
    }

    const VkExtent2D currentExtent = texture->Extent();
    if (currentExtent.width != extent.width || currentExtent.height != extent.height) {
        // Resizes are rare/user-driven (dragging the Game/Scene panel's
        // border) - a full device stall here is the simplest correct thing,
        // mirroring AtmosphereLutRenderer's own identical discipline.
        vkDeviceWaitIdle(m_device);
        texture->Resize(width, height);
    }
}

RenderFeatureCompositor::PrivateTargetState& RenderFeatureCompositor::EnsurePrivateTargetState(
    const char* internedName, VkExtent2D extent)
{
    const auto existing = m_privateTargetStates.find(internedName);
    if (existing != m_privateTargetStates.end()) {
        EnsureTextureSized(existing->second.texture, internedName, extent);
        return existing->second;
    }

    PrivateTargetState state;
    state.opsDescriptorSet = ComputeDescriptorSet(m_renderer.AllocateComputeDescriptorSet(m_opsDescriptorSetLayout));
    EnsureTextureSized(state.texture, internedName, extent);
    const auto inserted = m_privateTargetStates.emplace(internedName, std::move(state));
    return inserted.first->second;
}

RenderFeatureCompositor::BlendStageState& RenderFeatureCompositor::EnsureBlendStageDescriptorOnly(
    const char* internedName)
{
    const auto existing = m_blendStageStates.find(internedName);
    if (existing != m_blendStageStates.end()) {
        return existing->second;
    }

    BlendStageState state;
    state.blendDescriptorSet =
        ComputeDescriptorSet(m_renderer.AllocateComputeDescriptorSet(m_blendStubDescriptorSetLayout));
    const auto inserted = m_blendStageStates.emplace(internedName, std::move(state));
    return inserted.first->second;
}

RenderFeatureCompositor::BlendStageState& RenderFeatureCompositor::EnsureBlendStageState(
    const char* internedName, VkExtent2D extent)
{
    BlendStageState& state = EnsureBlendStageDescriptorOnly(internedName);
    EnsureTextureSized(state.texture, internedName, extent);
    return state;
}

void RenderFeatureCompositor::EnsureOpsInitialized(Renderer& renderer)
{
    if (m_opsPipeline.has_value()) {
        return;
    }

    const Renderer::VulkanContextInfo context = renderer.GetVulkanContextInfo();
    m_device = context.device;

    // Binding convention (matches Shaders/RenderFeatureOps.comp exactly):
    // binding 0 = privateTarget, the ONLY binding - a single read-write
    // storage image this shader always fully overwrites/reads back
    // (opCode 2 only) via imageLoad/imageStore, never sampled.
    DescriptorSetLayoutBuilder layoutBuilder(m_device);
    m_opsDescriptorSetLayout = layoutBuilder.AddStorageImage(/*binding=*/0).Build();

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(RenderFeatureOpsPushConstants);

    m_opsPipeline.emplace(renderer.CreateComputePipeline("shaders/RenderFeatureOps.comp.spv",
        std::vector<VkDescriptorSetLayout>{ m_opsDescriptorSetLayout }, pushConstantRange));
}

void RenderFeatureCompositor::EnsureBlendStubInitialized(Renderer& renderer)
{
    if (m_blendStubPipeline.has_value()) {
        return;
    }

    const Renderer::VulkanContextInfo context = renderer.GetVulkanContextInfo();
    m_device = context.device;

    // Binding convention (matches Shaders/RenderFeatureBlendStub.comp
    // exactly): binding 0 = dstIn, binding 1 = srcIn (both read-only
    // combined image samplers), binding 2 = destinationImage (a write-only
    // storage image) - DELIBERATELY IDENTICAL in shape to PHASE5's own real,
    // permanent RenderFeatureBlend.comp, so that future swap requires no
    // descriptor-set-layout/binding-kind change (PHASE4_RENDER_FEATURE_COMPOSITOR_CORE_AND_ORDERING.md's
    // own Step 3.6).
    DescriptorSetLayoutBuilder layoutBuilder(m_device);
    m_blendStubDescriptorSetLayout = layoutBuilder.AddCombinedImageSampler(/*binding=*/0)
                                          .AddCombinedImageSampler(/*binding=*/1)
                                          .AddStorageImage(/*binding=*/2)
                                          .Build();

    m_blendStubPipeline.emplace(renderer.CreateComputePipeline(
        "shaders/RenderFeatureBlendStub.comp.spv", std::vector<VkDescriptorSetLayout>{ m_blendStubDescriptorSetLayout }));
}

void RenderFeatureCompositor::DispatchOps(rg::RenderGraphBuilder& builder, rg::TextureHandle privateTarget,
    const char* stateKey, const char* debugName, const RenderFeatureOpsPushConstants& pushConstants)
{
    const auto it = m_privateTargetStates.find(stateKey);
    // Must already exist - ContributeRenderGraphPasses() creates/resizes it
    // before constructing the PluginRenderPassBuilderAdapter_v2 that calls
    // this - a missing entry here is a programmer error, not a
    // runtime-recoverable one.
    assert(it != m_privateTargetStates.end()
        && "RenderFeatureCompositor::DispatchOps: stateKey was never created by ContributeRenderGraphPasses() this "
           "frame");
    if (it == m_privateTargetStates.end()) {
        return;
    }
    PrivateTargetState& state = it->second;
    const VkExtent2D extent = state.texture->Extent();

    builder.AddRenderPass(debugName, rg::PassKind::Compute, rg::ViewScope::Shared, rg::RenderPassCategory::General,
        [privateTarget](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteTexture(privateTarget, rg::ResourceAccess::ComputeShaderWrite);
        },
        [this, &state, privateTarget, pushConstants, extent](rg::PassContext& ctx) {
            const rg::PassContext::ResolvedTexture dest = ctx.resolveTexture(privateTarget);
            state.opsDescriptorSet.Rewrite(
                m_device, std::vector<ComputeDescriptorWrite>{ ComputeDescriptorWrite::StorageImage(0, dest.view) });

            RenderFeatureOpsPushConstants localPushConstants = pushConstants;
            const Extent3D groupCounts =
                ComputeGroupCount3D(Extent3D{ extent.width, extent.height, 1 }, Extent3D{ 16, 16, 1 });

            m_renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
            m_renderer.Dispatch(*m_opsPipeline, state.opsDescriptorSet.Native(), &localPushConstants,
                sizeof(localPushConstants), groupCounts.width, groupCounts.height, groupCounts.depth);
            m_renderer.EndGraphPassRecording();
        },
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::AfterEverything);
}

void RenderFeatureCompositor::DispatchBlendStub(rg::RenderGraphBuilder& builder, rg::TextureHandle dstIn,
    VkSampler dstInSampler, rg::TextureHandle srcIn, VkSampler srcInSampler, rg::TextureHandle destination,
    BlendStageState& state, const char* debugName, VkExtent2D extent)
{
    builder.AddRenderPass(debugName, rg::PassKind::Compute, rg::ViewScope::Shared, rg::RenderPassCategory::General,
        [dstIn, srcIn, destination](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(dstIn, rg::ResourceAccess::ShaderRead);
            pass.ReadTexture(srcIn, rg::ResourceAccess::ShaderRead);
            pass.WriteTexture(destination, rg::ResourceAccess::ComputeShaderWrite);
        },
        [this, &state, dstIn, dstInSampler, srcIn, srcInSampler, destination, extent](rg::PassContext& ctx) {
            const rg::PassContext::ResolvedTexture dstResolved = ctx.resolveTexture(dstIn);
            const rg::PassContext::ResolvedTexture srcResolved = ctx.resolveTexture(srcIn);
            const rg::PassContext::ResolvedTexture destResolved = ctx.resolveTexture(destination);

            state.blendDescriptorSet.Rewrite(m_device,
                std::vector<ComputeDescriptorWrite>{
                    ComputeDescriptorWrite::CombinedImageSampler(0, dstResolved.view, dstInSampler),
                    ComputeDescriptorWrite::CombinedImageSampler(1, srcResolved.view, srcInSampler),
                    ComputeDescriptorWrite::StorageImage(2, destResolved.view),
                });

            const Extent3D groupCounts =
                ComputeGroupCount3D(Extent3D{ extent.width, extent.height, 1 }, Extent3D{ 16, 16, 1 });

            m_renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
            m_renderer.Dispatch(*m_blendStubPipeline, state.blendDescriptorSet.Native(), nullptr, 0,
                groupCounts.width, groupCounts.height, groupCounts.depth);
            m_renderer.EndGraphPassRecording();
        },
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::AfterEverything);
}

// editor-core-separation-6 campaign, PHASE4 (Step 3.4) - the real
// end-to-end pipeline: seed the chain (closing the same-physical-image
// read+write hazard for the N == 1 case), then walk every entry in this
// view's combined (PostComposite, then PreUI) list, giving each its own
// private target to draw into and its own dedicated blend dispatch into
// either the next accumulator or (for the LAST entry) directly into the
// view's own real, final handle (PHASE0_MASTER_STRATEGY.md's Locked Design
// Decision #10).
void RenderFeatureCompositor::ContributeRenderGraphPasses(
    const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&)
{
    std::vector<Entry> combinedList;
    combinedList.reserve(m_postComposite.size() + m_preUi.size());
    combinedList.insert(combinedList.end(), m_postComposite.begin(), m_postComposite.end());
    combinedList.insert(combinedList.end(), m_preUi.begin(), m_preUi.end());
    if (combinedList.empty()) {
        return;
    }

    const std::optional<Core::PluginRenderFeatureTargetInfo> resolved = m_core.FindPluginRenderFeatureTarget(frame);
    if (!resolved.has_value()) {
        return;
    }

    EnsureOpsInitialized(m_renderer);
    EnsureBlendStubInitialized(m_renderer);

    const bool isGameView = (frame.currentView == rg::RenderViewId::Named("Game"));
    const std::string viewName = isGameView ? "Game" : "Scene";
    const VkExtent2D extent = resolved->extent;

    // Seed the chain - see this class's own header comment/PHASE4's own
    // Step 3.4 point 4 for exactly why: without this, `currentInput` and the
    // LAST entry's own `outputTarget` would be the SAME handle
    // (resolved->target) in the SAME dispatch whenever the combined list has
    // exactly one entry (N == 1) - a real GPU hazard (a compute pass
    // reading and writing the exact same storage image in one dispatch).
    const char* seedName = m_namePool.SeedName(viewName);
    BlendStageState& seedState = EnsureBlendStageState(seedName, extent);
    const rg::TextureHandle seedHandle =
        frame.builder.ImportTexture(seedName, seedState.texture->Target(), VK_IMAGE_LAYOUT_UNDEFINED);
    DispatchBlendStub(frame.builder, resolved->target, resolved->sampler, resolved->target, resolved->sampler,
        seedHandle, seedState, m_namePool.SeedCopyPassName(viewName), extent);

    rg::TextureHandle currentInput = seedHandle;
    VkSampler currentInputSampler = seedState.texture->Sampler();

    for (std::size_t i = 0; i < combinedList.size(); ++i) {
        const Entry& entry = combinedList[i];
        const std::string pluginName = entry.descriptor.name;
        const bool isLast = (i + 1 == combinedList.size());

        const char* privateName = m_namePool.PrivateName(pluginName, viewName);
        PrivateTargetState& privateState = EnsurePrivateTargetState(privateName, extent);
        const rg::TextureHandle privateTarget =
            frame.builder.ImportTexture(privateName, privateState.texture->Target(), VK_IMAGE_LAYOUT_UNDEFINED);

        PluginRenderPassBuilderAdapter_v2 adapter(frame.builder, privateTarget, *this, privateName);
        entry.module->AddRenderGraphPasses(adapter);

        rg::TextureHandle outputTarget;
        BlendStageState* outputState = nullptr;
        if (isLast) {
            // PHASE0_MASTER_STRATEGY.md's Locked Design Decision #10 - the
            // LAST entry's blend writes back into the SAME handle the
            // legacy `_v1` path already writes into.
            outputTarget = resolved->target;
            outputState = &EnsureBlendStageDescriptorOnly(m_namePool.AccumName(pluginName, viewName));
        } else {
            const char* accumName = m_namePool.AccumName(pluginName, viewName);
            outputState = &EnsureBlendStageState(accumName, extent);
            outputTarget = frame.builder.ImportTexture(accumName, outputState->texture->Target(), VK_IMAGE_LAYOUT_UNDEFINED);
        }

        DispatchBlendStub(frame.builder, currentInput, currentInputSampler, privateTarget,
            privateState.texture->Sampler(), outputTarget, *outputState, m_namePool.BlendPassName(pluginName, viewName),
            extent);

        currentInput = outputTarget;
        currentInputSampler = isLast ? VK_NULL_HANDLE : outputState->texture->Sampler();
    }

    frame.finalTextureOutputs.push_back(resolved->target);
}

} // namespace gte
