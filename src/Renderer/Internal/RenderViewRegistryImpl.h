#pragma once

// Renderer-internal only - never included by a Project Assembly. Holds
// every Vk*/RenderTexture field RenderViewRegistry.h used to expose
// directly. Only RenderViewRegistry.cpp includes this.

#include "../../Core/Plugins/RenderViewRegistry.h"
#include "../RenderTexture.h"

#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

namespace gte {

class Renderer;

struct RenderViewRegistry::Impl {
    explicit Impl(Renderer& rendererRef) noexcept
        : renderer(&rendererRef)
    {
    }

    Renderer* renderer;

    // Set once at construction, on the engine's own main thread.
    // CreateOrGetView()/FindViewTarget() both assert every call still comes
    // from this thread.
    const std::thread::id mainThreadId = std::this_thread::get_id();

    struct Entry {
        rg::RenderViewId id;
        RenderViewDesc desc;
        bool hasLoggedDescMismatch = false;
        std::optional<RenderTexture> target;
    };

    // Keyed by NAME (not by RenderViewId) - the name is the caller-facing
    // identity; the RenderViewId is derived from it (rg::RenderViewId::
    // Named(name)) and is recoverable from it at any time, so a reverse
    // index is never needed. FindViewTarget() does a small linear scan over
    // this same map instead.
    std::unordered_map<std::string, Entry> views;
};

} // namespace gte
