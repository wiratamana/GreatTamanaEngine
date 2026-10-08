#include "RenderGraphDebugTextureRegistry.h"

namespace gte::rg {

void RenderGraphDebugTextureRegistry::ApplyColorStateOverride(
    const std::string& name, const ResourceState& newColorState)
{
    if (DebugTextureSnapshot* entry = FindMutable(name)) {
        entry->colorState = newColorState;
    }
    // No entry yet - a safe no-op. The next real frame's Upsert() will
    // simply establish the entry with a correct state from scratch anyway.
}

} // namespace gte::rg
