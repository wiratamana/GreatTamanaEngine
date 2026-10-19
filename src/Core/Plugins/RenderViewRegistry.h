#pragma once

#include "../../Renderer/RenderGraph/RenderGraphHandles.h"
#include "../PixelFormat.h"

#include <cstdint>
#include <memory>

// Owns every dynamically created view's persistent render target, keyed by name.
// Views live for the process lifetime and can be resized. No Vulkan types here.
namespace gte {

class Renderer;
class RenderTexture; // Forward-declared only - a pointer to this incomplete type carries zero Vk* text.

struct RenderViewDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool hasColor = true; // false => a genuinely colorless render target, not a bookkeeping-only flag.
    bool hasDepth = true;
    // Undefined == match Renderer::ColorFormat(). Meaningless when hasColor is false.
    PixelFormat colorFormat = PixelFormat::Undefined;
    // Opts the depth image into a real sampler, so a later pass can read
    // this view's depth back as a texture. Meaningless when hasDepth is false.
    bool allowDepthSampledAccess = false;
};

// Equality used only to detect "same name, different desc" misuse in
// CreateOrGetView() below - colorFormat is ignored whenever either side has
// hasColor == false (a depth-only view requested twice with two different,
// both-irrelevant colorFormat values is never a caller bug).
bool DescsMatchForSameName(const RenderViewDesc& a, const RenderViewDesc& b) noexcept;

// Single source of truth for every view name this engine itself already
// owns - CreateOrGetView() checks a caller-supplied name against this first.
inline constexpr const char* kReservedViewNames[] = { "Game", "Scene" };
bool IsReservedViewName(const char* name) noexcept;

class RenderViewRegistry {
public:
    explicit RenderViewRegistry(Renderer& renderer) noexcept;
    ~RenderViewRegistry();

    RenderViewRegistry(const RenderViewRegistry&) = delete;
    RenderViewRegistry& operator=(const RenderViewRegistry&) = delete;

    // Idempotent by name. A refused request (bad name or desc) returns rg::RenderViewId::Shared().
    // Throws std::runtime_error when the target cannot be created; the same name can be retried.
    rg::RenderViewId CreateOrGetView(const char* name, const RenderViewDesc& desc);

    // Resizes the view's images (waits for the GPU to go idle). True when the view has the requested size
    // afterwards. On failure the target is dropped, so the next CreateOrGetView of that name rebuilds it.
    bool ResizeView(rg::RenderViewId view, std::uint32_t width, std::uint32_t height);

    // nullptr if `view` was never created via CreateOrGetView().
    RenderTexture* FindViewTarget(rg::RenderViewId view) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace gte
