#pragma once

#include <optional>
#include <string>
#include <vector>

// Generic flat, name-keyed upsert table shared by every per-resource-kind
// debug registry (RenderGraphDebugTextureRegistry,
// RenderGraphDebugVolumeTextureRegistry). `SnapshotT` must be a plain,
// copyable value type with a `std::string name` member - this template owns
// only the Upsert/FindByName/ListAll mechanics; each resource kind's own
// state-override method (shape differs - color+depth vs a single state)
// stays in its own derived class.
//
// A flat vector, scanned linearly - this engine's scale (a few dozen live
// debug resources at most) makes a hash map unwarranted. Single-threaded,
// main-thread-only, same as every other RenderGraph-adjacent class.
namespace gte::rg {

template <typename SnapshotT>
class DebugResourceRegistryT {
public:
    DebugResourceRegistryT() = default;

    // Inserts a brand-new entry for `snapshot.name`, or overwrites an
    // existing one in place (every field replaced).
    void Upsert(const SnapshotT& snapshot)
    {
        for (SnapshotT& entry : m_entries) {
            if (entry.name == snapshot.name) {
                entry = snapshot;
                return;
            }
        }
        m_entries.push_back(snapshot);
    }

    // Returns a copy of the entry named `name`, or std::nullopt if never seen.
    std::optional<SnapshotT> FindByName(const std::string& name) const
    {
        for (const SnapshotT& entry : m_entries) {
            if (entry.name == name) {
                return entry;
            }
        }
        return std::nullopt;
    }

    // Every currently-known entry, in first-seen (stable) order.
    std::vector<SnapshotT> ListAll() const { return m_entries; }

protected:
    // In-place lookup for a derived class's own state-override method -
    // nullptr if `name` has never been Upsert()-ed (a safe no-op for the
    // caller, never an assertion).
    SnapshotT* FindMutable(const std::string& name)
    {
        for (SnapshotT& entry : m_entries) {
            if (entry.name == name) {
                return &entry;
            }
        }
        return nullptr;
    }

private:
    std::vector<SnapshotT> m_entries;
};

} // namespace gte::rg
