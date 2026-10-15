#pragma once

#include "BuildRetryGate.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <unordered_map>

namespace gte::rg {

// One BuildRetryGate per failing key, capped at `Capacity`. A full map evicts an arbitrary entry,
// so a flood of distinct failing keys can never grow memory.
template <typename Key, typename Hash, typename Equal, std::size_t Capacity>
class BoundedRetryGates {
public:
    // True when `key` never failed, or its cooldown has elapsed. Accepts any type the Hash/Equal accept.
    template <typename Query>
    bool ShouldAttempt(const Query& key, std::uint64_t frame) const
    {
        const auto it = m_gates.find(key);
        return it == m_gates.end() || it->second.ShouldAttempt(frame);
    }

    BuildRetryGate::FailureKind OnFailure(const Key& key, std::uint64_t frame)
    {
        auto it = m_gates.find(key);
        if (it == m_gates.end()) {
            if (m_gates.size() >= Capacity) {
                m_gates.erase(m_gates.begin());
            }
            it = m_gates.emplace(key, BuildRetryGate{}).first;
        }
        return it->second.OnFailure(frame);
    }

    // True when `key` had failed before.
    template <typename Query>
    bool OnSuccess(const Query& key)
    {
        const auto it = m_gates.find(key);
        if (it == m_gates.end()) {
            return false;
        }
        m_gates.erase(it);
        return true;
    }

    template <typename Predicate>
    void EraseIf(Predicate&& shouldErase)
    {
        for (auto it = m_gates.begin(); it != m_gates.end();) {
            it = shouldErase(it->first) ? m_gates.erase(it) : std::next(it);
        }
    }

private:
    std::unordered_map<Key, BuildRetryGate, Hash, Equal> m_gates;
};

} // namespace gte::rg
