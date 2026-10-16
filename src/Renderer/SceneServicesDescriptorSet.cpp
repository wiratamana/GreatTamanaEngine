#include "SceneServicesDescriptorSet.h"

#include "DescriptorSet.h"
#include "Renderer.h"
#include "Vulkan/DescriptorSetLayoutBuilder.h"
#include "../Core/Logging.h"

#include <cassert>
#include <cstring>
#include <string>

namespace {

struct SceneServiceSlotEntry {
    bool registered = false;
    std::string debugName;
    gte::SceneServiceResourceKind kind = gte::SceneServiceResourceKind::Image2D;
    bool isDepthResource = false;
    bool hasLoggedKindMismatch = false;
    bool hasLoggedPreferredIndexMismatch = false;
};

enum class SceneServiceRegistrationFailureReason { Collision, Exhaustion, InvalidPreferredIndex, PostSeal };

struct SceneServiceRegistrationFailure {
    std::string debugName;
    SceneServiceRegistrationFailureReason reason;
    std::uint32_t resultToReplay; // whatever this exact (name, reason) must keep returning.
};

std::array<SceneServiceSlotEntry, gte::kSceneServiceSlotCount>& SceneServiceSlotTable()
{
    static std::array<SceneServiceSlotEntry, gte::kSceneServiceSlotCount> table{};
    return table;
}

std::vector<SceneServiceRegistrationFailure>& SceneServiceFailureMemory()
{
    static std::vector<SceneServiceRegistrationFailure> failures;
    return failures;
}

bool& SceneServiceRegistrySealedFlag()
{
    static bool sealed = false;
    return sealed;
}

} // namespace

