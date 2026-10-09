#pragma once

// Block 4 "Global Scene Services Descriptor Set"
// (task_manager/better-render-pass-6/PHASE0_MASTER_STRATEGY.md), PHASE1
// (PHASE1_SCENE_SERVICE_TYPES_AND_BLACKBOARD_KEY.md) - the fixed, versioned,
// engine-wide vocabulary every future "scene-wide optional texture" feature
// (shadow maps, GI, fog, and 5 more reserved slots) shares forever:
// SceneServiceSlot, its resource-kind classifier, and the per-(slot, view)
// RenderPassBlackboard key function that lets a PreOpaque feature publish
// data without colliding across concurrently-active render views (Game +
// Scene, same frame).
//
// PHASE2 (PHASE2_SCENE_SERVICES_DESCRIPTOR_SET_CLASS.md) EXTENDS this same
// header with the real, owning SceneServicesDescriptorSet CLASS below - the
// owned VkDescriptorSetLayout, per-view VkDescriptorSets, real uploaded
// dummy fallback resources, and Rewrite()/DescriptorSetFor().

#include "RenderGraph/RenderPipeline.h" // rg::RenderPassId, rg::RenderViewId
#include "Buffer.h"
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

// Fixed upper bound on how many scene-service slots can ever be registered -
// a shipped feature's own preferredIndex is a durable shader-binding contract
// (every consuming .frag file hardcodes `layout(set = 1, binding = N)`).
inline constexpr std::uint32_t kSceneServiceSlotCount = 8;

// Every slot is a VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER at the Vulkan
// descriptor level (uniform across all 8) - this enum exists ONLY to pick
// which underlying GPU resource TYPE (Texture2D vs VolumeTexture) backs a
// given slot's real data AND its dummy/neutral fallback, since a
// VK_IMAGE_VIEW_TYPE_2D view is not interchangeable with a
// VK_IMAGE_VIEW_TYPE_3D one for a shader that declared `sampler3D`.
enum class SceneServiceResourceKind : std::uint8_t { Image2D, Image3D };

// A return value >= kSceneServiceSlotCount from any function below means
// "no usable slot" - the one sentinel this whole API uses for every failure
// shape.
inline constexpr std::uint32_t kInvalidSceneServiceSlotIndex = kSceneServiceSlotCount;

// Registers (or re-confirms, if already registered under this exact name) a
// scene-service slot. Idempotent by debugName: a second call with the SAME
// name returns the SAME index every time. An out-of-range preferredIndex, a
// preferredIndex collision with a DIFFERENT name, a full registry, or a
// null/empty debugName are all refused: GTE_LOG_ERROR once per distinct
// (name, reason), return kInvalidSceneServiceSlotIndex (or, for a collision,
// the EXISTING occupant's own index). A null/empty debugName re-logs on
// EVERY such call (there is no name to key a "once" suppression entry on) -
// this is the one deliberate exception to the "once per distinct (name,
// reason)" promise above. Copies debugName into owned storage - the
// caller's buffer may be freed immediately after this call returns.
// Startup-only: refused once SceneServicesDescriptorSet::Rewrite() has
// completed anywhere in the process for the first time. Never call from
// code any frame-declare loop/render pass/provider invokes more than once.
//
// `isDepthResource` MUST be true for a slot whose real published texture is
// ever a depth-format image (e.g. a shadow map) - the generic per-slot
// render-graph read declaration (Core.cpp's RenderOpaque) has no other way
// to know which physical half (color vs depth) of an imported handle to
// synchronize/transition. Getting this wrong leaves the real depth image in
// the wrong Vulkan image layout when a shader samples it - undefined
// behavior that can crash a GPU driver outright.
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

// False ("color aspect") for a slot index that was never registered (or is
// out of range) - see RegisterSceneServiceSlot()'s own `isDepthResource` doc
// comment above for why a generic reader needs this.
bool SceneServiceSlotIsDepthResource(std::uint32_t slotIndex) noexcept;

// Testing-only: clears every registered slot, the registration-order list,
// the failure-memory table, and the Runtime Sealing latch.
void ResetSceneServiceRegistryForTesting() noexcept;

// The RenderPassBlackboard key a PreOpaque feature Publish()es its
// rg::TextureHandle under for `slot`, for the specific render view ITS OWN
// callback was invoked with (never a cached/stale view). CRITICAL: folds
// BOTH `slot` and `view.Hash()` into the result via a REAL FNV-1a avalanche
// (see this header's own implementation comment in the .cpp) - two
// concurrently-active views (this engine always has Game+Scene active
// together whenever the Editor is up) must never alias onto the same key.
rg::RenderPassId SceneServiceBlackboardKey(std::uint32_t slotIndex, rg::RenderViewId view) noexcept;
class Renderer; // forward-declared only - the .cpp includes Renderer.h.

