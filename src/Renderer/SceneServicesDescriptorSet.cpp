#include "SceneServicesDescriptorSet.h"

#include "Renderer.h"
#include "Vulkan/DescriptorSetLayoutBuilder.h"

#include <cassert>
#include <cstring>
#include <string>

namespace gte {

rg::RenderPassId SceneServiceBlackboardKey(SceneServiceSlot slot, rg::RenderViewId view) noexcept
{
    // Reuses the EXACT SAME FNV-1a offset-basis/prime this codebase's own
    // operator""_passId / RenderViewId::Named() already use (RenderPipeline.h)
    // - continuing ONE running accumulator across the 8 bytes of
    // view.Hash() immediately followed by the 4 bytes of
    // static_cast<std::uint32_t>(slot), exactly as mandated by the source
    // design doc (BLOCK4_GLOBAL_SCENE_SERVICES_DESCRIPTOR_SET.txt, Section 3,
    // "IMPLEMENTATION REQUIREMENT"). A weak combiner (plain XOR/addition)
    // can silently alias two DIFFERENT (slot, view) pairs onto the SAME key
    // for some unlucky pair of view hashes - this must be correct by
    // construction, not merely "probably fine against the views tested".
    std::uint64_t hash = 2166136261u;

    const std::uint64_t viewHash = view.Hash();
    unsigned char viewBytes[sizeof(viewHash)];
    std::memcpy(viewBytes, &viewHash, sizeof(viewHash));
    for (unsigned char b : viewBytes) {
        hash ^= static_cast<std::uint64_t>(b);
        hash *= 16777619u;
    }

    const std::uint32_t slotValue = static_cast<std::uint32_t>(slot);
    unsigned char slotBytes[sizeof(slotValue)];
    std::memcpy(slotBytes, &slotValue, sizeof(slotValue));
    for (unsigned char b : slotBytes) {
        hash ^= static_cast<std::uint64_t>(b);
        hash *= 16777619u;
    }

    return rg::RenderPassId{ hash };
}

// --- SceneServicesDescriptorSet (PHASE2) -----------------------------------

SceneServicesDescriptorSet::SceneServicesDescriptorSet(Renderer& renderer)
    : m_renderer(&renderer)
    , m_device(renderer.GetVulkanContextInfo().device)
    // The one Image3D-kind slot's real, uploaded dummy - a neutral,
    // zero-density 1x1x1 value. VK_FORMAT_R8G8B8A8_UNORM is a Vulkan
    // mandatory-support format for VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT (see
    // the Vulkan spec's own "Mandatory format support" table), so
    // GpuResourceFactory::CreateVolumeTexture()'s own
    // SupportsStorageImageUsage() check is guaranteed to pass on every
    // Vulkan-conformant device this engine targets - no fallback format
    // branch is needed in practice (confirmed live by this phase's own
    // integration test actually constructing this class against a real
    // device).
    , m_dummyVolumetricFogTexture(renderer.CreateVolumeTexture(1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, "VolumetricFog dummy"))
{
    // The one reserved "scene services" descriptor-set layout - 8 combined-
    // image-sampler bindings, fragment-stage visible (this set is consumed
    // by a fragment shader, never a compute one - see
    // DescriptorSetLayoutBuilder's own default stageFlags comment for why
    // that default does not apply here).
    DescriptorSetLayoutBuilder layoutBuilder(m_device);
    for (std::uint32_t i = 0; i < kSceneServiceSlotCount; ++i) {
        layoutBuilder.AddCombinedImageSampler(i, VK_SHADER_STAGE_FRAGMENT_BIT);
    }
    m_layout = layoutBuilder.Build();

    // Every Image2D-kind slot's real, uploaded dummy - a single opaque-white
    // {255,255,255,255} pixel (semantically "fully lit / no shadow / no
    // fog contribution" - correct for ShadowMap; re-confirm per-slot
    // semantics the day GIVolume grows real consumers).
    const unsigned char whitePixel[4] = { 255, 255, 255, 255 };
    for (std::uint32_t i = 0; i < kSceneServiceSlotCount; ++i) {
        const auto slot = static_cast<SceneServiceSlot>(i);
        if (SceneServiceSlotResourceKind(slot) == SceneServiceResourceKind::Image2D) {
            const std::string debugName = "SceneServiceSlot" + std::to_string(i) + "Dummy";
            m_dummyImage2DTextures.emplace(i, renderer.CreateTexture2D(whitePixel, 1, 1, debugName.c_str()));
        }
    }
}

SceneServicesDescriptorSet::~SceneServicesDescriptorSet()
{
    // Texture2D/VolumeTexture (RAII) and the per-view VkDescriptorSets
    // (pool-owned, never individually freed - see this class's own header
    // comment) need no explicit cleanup here - only the hand-built,
    // non-reflected layout this class itself owns.
    if (m_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_layout, nullptr);
    }
}