namespace gte {

rg::RenderPassId SceneServiceBlackboardKey(std::uint32_t slotIndex, rg::RenderViewId view) noexcept
{
    // One running FNV-1a over view.Hash() then the slot index. A weak combiner (xor/add) could alias two
    // (slot, view) pairs onto one key, so this must be correct by construction.
    std::uint64_t hash = 2166136261u;

    const std::uint64_t viewHash = view.Hash();
    unsigned char viewBytes[sizeof(viewHash)];
    std::memcpy(viewBytes, &viewHash, sizeof(viewHash));
    for (unsigned char b : viewBytes) {
        hash ^= static_cast<std::uint64_t>(b);
        hash *= 16777619u;
    }

    unsigned char slotBytes[sizeof(slotIndex)];
    std::memcpy(slotBytes, &slotIndex, sizeof(slotIndex));
    for (unsigned char b : slotBytes) {
        hash ^= static_cast<std::uint64_t>(b);
        hash *= 16777619u;
    }

    return rg::RenderPassId{ hash };
}

// --- Runtime slot registry: a feature claims a slot by debug name at startup ---

std::uint32_t RegisterSceneServiceSlot(
    const char* debugName, SceneServiceResourceKind kind, std::optional<std::uint32_t> preferredIndex,
    bool isDepthResource)
{
    if (debugName == nullptr || debugName[0] == '\0') {
        GTE_LOG_ERROR(
            "SceneServicesDescriptorSet", "RegisterSceneServiceSlot() called with a null/empty debugName - refusing.");
        return kInvalidSceneServiceSlotIndex;
    }

    std::vector<SceneServiceRegistrationFailure>& failures = SceneServiceFailureMemory();
    for (const SceneServiceRegistrationFailure& failure : failures) {
        if (failure.debugName == debugName) {
            return failure.resultToReplay;
        }
    }

    std::array<SceneServiceSlotEntry, kSceneServiceSlotCount>& table = SceneServiceSlotTable();
    for (std::uint32_t i = 0; i < kSceneServiceSlotCount; ++i) {
        SceneServiceSlotEntry& entry = table[i];
        if (!entry.registered || entry.debugName != debugName) {
            continue;
        }
        if (entry.kind != kind && !entry.hasLoggedKindMismatch) {
            GTE_LOG_ERROR("SceneServicesDescriptorSet",
                std::string("RegisterSceneServiceSlot(\"") + debugName
                    + "\") re-registered with a different SceneServiceResourceKind than its existing slot - keeping "
                      "the original kind.");
            entry.hasLoggedKindMismatch = true;
        }
        if (preferredIndex.has_value() && *preferredIndex != i && !entry.hasLoggedPreferredIndexMismatch) {
            GTE_LOG_ERROR("SceneServicesDescriptorSet",
                std::string("RegisterSceneServiceSlot(\"") + debugName
                    + "\") re-registered with a different preferredIndex than its existing slot - keeping the "
                      "original index.");
            entry.hasLoggedPreferredIndexMismatch = true;
        }
        return i;
    }

    if (SceneServiceRegistrySealedFlag()) {
        GTE_LOG_ERROR("SceneServicesDescriptorSet",
            std::string("RegisterSceneServiceSlot(\"") + debugName
                + "\") called after the registry has already sealed (SceneServicesDescriptorSet::Rewrite() already "
                  "ran once) - refusing.");
        failures.push_back(SceneServiceRegistrationFailure{
            debugName, SceneServiceRegistrationFailureReason::PostSeal, kInvalidSceneServiceSlotIndex });
        return kInvalidSceneServiceSlotIndex;
    }

    if (preferredIndex.has_value() && *preferredIndex >= kSceneServiceSlotCount) {
        GTE_LOG_ERROR("SceneServicesDescriptorSet",
            std::string("RegisterSceneServiceSlot(\"") + debugName + "\") requested an out-of-range preferredIndex - "
                                                                      "refusing.");
        failures.push_back(SceneServiceRegistrationFailure{
            debugName, SceneServiceRegistrationFailureReason::InvalidPreferredIndex, kInvalidSceneServiceSlotIndex });
        return kInvalidSceneServiceSlotIndex;
    }

    std::uint32_t claimedIndex = kInvalidSceneServiceSlotIndex;
    if (preferredIndex.has_value()) {
        if (table[*preferredIndex].registered) {
            GTE_LOG_ERROR("SceneServicesDescriptorSet",
                std::string("RegisterSceneServiceSlot(\"") + debugName + "\") requested preferredIndex "
                    + std::to_string(*preferredIndex) + " but it is already occupied by \""
                    + table[*preferredIndex].debugName + "\" - refusing.");
            failures.push_back(SceneServiceRegistrationFailure{
                debugName, SceneServiceRegistrationFailureReason::Collision, *preferredIndex });
            return *preferredIndex;
        }
        claimedIndex = *preferredIndex;
    } else {
        for (std::uint32_t i = 0; i < kSceneServiceSlotCount; ++i) {
            if (!table[i].registered) {
                claimedIndex = i;
                break;
            }
        }
        if (claimedIndex == kInvalidSceneServiceSlotIndex) {
            GTE_LOG_ERROR("SceneServicesDescriptorSet",
                std::string("RegisterSceneServiceSlot(\"") + debugName
                    + "\") found no free slot - the registry is full.");
            failures.push_back(SceneServiceRegistrationFailure{
                debugName, SceneServiceRegistrationFailureReason::Exhaustion, kInvalidSceneServiceSlotIndex });
            return kInvalidSceneServiceSlotIndex;
        }
    }

    SceneServiceSlotEntry& claimed = table[claimedIndex];
    claimed.registered = true;
    claimed.debugName = debugName;
    claimed.kind = kind;
    claimed.isDepthResource = isDepthResource;
    return claimedIndex;
}

SceneServiceResourceKind SceneServiceSlotResourceKind(std::uint32_t slotIndex) noexcept
{
    if (slotIndex >= kSceneServiceSlotCount || !SceneServiceSlotTable()[slotIndex].registered) {
        return SceneServiceResourceKind::Image2D;
    }
    return SceneServiceSlotTable()[slotIndex].kind;
}

bool SceneServiceSlotIsDepthResource(std::uint32_t slotIndex) noexcept
{
    if (slotIndex >= kSceneServiceSlotCount || !SceneServiceSlotTable()[slotIndex].registered) {
        return false;
    }
    return SceneServiceSlotTable()[slotIndex].isDepthResource;
}

void ResetSceneServiceRegistryForTesting() noexcept
{
    for (SceneServiceSlotEntry& entry : SceneServiceSlotTable()) {
        entry = SceneServiceSlotEntry{};
    }
    SceneServiceFailureMemory().clear();
    SceneServiceRegistrySealedFlag() = false;
}

// --- SceneServicesDescriptorSet --------------------------------------------

namespace {

// 8 fragment-stage combined image samplers plus the lighting UBO at binding 8.
VkDescriptorSetLayout BuildSceneServicesLayout(VkDevice device)
{
    DescriptorSetLayoutBuilder layoutBuilder(device);
    for (std::uint32_t i = 0; i < kSceneServiceSlotCount; ++i) {
        layoutBuilder.AddCombinedImageSampler(i, VK_SHADER_STAGE_FRAGMENT_BIT);
    }
    layoutBuilder.AddUniformBuffer(kSceneServiceSlotCount, VK_SHADER_STAGE_FRAGMENT_BIT);
    return layoutBuilder.Build();
}

} // namespace

SceneServicesDescriptorSet::SceneServicesDescriptorSet(Renderer& renderer)
    : m_renderer(&renderer)
    , m_device(renderer.GetVulkanContextInfo().device)
    , m_layout(m_device, BuildSceneServicesLayout(m_device))
    // 1x1x1 RGBA8_UNORM: mandatory storage-image support, so no fallback format is needed.
    , m_dummyImage3DTexture(renderer.CreateVolumeTexture(1, 1, 1, PixelFormat::RGBA8_UNorm, "SceneServiceImage3DDummy"))
    // Zero-filled fallback for binding 8.
    , m_dummyGlobalUniformBuffer(renderer.CreateBuffer(kSceneGlobalUniformBlockSize, BufferUsage::UniformBuffer,
          BufferMemoryUsage::CpuToGpu, "SceneGlobalUniformBlockDummy"))
{
    // Every Image2D-kind slot gets a dummy: one opaque-white pixel, meaning "fully lit / neutral".
    const unsigned char whitePixel[4] = { 255, 255, 255, 255 };
    for (std::uint32_t i = 0; i < kSceneServiceSlotCount; ++i) {
        if (SceneServiceSlotResourceKind(i) == SceneServiceResourceKind::Image2D) {
            const std::string debugName = "SceneServiceSlot" + std::to_string(i) + "Dummy";
            m_dummyImage2DTextures.emplace(i, renderer.CreateTexture2D(whitePixel, 1, 1, debugName.c_str()));
        }
    }

    const std::array<std::byte, static_cast<std::size_t>(kSceneGlobalUniformBlockSize)> zeroBytes{};
    m_dummyGlobalUniformBuffer.Upload(zeroBytes.data(), zeroBytes.size());
}

SceneServicesDescriptorSet::~SceneServicesDescriptorSet() = default;

DescriptorSetHandle SceneServicesDescriptorSet::Rewrite(
    rg::RenderViewId view, const std::array<ResolvedSlot, kSceneServiceSlotCount>& resolved)
{
    SceneServiceRegistrySealedFlag() = true; // Startup-only registration: this is the first-ever call's permanent cutoff.

    DescriptorSetHandle handle = DescriptorSetHandle::Invalid;
    for (PerViewSet& entry : m_perViewSets) {
        if (entry.view == view) {
            handle = entry.handle;
            break;
        }
    }
    VkDescriptorSet set = Native(handle);
    const bool freshlyAllocated = (set == VK_NULL_HANDLE);
    if (freshlyAllocated) {
        set = m_renderer->AllocateDescriptorSet(m_layout.Native()).Native();
        handle = RegisterDescriptorSet(set);
        m_perViewSets.push_back(PerViewSet{ view, handle });
    }

    std::array<VkDescriptorImageInfo, kSceneServiceSlotCount> imageInfos{};
    std::array<VkWriteDescriptorSet, kSceneServiceSlotCount + 1> writes{};

    for (std::uint32_t i = 0; i < kSceneServiceSlotCount; ++i) {
        VkImageView view_ = resolved[i].view;
        VkSampler sampler = resolved[i].sampler;

        // A slot is published only when both view and sampler are non-null; a half-null pair (e.g. an imported
        // texture without a sampler) uses the dummy instead of reaching vkUpdateDescriptorSets().
        if (view_ == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE) {
            if (SceneServiceSlotResourceKind(i) == SceneServiceResourceKind::Image2D) {
                const Texture2D& dummy = m_dummyImage2DTextures.at(i);
                view_ = dummy.View();
                sampler = dummy.Sampler();
            } else {
                view_ = m_dummyImage3DTexture.View();
                sampler = m_dummyImage3DTexture.Sampler();
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

    // Binding 8 has no per-slot fallback: a fresh set gets it written in this same batched update
    // (two back-to-back updates on a new set crash some drivers).
    VkDescriptorBufferInfo dummyBufferInfo{};
    std::uint32_t writeCount = kSceneServiceSlotCount;
    if (freshlyAllocated) {
        dummyBufferInfo = { m_dummyGlobalUniformBuffer.Native(), 0, kSceneGlobalUniformBlockSize };
        VkWriteDescriptorSet& dummyWrite = writes[kSceneServiceSlotCount];
        dummyWrite = VkWriteDescriptorSet{};
        dummyWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        dummyWrite.dstSet = set;
        dummyWrite.dstBinding = kSceneServiceSlotCount;
        dummyWrite.descriptorCount = 1;
        dummyWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        dummyWrite.pBufferInfo = &dummyBufferInfo;
        writeCount = kSceneServiceSlotCount + 1;
    }

    vkUpdateDescriptorSets(m_device, writeCount, writes.data(), 0, nullptr);
    return handle;
}

DescriptorSetHandle SceneServicesDescriptorSet::DescriptorSetFor(rg::RenderViewId view) const noexcept
{
    for (const PerViewSet& entry : m_perViewSets) {
        if (entry.view == view) {
            return entry.handle;
        }
    }
    return DescriptorSetHandle::Invalid;
}

const Texture2D& SceneServicesDescriptorSet::DummyImage2DTextureFor(std::uint32_t slotIndex) const
{
    assert(SceneServiceSlotResourceKind(slotIndex) == SceneServiceResourceKind::Image2D
        && "SceneServicesDescriptorSet::DummyImage2DTextureFor: slotIndex is not an Image2D-kind slot.");
    return m_dummyImage2DTextures.at(slotIndex);
}

void SceneServicesDescriptorSet::UpdateGlobalUniformBlock(rg::RenderViewId view, std::span<const std::byte> bytes)
{
    assert(bytes.size() <= static_cast<std::size_t>(kSceneGlobalUniformBlockSize)
        && "UpdateGlobalUniformBlock: payload exceeds the fixed ceiling - grow kSceneGlobalUniformBlockSize.");

    PerViewUniformBuffer* entry = nullptr;
    for (PerViewUniformBuffer& candidate : m_perViewUniformBuffers) {
        if (candidate.view == view) {
            entry = &candidate;
            break;
        }
    }
    const bool freshlyCreatedBuffer = (entry == nullptr);
    if (freshlyCreatedBuffer) {
        m_perViewUniformBuffers.push_back(PerViewUniformBuffer{ view,
            m_renderer->CreateBuffer(kSceneGlobalUniformBlockSize, BufferUsage::UniformBuffer,
                BufferMemoryUsage::CpuToGpu, "SceneGlobalUniformBlock") });
        entry = &m_perViewUniformBuffers.back();
    }
    // The mapped buffer handle never changes, so every later frame only memcpy's fresh bytes.
    entry->buffer.Upload(bytes.data(), bytes.size());

    // Allocate the shared per-view set lazily here too: the first caller for a view creates it and the
    // other reuses it. Do not turn this into an assert; a PreOpaque depth pass may touch the view first.
    DescriptorSetHandle handle = DescriptorSetHandle::Invalid;
    for (PerViewSet& perView : m_perViewSets) {
        if (perView.view == view) {
            handle = perView.handle;
            break;
        }
    }
    VkDescriptorSet set = Native(handle);
    const bool freshlyAllocatedSet = (set == VK_NULL_HANDLE);
    if (freshlyAllocatedSet) {
        set = m_renderer->AllocateDescriptorSet(m_layout.Native()).Native();
        handle = RegisterDescriptorSet(set);
        m_perViewSets.push_back(PerViewSet{ view, handle });
    }

    // Point binding 8 at the buffer only once: updating a set that may still be in flight is unsafe (crashes some
    // drivers). Only the frame the buffer or set is new touches it; later frames just refresh the bytes via Upload().
    if (freshlyCreatedBuffer || freshlyAllocatedSet) {
        VkDescriptorBufferInfo bufferInfo{ entry->buffer.Native(), 0, kSceneGlobalUniformBlockSize };
        VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = set;
        write.dstBinding = kSceneServiceSlotCount;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    }
}

} // namespace gte
