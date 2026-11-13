#pragma once

// Scene services: engine-wide slots for optional scene textures (depth maps, GI), a per-(slot, view) blackboard key,
// and the descriptor-set class that owns the layout, per-view sets and dummy fallbacks.

#include "RenderGraph/RenderPipeline.h" // rg::RenderPassId, rg::RenderViewId
#include "Buffer.h"
#include "GpuTiming.h"
#include "OffscreenFrameTicket.h"
#include "OffscreenRingGuard.h"
#include "../Core/GraphicsHandles.h" // DescriptorSetHandle
#include "GraphicsHandleRegistry.h" // OwnedDescriptorSetLayout
#include "Texture2D.h"
#include "VolumeTexture.h"

#include <volk.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace gte {

// Upper bound on scene-service slots; a feature's preferredIndex is a durable shader binding contract
// (consuming shaders hardcode `layout(set = 1, binding = N)`).
inline constexpr std::uint32_t kSceneServiceSlotCount = 8;

// Every slot is a combined image sampler; this enum only picks the backing resource type (2D or 3D view) for real data
// and the dummy fallback, since a 2D view cannot stand in for a sampler3D.
enum class SceneServiceResourceKind : std::uint8_t { Image2D, Image3D };

// A return value >= kSceneServiceSlotCount means "no usable slot".
inline constexpr std::uint32_t kInvalidSceneServiceSlotIndex = kSceneServiceSlotCount;

// Registers a scene-service slot, idempotent by debugName (the name is copied). Startup-only: refused after the first Rewrite().
// Bad input logs once and returns kInvalidSceneServiceSlotIndex. `isDepthResource` must be true for a depth-format image, else its layout is wrong when sampled.
[[nodiscard]] std::uint32_t RegisterSceneServiceSlot(const char* debugName, SceneServiceResourceKind kind,
    std::optional<std::uint32_t> preferredIndex = std::nullopt, bool isDepthResource = false);

// Resource kind of a registered slot; Image2D for a slot index that was never registered (or is out of range).
SceneServiceResourceKind SceneServiceSlotResourceKind(std::uint32_t slotIndex) noexcept;

// False (color aspect) for a slot index that was never registered or is out of range.
bool SceneServiceSlotIsDepthResource(std::uint32_t slotIndex) noexcept;

// --- Testing only ---
// Clears every slot, the registration-failure memory and the sealed flag. Never call from production code.
void ResetSceneServiceRegistryForTesting() noexcept;
// --- End testing only ---

// Blackboard key a PreOpaque feature publishes its TextureHandle under for `slot` and the view its callback got.
// Folds `slot` and `view.Hash()` through FNV-1a so concurrently active views never alias.
rg::RenderPassId SceneServiceBlackboardKey(std::uint32_t slotIndex, rg::RenderViewId view) noexcept;

class Renderer;

// Fixed ceiling in bytes for binding 8's per-frame lighting uniform block (std140).
inline constexpr uint64_t kSceneGlobalUniformBlockSize = 544;

// Owns the scene-services descriptor-set layout (set 1 at the Pipeline level) and one VkDescriptorSet per active view
// and frame slot: a shared set would be unsafe with same-frame primary and secondary views, and a frame must never
// rewrite the set an earlier frame in flight still reads.
class SceneServicesDescriptorSet {
public:
    explicit SceneServicesDescriptorSet(Renderer& renderer);
    ~SceneServicesDescriptorSet();

    SceneServicesDescriptorSet(const SceneServicesDescriptorSet&) = delete;
    SceneServicesDescriptorSet& operator=(const SceneServicesDescriptorSet&) = delete;
    // Core owns exactly one instance for its whole lifetime, so move is not provided.

    // Opaque handle for code outside src/Renderer; valid for this object's lifetime.
    DescriptorSetLayoutHandle LayoutHandle() const noexcept { return m_layout.Handle(); }

    struct ResolvedSlot {
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
    };

    // Rewrites the descriptor set of `view` and frame `slot` in one vkUpdateDescriptorSets call (allocated on first
    // use). A scene slot counts as published only if both view and sampler are non-null; others get the dummy.
    // Call once per view per frame, from the consuming pass's execute(). `slot` is 0 for a synchronous frame.
    DescriptorSetHandle Rewrite(rg::RenderViewId view, const std::array<ResolvedSlot, kSceneServiceSlotCount>& resolved,
        std::uint32_t slot = 0);

    // Writes an opaque byte blob (<= kSceneGlobalUniformBlockSize) into the binding-8 uniform buffer of `view` and
    // the slot of `ticket`. The caller owns the schema. Call once per view per frame, like Rewrite().
    // Refused (and logged once) when that copy may still be read by a frame in flight.
    void UpdateGlobalUniformBlock(rg::RenderViewId view, std::span<const std::byte> bytes,
        const OffscreenFrameTicket& ticket);

    // The most recent Rewrite()-produced set for `view` and `slot`, or Invalid if none was made yet.
    DescriptorSetHandle DescriptorSetFor(rg::RenderViewId view, std::uint32_t slot = 0) const noexcept;

    // Dummy fallback resources backing unresolved slots.
    const Texture2D& DummyImage2DTextureFor(std::uint32_t slotIndex) const;

private:
    Renderer* m_renderer = nullptr;
    VkDevice m_device = VK_NULL_HANDLE;
    OwnedDescriptorSetLayout m_layout; // Hand-built layout (not reflected); the registry handle goes with it.

    struct PerViewSet {
        rg::RenderViewId view;
        std::uint32_t slot = 0;
        // Registered once when the set is first allocated; afterwards only rewritten in place (pool-owned set).
        DescriptorSetHandle handle = DescriptorSetHandle::Invalid;
    };
    std::vector<PerViewSet> m_perViewSets;

    // Separately typed dummies: the Image2D map is keyed by slot index; one Image3D dummy backs any Image3D slot.
    std::unordered_map<std::uint32_t, Texture2D> m_dummyImage2DTextures;
    // Built unconditionally; safe only while at most one Image3D-kind slot is registered before construction.
    VolumeTexture m_dummyImage3DTexture;

    // Declared after m_device and m_dummyImage3DTexture to match the constructor's init order. Binding 8 has no
    // per-call fallback: its dummy is written in Rewrite()'s single batched update (two back-to-back updates crash some drivers).
    Buffer m_dummyGlobalUniformBuffer;

    // Per-(view, slot) UBO backing binding 8 - populated lazily by
    // UpdateGlobalUniformBlock(), never by Rewrite() itself.
    struct PerViewUniformBuffer {
        rg::RenderViewId view;
        std::uint32_t slot = 0;
        Buffer buffer; // RAII, host-visible, persistently mapped (BufferMemoryUsage::CpuToGpu).
        RingCopyUse use; // The newest frame that wrote this copy.
    };
    std::vector<PerViewUniformBuffer> m_perViewUniformBuffers;
    bool m_rejectedWriteLogged = false;
};

} // namespace gte
