#pragma once

#include "../../Renderer/RenderGraph/RenderGraphHandles.h"
#include "../PixelFormat.h"
#include "../UiTextureHandle.h"

#include <cstdint>
#include <memory>

// Owns every dynamically created view's persistent render target, keyed by
// name. "Create once, reuse every frame, live for the process's whole
// lifetime" - a view's resolution and lifetime are both fixed at creation,
// no resize/eviction/teardown API. Public, zero Vulkan dependency - every
// Vulkan-bearing field lives in a private Impl (Renderer/Internal/
// RenderViewRegistryImpl.h).
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

    // Idempotent by name - refused (reserved name / null-or-empty name /
    // mismatched desc on an existing name) requests return
    // rg::RenderViewId::Shared() and insert nothing. May throw
    // std::runtime_error if the underlying render target construction
    // genuinely fails (e.g. out-of-device-memory) - the registry itself
    // remains safely retryable afterward for the same name.
    rg::RenderViewId CreateOrGetView(const char* name, const RenderViewDesc& desc);

    // nullptr if `view` was never created via CreateOrGetView() above.
    // Kept for Core::FindRenderViewTarget()'s own callers - RenderTexture
    // stays forward-declared only, never fully included here.
    RenderTexture* FindViewTarget(rg::RenderViewId view) const noexcept;

    // Opaque, Vulkan-free sibling of FindViewTarget() above - for a caller
    // that only wants to display this view (ImGui::Image()), never touch it
    // as a real render target.
    UiTextureHandle FindViewUiTextureHandle(rg::RenderViewId view) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace gte
