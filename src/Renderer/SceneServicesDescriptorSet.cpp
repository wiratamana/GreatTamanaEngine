#include "SceneServicesDescriptorSet.h"

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

std::vector<std::uint32_t>& SceneServiceRegistrationOrder()
{
    static std::vector<std::uint32_t> order;
    return order;
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

    unsigned char slotBytes[sizeof(slotIndex)];
    std::memcpy(slotBytes, &slotIndex, sizeof(slotIndex));
    for (unsigned char b : slotBytes) {
        hash ^= static_cast<std::uint64_t>(b);
        hash *= 16777619u;
    }

    return rg::RenderPassId{ hash };
}

// --- Additive runtime slot registry ----------------------------------------
//
// Lets any built-in feature claim a scene-service slot by debug name at
// startup instead of this header hand-naming every consumer. See this
// header's own doc comments for the exact contract each function below
// implements.

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
    SceneServiceRegistrationOrder().push_back(claimedIndex);
    return claimedIndex;
}

std::size_t RegisteredSceneServiceSlotCount() noexcept
{
    return SceneServiceRegistrationOrder().size();
}

std::uint32_t RegisteredSceneServiceSlotIndexAt(std::size_t i) noexcept
{
    return SceneServiceRegistrationOrder()[i];
}

const char* SceneServiceSlotDebugName(std::uint32_t slotIndex) noexcept
{
    if (slotIndex >= kSceneServiceSlotCount || !SceneServiceSlotTable()[slotIndex].registered) {
        return "<unregistered>";
    }
    return SceneServiceSlotTable()[slotIndex].debugName.c_str();
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
    SceneServiceRegistrationOrder().clear();
    SceneServiceFailureMemory().clear();
    SceneServiceRegistrySealedFlag() = false;
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
    , m_dummyImage3DTexture(renderer.CreateVolumeTexture(1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, "SceneServiceImage3DDummy"))
    // Binding 8's own zero-filled dummy fallback - see this member's own
    // doc comment in the header for why Rewrite()'s per-slot loop cannot
    // cover it.
    , m_dummyGlobalUniformBuffer(renderer.CreateBuffer(kSceneGlobalUniformBlockSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
          BufferMemoryUsage::CpuToGpu, "SceneGlobalUniformBlockDummy"))
{
    // The one reserved "scene services" descriptor-set layout - 8 combined-
    // image-sampler bindings (fragment-stage), plus one plain uniform buffer
    // at binding 8 (the packed per-frame lighting UBO - see
    // kSceneGlobalUniformBlockSize) - this set is consumed by a fragment
    // shader, never a compute one.
    DescriptorSetLayoutBuilder layoutBuilder(m_device);
    for (std::uint32_t i = 0; i < kSceneServiceSlotCount; ++i) {
        layoutBuilder.AddCombinedImageSampler(i, VK_SHADER_STAGE_FRAGMENT_BIT);
    }
    layoutBuilder.AddUniformBuffer(kSceneServiceSlotCount, VK_SHADER_STAGE_FRAGMENT_BIT);
    m_layout = layoutBuilder.Build();

    // Every Image2D-kind slot's real, uploaded dummy - a single opaque-white
    // {255,255,255,255} pixel (semantically "fully lit / neutral / no extra
    // contribution" - re-confirm this neutral meaning against any future
    // slot's own real semantics before relying on it).
    const unsigned char whitePixel[4] = { 255, 255, 255, 255 };
    for (std::uint32_t i = 0; i < kSceneServiceSlotCount; ++i) {
        if (SceneServiceSlotResourceKind(i) == SceneServiceResourceKind::Image2D) {
            const std::string debugName = "SceneServiceSlot" + std::to_string(i) + "Dummy";
            m_dummyImage2DTextures.emplace(i, renderer.CreateTexture2D(whitePixel, 1, 1, debugName.c_str()));
        }
    }

    // Zero-fill the dummy UBO exactly once - never written again after this.
    const std::array<std::byte, static_cast<std::size_t>(kSceneGlobalUniformBlockSize)> zeroBytes{};
    m_dummyGlobalUniformBuffer.Upload(zeroBytes.data(), zeroBytes.size());
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
    SceneServiceRegistrySealedFlag() = true; // Startup-only registration: this is the first-ever call's permanent cutoff.

    VkDescriptorSet set = VK_NULL_HANDLE;
    for (PerViewSet& entry : m_perViewSets) {
        if (entry.view == view) {
            set = entry.set;
            break;
        }
    }
    const bool freshlyAllocated = (set == VK_NULL_HANDLE);
    if (freshlyAllocated) {
        set = m_renderer->AllocateComputeDescriptorSet(m_layout);
        m_perViewSets.push_back(PerViewSet{ view, set });
    }

    std::array<VkDescriptorImageInfo, kSceneServiceSlotCount> imageInfos{};
    std::array<VkWriteDescriptorSet, kSceneServiceSlotCount + 1> writes{};

    for (std::uint32_t i = 0; i < kSceneServiceSlotCount; ++i) {
        VkImageView view_ = resolved[i].view;
        VkSampler sampler = resolved[i].sampler;

        // A slot counts as "really published this call" ONLY when BOTH
        // halves are non-null; a half-null pair (e.g. a resolved IMPORTED
        // texture handle with a null sampler) falls back to the dummy
        // instead of ever reaching vkUpdateDescriptorSets().
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

    // binding 8 has no per-slot fallback above - a freshly allocated set
    // needs it written too, batched into this SAME call (never a separate
    // vkUpdateDescriptorSets - two back-to-back updates against a brand-new
    // set is a known crash trigger on at least one targeted Vulkan driver).
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
            m_renderer->CreateBuffer(kSceneGlobalUniformBlockSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                BufferMemoryUsage::CpuToGpu, "SceneGlobalUniformBlock") });
        entry = &m_perViewUniformBuffers.back();
    }
    // Every frame just memcpy's fresh bytes into this view's OWN already-
    // mapped buffer - the buffer HANDLE never changes again after today, so
    // the descriptor set below never needs to be touched again either.
    entry->buffer.Upload(bytes.data(), bytes.size());

    // Lazily allocate+register the shared per-view set here too - do NOT
    // assert Rewrite() already ran. Whichever of the two runs first for a
    // view creates the set; the other just reuses it from m_perViewSets.
    // Mirrors Rewrite()'s own lazy-allocate pattern exactly - never
    // "simplify" this back into an assert, the ordering is load-bearing
    // (Shadow.DepthPass runs at PreOpaques, strictly BEFORE RenderOpaque,
    // so it may well be the FIRST thing to touch a view's set this frame).
    VkDescriptorSet set = VK_NULL_HANDLE;
    for (PerViewSet& perView : m_perViewSets) {
        if (perView.view == view) {
            set = perView.set;
            break;
        }
    }
    const bool freshlyAllocatedSet = (set == VK_NULL_HANDLE);
    if (freshlyAllocatedSet) {
        set = m_renderer->AllocateComputeDescriptorSet(m_layout);
        m_perViewSets.push_back(PerViewSet{ view, set });
    }

    // Binding 8 only ever needs to be POINTED at this view's buffer ONCE -
    // re-running vkUpdateDescriptorSets every frame against a set that may
    // still be in-flight on the GPU is unsafe and a known crash trigger on
    // at least one targeted Vulkan driver. Only touch it the one frame
    // either the buffer or the set is brand new; every later frame just
    // relies on the Upload() above to refresh the bytes this SAME binding
    // already points at.
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
