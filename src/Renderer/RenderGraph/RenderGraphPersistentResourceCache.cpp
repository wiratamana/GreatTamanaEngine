#include "RenderGraphPersistentResourceCache.h"

namespace gte::rg {

bool IsStaleCacheEntry(
    std::uint64_t lastUsedFrame, std::uint64_t currentFrame, std::uint64_t staleThresholdFrames) noexcept
{
    return (currentFrame - lastUsedFrame) > staleThresholdFrames;
}

} // namespace gte::rg
