#pragma once

// Scene services: engine-wide slots for optional scene textures (depth maps, GI), a per-(slot, view) blackboard key,
// and the descriptor-set class that owns the layout, per-view sets and dummy fallbacks.

#include "RenderGraph/RenderPipeline.h" // rg::RenderPassId, rg::RenderViewId
#include "Buffer.h"
#include "../Core/GraphicsHandles.h" // DescriptorSetHandle
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

// Registration-order enumeration (Editor/debug-facing only - Core never
// interprets these strings). i must be < RegisteredSceneServiceSlotCount().
std::size_t RegisteredSceneServiceSlotCount() noexcept;
std::uint32_t RegisteredSceneServiceSlotIndexAt(std::size_t i) noexcept;

// "<unregistered>" / SceneServiceResourceKind::Image2D for a slot index that
// was never registered (or is out of range).
const char* SceneServiceSlotDebugName(std::uint32_t slotIndex) noexcept;
SceneServiceResourceKind SceneServiceSlotResourceKind(std::uint32_t slotIndex) noexcept;

// False (color aspect) for a slot index that was never registered or is out of range.
bool SceneServiceSlotIsDepthResource(std::uint32_t slotIndex) noexcept;

// Testing-only: clears every registered slot, the registration-order list,
// the failure-memory table, and the Runtime Sealing latch.
void ResetSceneServiceRegistryForTesting() noexcept;

// Blackboard key a PreOpaque feature publishes its TextureHandle under for `slot` and the view its callback got.
// Folds `slot` and `view.Hash()` through FNV-1a so concurrently active views never alias.
rg::RenderPassId SceneServiceBlackboardKey(std::uint32_t slotIndex, rg::RenderViewId view) noexcept;
class Renderer; // forward-declared only - the .cpp includes Renderer.h.

// Fixed ceiling for binding 8's packed per-frame lighting UBO (std140-safe). The feature's own
// settings struct must fit inside this many bytes.
inline constexpr uint64_t kSceneGlobalUniformBlockSize = 128;

// Owns the scene-services descriptor-set layout (set 1 at the Pipeline level) and one VkDescriptorSet per active view;
// a shared set would be unsafe with same-frame Game and Scene views.
class SceneServicesDescriptorSet {
public:
    explicit SceneServicesDescriptorSet(Renderer& renderer);
    ~SceneServicesDescriptorSet();

    SceneServicesDescriptorSet(const SceneServicesDescriptorSet&) = delete;
    SceneServicesDescriptorSet& operator=(const SceneServicesDescriptorSet&) = delete;
    // Core owns exactly one instance for its whole lifetime, so move is not provided.

    VkDescriptorSetLayout Layout() const noexcept { return m_layout; }

    // Opaque handle for code outside src/Renderer; valid for this object's lifetime.
    DescriptorSetLayoutHandle LayoutHandle() const noexcept { return m_layoutHandle; }

    struct ResolvedSlot {
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
    };

    // Rewrites `view`'s descriptor set in one vkUpdateDescriptorSets call (allocated on first use). A slot counts as
    // published only if both view and sampler are non-null; others get the dummy. Call once per view per frame, from the consuming pass's execute().
    DescriptorSetHandle Rewrite(rg::RenderViewId view, const std::array<ResolvedSlot, kSceneServiceSlotCount>& resolved);

    // Writes an opaque byte blob (<= kSceneGlobalUniformBlockSize) into `view`'s binding-8 uniform buffer.
    // The caller owns the schema. Call once per view per frame, like Rewrite().
    void UpdateGlobalUniformBlock(rg::RenderViewId view, std::span<const std::byte> bytes);

    // The most recent Rewrite()-produced VkDescriptorSet for `view`, or
    // VK_NULL_HANDLE if Rewrite() was never called for that view yet.
    DescriptorSetHandle DescriptorSetFor(rg::RenderViewId view) const noexcept;

    // Test-facing accessors confirming the real, uploaded dummy fallback
    // resources backing an unresolved slot are valid.
    const Texture2D& DummyImage2DTextureFor(std::uint32_t slotIndex) const;
    // Backs the Image3D-kind slot(s) registered so far - today, only ever one.
    const VolumeTexture& DummyImage3DTexture() const noexcept { return m_dummyImage3DTexture; }

private:
    Renderer* m_renderer = nullptr;
    VkDevice m_device = VK_NULL_HANDLE; // cached at construction, for Destroy().
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE; // owned - hand-built, NOT reflected; must be destroyed by this class.
    DescriptorSetLayoutHandle m_layoutHandle = DescriptorSetLayoutHandle::Invalid; // Non-owning handle to m_layout.

    struct PerViewSet {
        rg::RenderViewId view;
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

    // Per-view UBO backing binding 8 - populated lazily by
    // UpdateGlobalUniformBlock(), never by Rewrite() itself.
    struct PerViewUniformBuffer {
        rg::RenderViewId view;
        Buffer buffer; // RAII, host-visible, persistently mapped (BufferMemoryUsage::CpuToGpu).
    };
    std::vector<PerViewUniformBuffer> m_perViewUniformBuffers;
};

} // namespace gte
