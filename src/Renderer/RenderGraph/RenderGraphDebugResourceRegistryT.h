#pragma once

#include <optional>
#include <string>
#include <vector>

// Flat, name-keyed upsert table shared by the per-resource-kind debug registries. `SnapshotT` is a
// copyable value type with a `std::string name`. Linear scan, single-threaded (main thread only).
namespace gte::rg {

template <typename SnapshotT>
class DebugResourceRegistryT {
public:
    DebugResourceRegistryT() = default;

    // Inserts a new entry for `snapshot.name`, or overwrites the existing one in place.
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

    // A copy of the entry named `name`, or std::nullopt if never seen.
    std::optional<SnapshotT> FindByName(const std::string& name) const
    {
        for (const SnapshotT& entry : m_entries) {
            if (entry.name == name) {
                return entry;
            }
        }
        return std::nullopt;
    }

    // Every known entry, in first-seen order.
    std::vector<SnapshotT> ListAll() const { return m_entries; }

protected:
    // In-place lookup for a derived class's state override; nullptr if `name` was never Upsert()-ed.
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
