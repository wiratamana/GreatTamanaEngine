#pragma once

// The pure, Vulkan-header-only (no live VkDevice/VmaAllocator anywhere in
// this file) name -> physical-texture-snapshot table behind
// GET /get_texture and GET /list_textures. This class itself has no opinion
// about HOW it gets populated - it is a dumb, passive store; RenderGraph is
// what actually calls Upsert() every ExecuteCompiledGraph() call.
//
// Every texture RenderGraphBuilder::CreateTexture()/ImportTexture() ever
// declares, in either ExecuteTimingMode regime, is automatically eligible
// to appear here.

#include "RenderGraphBarrierPlanner.h"
#include "RenderGraphDebugResourceRegistryT.h"
#include "../RenderTarget.h"

#include <cstdint>
#include <string>

namespace gte::rg {

enum class ExecuteTimingMode : std::uint8_t; // see RenderGraph.h - forward-declared to avoid a circular include.

// One texture's full current knowledge - color required, depth optional.
// Deliberately a plain, copyable value (no pointers/handles owned) - safe
// to hand back out of the registry by value; a VkImage/VkImageView handle
// copied out of this registry is only meaningful for as long as the real
// underlying resource is still alive, exactly like any other raw Vulkan
// handle this engine threads around by value (e.g. RenderTarget itself).
struct DebugTextureSnapshot {
    std::string name;
    ExecuteTimingMode regime{};

    RenderTarget target; // color half; depthImage/depthImageView/depthFormat is the optional depth half.
    bool hasDepth = false;

    // The real, live VkSampler for each half - required to back an ImGui
    // descriptor (ImGui_ImplVulkan_AddTexture() needs a real VkSampler,
    // never just a VkImageView). depthSampler is meaningful only when
    // hasDepth is true.
    VkSampler sampler = VK_NULL_HANDLE;
    VkSampler depthSampler = VK_NULL_HANDLE;

    ResourceState colorState; // layout/stage/access this registry LAST believes the color image is actually in.
    ResourceState depthState; // meaningful only when hasDepth is true.

    // This registry's own monotonically increasing frame counter value at
    // the moment this entry was last written by Upsert() - NOT touched by
    // ApplyColorStateOverride(), which corrects state only, never freshness.
    std::uint64_t lastUpdatedFrameCounter = 0;
};

// Upsert/FindByName/ListAll mechanics live in the shared
// DebugResourceRegistryT<DebugTextureSnapshot> base - this class only adds
// the color-half state-override method a 2D texture needs that a volume
// texture (single state, no depth concept) does not.
class RenderGraphDebugTextureRegistry : public DebugResourceRegistryT<DebugTextureSnapshot> {
public:
    RenderGraphDebugTextureRegistry() = default;

    // Overwrites JUST the color ResourceState of the entry named `name` (a
    // safe no-op if no such entry exists yet). Deliberately does NOT touch
    // `lastUpdatedFrameCounter` - a state correction is not a fresh capture
    // of contents.
    void ApplyColorStateOverride(const std::string& name, const ResourceState& newColorState);
};

} // namespace gte::rg
