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
#include "Texture2D.h"
#include "VolumeTexture.h"

#include <volk.h>

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace gte {

// FIXED, versioned, engine-owned list - append-only. NEVER renumber once
// shipped; a shipped slot's numeric value is a durable ABI/shader contract
// (every consuming .frag file hardcodes `layout(set = 1, binding = N)`).
enum class SceneServiceSlot : std::uint32_t {
    ShadowMap = 0, // sampler2D - a 2D shadow depth/visibility map.
    GIVolume = 1, // sampler2D - a baked GI data atlas (2D-shaped TODAY -
                  // see SceneServiceSlotResourceKind()'s own doc comment
                  // before ever reinterpreting this as a real 3D volume).
    VolumetricFog = 2, // sampler3D - a REAL VK_IMAGE_TYPE_3D volume/froxel
                       // texture (VolumeTexture.h). The ONE Image3D-kind slot
                       // among today's 3 named slots.
    // Reserved = 3..7. Document each real slot's resource kind explicitly in
    // SceneServiceSlotResourceKind() the moment it is actually added - never
    // leave it implicit. Bump kSceneServiceSlotCount only when a slot 3+ is
    // actually wired, not before.
};
inline constexpr std::uint32_t kSceneServiceSlotCount = 8;

// Every slot is a VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER at the Vulkan
// descriptor level (uniform across all 8) - this enum exists ONLY to pick
// which underlying GPU resource TYPE (Texture2D vs VolumeTexture) backs a
// given slot's real data AND its dummy/neutral fallback, since a
// VK_IMAGE_VIEW_TYPE_2D view is not interchangeable with a
// VK_IMAGE_VIEW_TYPE_3D one for a shader that declared `sampler3D`.
enum class SceneServiceResourceKind : std::uint8_t { Image2D, Image3D };

constexpr SceneServiceResourceKind SceneServiceSlotResourceKind(SceneServiceSlot slot) noexcept
{
    return slot == SceneServiceSlot::VolumetricFog ? SceneServiceResourceKind::Image3D
                                                    : SceneServiceResourceKind::Image2D;
}

// The RenderPassBlackboard key a PreOpaque feature Publish()es its
// rg::TextureHandle under for `slot`, for the specific render view ITS OWN
// callback was invoked with (never a cached/stale view). CRITICAL: folds
// BOTH `slot` and `view.Hash()` into the result via a REAL FNV-1a avalanche
// (see this header's own implementation comment in the .cpp) - two
// concurrently-active views (this engine always has Game+Scene active
// together whenever the Editor is up) must never alias onto the same key.
rg::RenderPassId SceneServiceBlackboardKey(SceneServiceSlot slot, rg::RenderViewId view) noexcept;

class Renderer; // forward-declared only - the .cpp includes Renderer.h.

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

    // The most recent Rewrite()-produced VkDescriptorSet for `view`, or
    // VK_NULL_HANDLE if Rewrite() was never called for that view yet.
    VkDescriptorSet DescriptorSetFor(rg::RenderViewId view) const noexcept;

    // Test-facing accessors - see this phase's own "Testability correction"
    // (PHASE2_SCENE_SERVICES_DESCRIPTOR_SET_CLASS.md, Step 2) for why these
    // exist instead of a live-descriptor-set-introspection helper.
    const Texture2D& DummyImage2DTextureFor(SceneServiceSlot slot) const;
    const VolumeTexture& DummyVolumetricFogTexture() const noexcept { return m_dummyVolumetricFogTexture; }

private:
    Renderer* m_renderer = nullptr;
    VkDevice m_device = VK_NULL_HANDLE; // cached at construction, for Destroy().
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE; // owned - hand-built, NOT reflected; must be destroyed by this class.

    struct PerViewSet {
        rg::RenderViewId view;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };
    std::vector<PerViewSet> m_perViewSets;

    // Deliberately TWO SEPARATE, correctly-typed dummy containers - see
    // PHASE0 global rule 4. Keyed by raw slot index; the one Image3D slot
    // (VolumetricFog) is never populated in this map.
    std::unordered_map<std::uint32_t, Texture2D> m_dummyImage2DTextures;
    VolumeTexture m_dummyVolumetricFogTexture;
};

} // namespace gte
