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

    // Keyed by name; the RenderViewId is derived from it, so no reverse index is needed.
    std::unordered_map<std::string, Entry> views;
};

} // namespace gte