// Fixed ceiling for binding 8's packed per-frame lighting UBO - std140-safe,
// the domain schema living in Features/Shadow/ShadowTypes.h must fit inside
// this many bytes.
inline constexpr VkDeviceSize kSceneGlobalUniformBlockSize = 128;

// Owns ONE reserved "scene services" descriptor-set LAYOUT (set = 1 at the
// Pipeline level - see Pipeline.h's own new sceneServicesSetLayout
// constructor parameter, PHASE3) and ONE real VkDescriptorSet PER
// concurrently-active rg::RenderViewId (never one shared instance - see
// PHASE0's global rule 5 for why a shared set is unsafe with this engine's
// confirmed same-frame Game+Scene dual-view rendering).
class SceneServicesDescriptorSet {
public:
    explicit SceneServicesDescriptorSet(Renderer& renderer);
    ~SceneServicesDescriptorSet();

    SceneServicesDescriptorSet(const SceneServicesDescriptorSet&) = delete;
    SceneServicesDescriptorSet& operator=(const SceneServicesDescriptorSet&) = delete;
    // Core owns exactly ONE instance for its entire lifetime (mirrors
    // RenderViewRegistry's own "create once, never torn down" convention) -
    // move is intentionally NOT provided; add it later only if a genuine
    // need arises, following Pipeline's own move-safety discipline exactly
    // (see PHASE0 global rule 9) if that day comes.

    VkDescriptorSetLayout Layout() const noexcept { return m_layout; }

    struct ResolvedSlot {
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
    };

    // Rewrites (ONE vkUpdateDescriptorSets call) the VkDescriptorSet
    // belonging to `view`, allocating/caching it on first use for that
    // view. A slot counts as "really published this call" ONLY when BOTH
    // resolved[slot].view AND .sampler are non-VK_NULL_HANDLE (PHASE0
    // global rule 12 - an imported-texture-handle edge case can otherwise
    // resolve a valid view with a null sampler) - every other slot gets
    // this class's own dummy resource substituted in, selected via
    // SceneServiceSlotResourceKind(slot). Call ONCE per view per frame,
    // from inside the SAME pass's own execute() callback that is about to
    // Submit() against it (see PHASE7) - never from outside a pass's
    // execution window.
    VkDescriptorSet Rewrite(rg::RenderViewId view, const std::array<ResolvedSlot, kSceneServiceSlotCount>& resolved);

    // Writes an opaque, fixed-size byte blob (<= kSceneGlobalUniformBlockSize)
    // into the uniform buffer bound at Scene Services binding 8, for `view`.
    // Content/schema is owned entirely by the caller - this class never
    // interprets it. Call once per view per frame, same discipline as Rewrite().
    void UpdateGlobalUniformBlock(rg::RenderViewId view, std::span<const std::byte> bytes);

    // The most recent Rewrite()-produced VkDescriptorSet for `view`, or
    // VK_NULL_HANDLE if Rewrite() was never called for that view yet.
    VkDescriptorSet DescriptorSetFor(rg::RenderViewId view) const noexcept;

    // Test-facing accessors confirming the real, uploaded dummy fallback
    // resources backing an unresolved slot are valid.
    const Texture2D& DummyImage2DTextureFor(std::uint32_t slotIndex) const;
    // Backs the Image3D-kind slot(s) registered so far - today, only ever one.
    const VolumeTexture& DummyImage3DTexture() const noexcept { return m_dummyImage3DTexture; }

private:
    Renderer* m_renderer = nullptr;
    VkDevice m_device = VK_NULL_HANDLE; // cached at construction, for Destroy().
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE; // owned - hand-built, NOT reflected; must be destroyed by this class.

    struct PerViewSet {
        rg::RenderViewId view;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };
    std::vector<PerViewSet> m_perViewSets;

    // Deliberately TWO SEPARATE, correctly-typed dummy containers. Keyed by
    // raw slot index - only the Image2D map is populated per-slot; the single
    // Image3D dummy below backs any Image3D-kind slot directly.
    std::unordered_map<std::uint32_t, Texture2D> m_dummyImage2DTextures;
    // Built unconditionally regardless of registry state. Safe only while at
    // most one Image3D-kind slot is ever registered before this constructor
    // runs.
    VolumeTexture m_dummyImage3DTexture;

    // DECLARATION ORDER IS LOAD-BEARING: must stay strictly after m_device
    // and m_dummyImage3DTexture above, matching the constructor's own
    // initializer-list order (C++ initializes in declaration order, not
    // init-list order).
    //
    // ROBUSTNESS: binding 8 has NO per-call fallback the way bindings 0-7
    // do inside Rewrite()'s own loop (that loop only ever touches indices
    // 0..kSceneServiceSlotCount-1). A brand-new per-view set's binding 8 is
    // written with this dummy buffer as PART OF Rewrite()'s own single
    // batched vkUpdateDescriptorSets call (never a separate call - two
    // back-to-back descriptor-set updates against a freshly allocated set
    // is a known crash trigger on at least one targeted Vulkan driver).
    // Built ONCE in the constructor, zero-filled.
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
