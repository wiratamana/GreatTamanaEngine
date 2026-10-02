#pragma once

// Block 4 "Global Scene Services Descriptor Set"
// (task_manager/better-render-pass-6/PHASE0_MASTER_STRATEGY.md), PHASE1
// (PHASE1_SCENE_SERVICE_TYPES_AND_BLACKBOARD_KEY.md) - the fixed, versioned,
// engine-wide vocabulary every future "scene-wide optional texture" feature
// (shadow maps, GI, fog, and 5 more reserved slots) shares forever:
// SceneServiceSlot, its resource-kind classifier, and the per-(slot, view)
// RenderPassBlackboard key function that lets a PreOpaque feature publish
// data without colliding across concurrently-active render views (Game +
// Scene, same frame). This phase is pure logic only - no Vulkan device
// object is touched, no descriptor set is created yet; the full
// SceneServicesDescriptorSet CLASS (owned layout, per-view VkDescriptorSets,
// dummy fallback resources, Rewrite()/DescriptorSetFor()) is added by PHASE2
// (PHASE2_SCENE_SERVICES_DESCRIPTOR_SET_CLASS.md).

#include "RenderGraph/RenderPipeline.h" // rg::RenderPassId, rg::RenderViewId

#include <cstdint>

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

} // namespace gte
