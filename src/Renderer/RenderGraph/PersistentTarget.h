#pragma once

#include "RenderGraphBuilder.h"
#include "../../Core/Extent.h"
#include "../../Core/PixelFormat.h"

#include <algorithm>

namespace gte::rg {

// Per-view color texture that survives across frames. Owned by the open RenderFeatureScope.
class PersistentTarget {
public:
    // Returns an invalid handle on refusal (no scope, bad format, double request); callers must check.
    // `name` is copied into the cache key, so it only has to outlive the call.
    TextureHandle Acquire(RenderGraphBuilder& builder, const char* name, Extent2D extent, PixelFormat format,
        TextureUsage usage = TextureUsage::Storage)
    {
        TextureDesc desc;
        desc.width = std::max(1u, extent.width);
        desc.height = std::max(1u, extent.height);
        desc.format = format;
        desc.usage = usage;
        return builder.GetOrCreatePersistentTexture(m_token, name, desc);
    }

private:
    PersistentTextureCacheToken m_token;
};

} // namespace gte::rg
