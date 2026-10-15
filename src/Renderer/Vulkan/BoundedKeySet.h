#pragma once

#include <cstddef>
#include <functional>
#include <unordered_set>

namespace gte {

// Set of keys with a hard size cap. Insert refuses new keys once full.
template <typename Key, std::size_t Capacity, typename Hash = std::hash<Key>>
class BoundedKeySet {
public:
    bool Contains(const Key& key) const { return m_keys.find(key) != m_keys.end(); }

    // False when the set is full and the key is not already present.
    bool Insert(const Key& key)
    {
        if (m_keys.size() >= Capacity && m_keys.find(key) == m_keys.end()) {
            return false;
        }
        m_keys.insert(key);
        return true;
    }

    void Erase(const Key& key) { m_keys.erase(key); }

private:
    std::unordered_set<Key, Hash> m_keys;
};

} // namespace gte
