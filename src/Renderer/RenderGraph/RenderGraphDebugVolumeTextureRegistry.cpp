#include "RenderGraphDebugVolumeTextureRegistry.h"

namespace gte::rg {

void RenderGraphDebugVolumeTextureRegistry::Upsert(const DebugVolumeTextureSnapshot& snapshot)
{
    for (DebugVolumeTextureSnapshot& entry : m_entries) {
        if (entry.name == snapshot.name) {
            entry = snapshot;
            return;
        }
    }
    m_entries.push_back(snapshot);
}

void RenderGraphDebugVolumeTextureRegistry::ApplyStateOverride(
    const std::string& name, const ResourceState& newState)
{
    for (DebugVolumeTextureSnapshot& entry : m_entries) {
        if (entry.name == name) {
            entry.state = newState;
            return;
        }
    }
    // No entry yet - a safe no-op, per this method's own doc comment. This
    // can legitimately happen if a graph-external correction call ever races
    // ahead of the FIRST Upsert() for a brand-new name; the next real
    // frame's Upsert() will simply establish the entry with a correct state
    // from scratch anyway - mirrors
    // RenderGraphDebugTextureRegistry::ApplyColorStateOverride()'s own
    // identical reasoning.
}

std::optional<DebugVolumeTextureSnapshot> RenderGraphDebugVolumeTextureRegistry::FindByName(const std::string& name) const
{
    for (const DebugVolumeTextureSnapshot& entry : m_entries) {
        if (entry.name == name) {
            return entry;
        }
    }
    return std::nullopt;
}

std::vector<DebugVolumeTextureSnapshot> RenderGraphDebugVolumeTextureRegistry::ListAll() const
{
    return m_entries;
}

} // namespace gte::rg