VkDescriptorSet SceneServicesDescriptorSet::Rewrite(
    rg::RenderViewId view, const std::array<ResolvedSlot, kSceneServiceSlotCount>& resolved)
{
    VkDescriptorSet set = VK_NULL_HANDLE;
    for (PerViewSet& entry : m_perViewSets) {
        if (entry.view == view) {
            set = entry.set;
            break;
        }
    }
    if (set == VK_NULL_HANDLE) {
        set = m_renderer->AllocateComputeDescriptorSet(m_layout);
        m_perViewSets.push_back(PerViewSet{ view, set });
    }

    std::array<VkDescriptorImageInfo, kSceneServiceSlotCount> imageInfos{};
    std::array<VkWriteDescriptorSet, kSceneServiceSlotCount> writes{};

    for (std::uint32_t i = 0; i < kSceneServiceSlotCount; ++i) {
        const auto slot = static_cast<SceneServiceSlot>(i);

        VkImageView view_ = resolved[i].view;
        VkSampler sampler = resolved[i].sampler;

        // PHASE0 global rule 12 - a slot counts as "really published this
        // call" ONLY when BOTH halves are non-null; a half-null pair (e.g.
        // a resolved IMPORTED texture handle with a null sampler) falls
        // back to the dummy instead of ever reaching vkUpdateDescriptorSets().
        if (view_ == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE) {
            if (SceneServiceSlotResourceKind(slot) == SceneServiceResourceKind::Image2D) {
                const Texture2D& dummy = m_dummyImage2DTextures.at(i);
                view_ = dummy.View();
                sampler = dummy.Sampler();
            } else {
                view_ = m_dummyVolumetricFogTexture.View();
                sampler = m_dummyVolumetricFogTexture.Sampler();
            }
        }

        imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[i].imageView = view_;
        imageInfos[i].sampler = sampler;

        writes[i] = VkWriteDescriptorSet{};
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].dstArrayElement = 0;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &imageInfos[i];
    }

    vkUpdateDescriptorSets(m_device, kSceneServiceSlotCount, writes.data(), 0, nullptr);
    return set;
}

VkDescriptorSet SceneServicesDescriptorSet::DescriptorSetFor(rg::RenderViewId view) const noexcept
{
    for (const PerViewSet& entry : m_perViewSets) {
        if (entry.view == view) {
            return entry.set;
        }
    }
    return VK_NULL_HANDLE;
}

const Texture2D& SceneServicesDescriptorSet::DummyImage2DTextureFor(SceneServiceSlot slot) const
{
    assert(SceneServiceSlotResourceKind(slot) == SceneServiceResourceKind::Image2D
        && "SceneServicesDescriptorSet::DummyImage2DTextureFor: slot is not an Image2D-kind slot.");
    return m_dummyImage2DTextures.at(static_cast<std::uint32_t>(slot));
}

} // namespace gte
