#pragma once

// Name -> physical-texture-snapshot table behind GET /get_texture and GET /list_textures. A passive store:
// RenderGraph calls Upsert() on every ExecuteCompiledGraph(), and every declared texture can appear here.

#include "RenderGraphBarrierPlanner.h"
#include "../../Core/Extent.h"
#include "RenderGraphDebugResourceRegistryT.h"
#include "../RenderTarget.h"

#include <cstdint>
#include <string>

namespace gte::rg {

enum class ExecuteTimingMode : std::uint8_t; // Forward-declared to avoid a circular include with RenderGraph.h.

// One texture's current state: color required, depth optional. A plain copyable value; raw handles
// in it are valid only while the underlying resource lives.
struct DebugTextureSnapshot {
    std::string name;
    ExecuteTimingMode regime{};

    RenderTarget target; // color half; depthImage/depthImageView/depthFormat is the optional depth half.
    bool hasDepth = false;

    // The live VkSampler of each half (ImGui_ImplVulkan_AddTexture needs a sampler, not only a view).
    // depthSampler is meaningful only when hasDepth is true.
    VkSampler sampler = VK_NULL_HANDLE;
    VkSampler depthSampler = VK_NULL_HANDLE;

    ResourceState colorState; // layout/stage/access the registry last believes the color image is in.
    ResourceState depthState; // meaningful only when hasDepth is true.

    // Registry frame counter at the last Upsert(); ApplyColorStateOverride() does not touch it.
    std::uint64_t lastUpdatedFrameCounter = 0;

    // Opaque identity of the color image view (0 when none), the color extent and whether a color image exists,
    // without Vulkan types.
    std::uint64_t ColorViewId() const noexcept { return reinterpret_cast<std::uintptr_t>(target.imageView); }
    bool HasColorImage() const noexcept { return target.image != VK_NULL_HANDLE; }
    Extent2D ColorExtent() const noexcept { return Extent2D{ target.extent.width, target.extent.height }; }
};

// Adds the color-state override a 2D texture needs; the shared base provides Upsert/FindByName/ListAll.
class RenderGraphDebugTextureRegistry : public DebugResourceRegistryT<DebugTextureSnapshot> {
public:
    RenderGraphDebugTextureRegistry() = default;

    // Overwrites only the color ResourceState of `name` (no-op if unknown). Leaves
    // `lastUpdatedFrameCounter` alone: a state correction is not a fresh capture.
    void ApplyColorStateOverride(std::string_view name, const ResourceState& newColorState);
};

} // namespace gte::rg
