#include "RenderGraphDebugTextureRegistry.h"

namespace gte::rg {

void RenderGraphDebugTextureRegistry::Upsert(const DebugTextureSnapshot& snapshot)
{
    for (DebugTextureSnapshot& entry : m_entries) {
        if (entry.name == snapshot.name) {
            entry = snapshot;
            return;
        }
    }
    m_entries.push_back(snapshot);
}

void RenderGraphDebugTextureRegistry::ApplyColorStateOverride(
    const std::string& name, const ResourceState& newColorState)
{
    for (DebugTextureSnapshot& entry : m_entries) {
        if (entry.name == name) {
            entry.colorState = newColorState;
            return;
        }
    }
    // No entry yet - a safe no-op, per this method's own doc comment. This
    // can legitimately happen if Application.cpp's correction call ever
    // races ahead of the FIRST Upsert() for a brand-new name (should not
    // happen in practice given call ordering - see Phase 3 - but is not a
    // bug worth asserting on if it ever does; the next real frame's
    // Upsert() will simply establish the entry with a correct state from
    // scratch anyway).
}

std::optional<DebugTextureSnapshot> RenderGraphDebugTextureRegistry::FindByName(const std::string& name) const
{
    for (const DebugTextureSnapshot& entry : m_entries) {
        if (entry.name == name) {
            return entry;
        }
    }
    return std::nullopt;
}

std::vector<DebugTextureSnapshot> RenderGraphDebugTextureRegistry::ListAll() const
{
    return m_entries;
}

} // namespace gte::rg
