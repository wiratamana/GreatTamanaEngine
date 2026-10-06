#include "RenderViewRegistry.h"

#include "../../Renderer/Renderer.h"
#include "../Logging.h"

#include <cassert>
#include <cstring>

namespace gte {

bool DescsMatchForSameName(const RenderViewDesc& a, const RenderViewDesc& b) noexcept
{
    if (a.width != b.width || a.height != b.height || a.hasColor != b.hasColor || a.hasDepth != b.hasDepth) {
        return false;
    }
    if (a.hasDepth && a.allowDepthSampledAccess != b.allowDepthSampledAccess) {
        return false;
    }
    if (!a.hasColor) {
        return true; // colorFormat is meaningless for either side - never compared.
    }
    return a.colorFormat == b.colorFormat;
}

bool IsReservedViewName(const char* name) noexcept
{
    if (name == nullptr) {
        return false; // null is handled as its OWN refusal in CreateOrGetView(), not folded into "reserved".
    }
    for (const char* reserved : kReservedViewNames) {
        if (std::strcmp(name, reserved) == 0) {
            return true;
        }
    }
    return false;
}

RenderViewRegistry::RenderViewRegistry(Renderer& renderer) noexcept
    : m_renderer(&renderer)
{
}

rg::RenderViewId RenderViewRegistry::CreateOrGetView(const char* name, const RenderViewDesc& desc)
{
    assert(std::this_thread::get_id() == m_mainThreadId
        && "RenderViewRegistry::CreateOrGetView is main-thread-only - called from a different thread.");

    assert(name != nullptr && name[0] != '\0'
        && "RenderViewRegistry::CreateOrGetView requires a non-empty name");
    if (name == nullptr || name[0] == '\0') {
        GTE_LOG_ERROR("RenderViewRegistry", "CreateOrGetView() called with a null/empty name - refusing.");
        return rg::RenderViewId::Shared();
    }

    assert(!IsReservedViewName(name)
        && "RenderViewRegistry::CreateOrGetView: name collides with a reserved, built-in view name");
    if (IsReservedViewName(name)) {
        GTE_LOG_ERROR("RenderViewRegistry",
            std::string("CreateOrGetView() called with reserved view name \"") + name + "\" - refusing.");
        return rg::RenderViewId::Shared();
    }

    assert((desc.hasColor || desc.hasDepth)
        && "RenderViewRegistry::CreateOrGetView: a view with neither a color image nor a depth buffer is meaningless");
    if (!desc.hasColor && !desc.hasDepth) {
        GTE_LOG_ERROR("RenderViewRegistry",
            std::string("CreateOrGetView(\"") + name + "\") requested with hasColor == false AND hasDepth == "
            "false - refusing.");
        return rg::RenderViewId::Shared();
    }

    assert((!desc.allowDepthSampledAccess || desc.hasDepth)
        && "RenderViewRegistry::CreateOrGetView: allowDepthSampledAccess requested on a view with hasDepth == false");
    if (desc.allowDepthSampledAccess && !desc.hasDepth) {
        GTE_LOG_ERROR("RenderViewRegistry",
            std::string("CreateOrGetView(\"") + name + "\") requested with allowDepthSampledAccess == true AND "
            "hasDepth == false - refusing.");
        return rg::RenderViewId::Shared();
    }

    const std::string key(name);

    // Two-phase, exception-safe construction - mirrors
    // RenderGraphPersistentResourceCache::Resolve()'s own proven recipe
    // exactly (PHASE2 strategy doc, Step 2).
    auto [it, inserted] = m_views.try_emplace(key);

    if (!it->second.target.has_value()) {
        try {
            it->second.target.emplace(m_renderer->CreateRenderTexture(
                static_cast<int>(desc.width), static_cast<int>(desc.height), desc.colorFormat,
                // better-render-pass-3 campaign, BLOCK2 PHASE4 - found live
                // during this phase's own Memory-panel verification: passing
                // depthDebugName=nullptr meant a depth-only view's ONE real
                // GPU allocation (its depth buffer) showed up in the
                // Editor's "Memory" panel as "(unnamed)" instead of by its
                // own view name, making the required "exactly one row named
                // <ViewName>" check unverifiable by name. Reusing this same
                // map-owned, stable key string for BOTH debugName AND
                // depthDebugName is exactly as safe as using it once (see
                // this method's own two-phase construction comment above -
                // `it->first` outlives this RenderTexture for the registry's
                // entire process lifetime).
                /*debugName=*/it->first.c_str(), /*depthDebugName=*/it->first.c_str(),
                /*allowStorageImageAccess=*/false, /*allowDepthSampledAccess=*/desc.allowDepthSampledAccess,
                /*createDepthCompanion=*/desc.hasDepth, /*createColorImage=*/desc.hasColor));
            it->second.id = rg::RenderViewId::Named(it->first.c_str());
            it->second.desc = desc;
        } catch (...) {
            if (inserted) {
                m_views.erase(it);
            }
            throw;
        }
        return it->second.id;
    }

    // Already exists - compare descs (Section 3's "idempotent by name,
    // refused on mismatch" contract).
    if (!DescsMatchForSameName(desc, it->second.desc)) {
        if (!it->second.hasLoggedDescMismatch) {
            GTE_LOG_ERROR("RenderViewRegistry",
                std::string("CreateOrGetView(\"") + name + "\") was re-requested with a DIFFERENT desc than its "
                "existing view - this is always a caller bug (views are fixed-size for their whole lifetime, see "
                "PHASE0_MASTER_STRATEGY.md Locked Design Decision 2); the EXISTING view is returned unchanged.");
            it->second.hasLoggedDescMismatch = true;
        }
    }
    return it->second.id;
}

RenderTexture* RenderViewRegistry::FindViewTarget(rg::RenderViewId view) const noexcept
{
    assert(std::this_thread::get_id() == m_mainThreadId
        && "RenderViewRegistry::FindViewTarget is main-thread-only - called from a different thread.");
    for (auto& [name, entry] : m_views) {
        if (entry.target.has_value() && entry.id == view) {
            return &const_cast<RenderTexture&>(*entry.target);
        }
    }
    return nullptr;
}

} // namespace gte
