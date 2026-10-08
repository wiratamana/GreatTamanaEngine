#include "RenderGraphDebugVolumeTextureRegistry.h"

namespace gte::rg {

void RenderGraphDebugVolumeTextureRegistry::ApplyStateOverride(const std::string& name, const ResourceState& newState)
{
    if (DebugVolumeTextureSnapshot* entry = FindMutable(name)) {
        entry->state = newState;
    }
    // No entry yet - a safe no-op. The next real frame's Upsert() will
    // simply establish the entry with a correct state from scratch anyway.
}

} // namespace gte::rg
